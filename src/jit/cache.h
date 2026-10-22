// cache.h: code cache, block table and self-modifying-code protection.
//
// The MAP_JIT code cache, the guest-eip -> block hash table with chaining (pending links), the hand-assembled entry/exit/IBTC
// stubs, and the write-protection scheme: translated 16 KiB guest pages are made read-only; a store faults, the page is
// unprotected and the whole cache is flushed before the next block runs.

#define OFF(f) ((int)offsetof(CPU, f))
#define XC 27            // host register holding the CPU*
#define XG 28            // host register holding G
#define GR(i) (19 + (i)) // guest register -> host register
#define CACHE_SIZE (112u << 20)
#define IBTC_BITS 18
#define BH_BITS 20
#define MAXBLK (1 << 19)
#define MAXB 96 // guest instructions per block

typedef struct Block {
    u32 guest, gend;
    u32 *code, *end;
    u16 nins;
    u8 liveout, livein;
} Block;
typedef struct Pend {
    u32 *site;
    u32 tgt;
    struct Pend *next;
} Pend;
typedef struct {
    u32 key, pad;
    u64 code;
} IbtcEnt;

static int jit_on, jcheck, jstat, jnochain, jnoibtc;
static u32 jdump;
static u8 *cache;
static u32 *cache_base, *cache_top, *cache_lim;
static Block **bh, *bpool;
static u32 nblocks;
static Pend *pend_pool, *phead[1 << 14];
static u32 npend;
static IbtcEnt *ibtc;
static u32 *t_enter, *t_exit, *t_exit_ns, *t_ibtc, *t_fb, *t_fbc, *t_wlog;
static u64 st_trans, st_flush, st_fb_static, st_nat_static, st_fbcount, st_ibtc_miss, st_runs, st_smc;
static u64 fbhist[0x200];
static volatile int smc_flag, smc_hit;
static volatile long smc_jb;

static void jit_fb(CPU *c);
static void jit_fbc(CPU *c, u32 expect);
static void jit_wlog(u32 addr, u32 sz);

static inline void wp(int on) {
    pthread_jit_write_protect_np(on);
}

static inline u32 bh_hash(u32 a) {
    return (a * 2654435761u) >> (32 - BH_BITS);
}
static Block *blk_find(u32 a) {
    for (u32 h = bh_hash(a);; h = (h + 1) & ((1u << BH_BITS) - 1)) {
        Block *b = bh[h];
        if (!b)
            return NULL;
        if (b->guest == a)
            return b;
    }
}
static void blk_insert(Block *b) {
    u32 h = bh_hash(b->guest);
    while (bh[h])
        h = (h + 1) & ((1u << BH_BITS) - 1);
    bh[h] = b;
}
static inline void ibtc_put(Block *b) {
    if (jnoibtc)
        return;
    IbtcEnt *e = &ibtc[(b->guest ^ (b->guest >> 15)) & ((1u << IBTC_BITS) - 1)];
    e->key = b->guest;
    e->code = (u64)b->code;
}

static void emit_lit64(u64 v) {
    E((u32)v);
    E((u32)(v >> 32));
}
static void patch_ldrlit(u32 *at, u32 *lit) {
    *at |= ((u32)(lit - at) & 0x7ffff) << 5;
}

// Hand-assembled entry/exit/ibtc stubs at the start of the cache.
static void jit_gen_tramps(void) {
    u32 *ldrpos, *lit;
    t_enter = jp; // u64 enter(CPU *c /*x0*/, void *code /*x1*/)
    a_stpx_pre(29, 30, 31, -112);
    a_stpx(19, 20, 31, 16);
    a_stpx(21, 22, 31, 32);
    a_stpx(23, 24, 31, 48);
    a_stpx(25, 26, 31, 64);
    a_stpx(27, 28, 31, 80);
    a_movx_r(XC, 0);
    ldrpos = jp;
    a_ldrlit_x(XG, 0);
    a_ldpw(19, 20, XC, 0);
    a_ldpw(21, 22, XC, 8);
    a_ldpw(23, 24, XC, 16);
    a_ldpw(25, 26, XC, 24);
    a_br(1);
    lit = jp;
    emit_lit64((u64)G);
    patch_ldrlit(ldrpos, lit);

    t_exit = jp; // w0 = guest eip; stores the registers
    a_stpw(19, 20, XC, 0);
    a_stpw(21, 22, XC, 8);
    a_stpw(23, 24, XC, 16);
    a_stpw(25, 26, XC, 24);
    a_memi(STRWI, 0, XC, OFF(eip));
    t_exit_ns = jp; // registers and c->eip already in the CPU struct
    a_ldpx(19, 20, 31, 16);
    a_ldpx(21, 22, 31, 32);
    a_ldpx(23, 24, 31, 48);
    a_ldpx(25, 26, 31, 64);
    a_ldpx(27, 28, 31, 80);
    a_ldpx_post(29, 30, 31, 112);
    a_ret();

    t_ibtc = jp; // w0 = guest target; probes the direct-mapped table
    a_rrr(EOR, 1, 0, 0, LSR_, 15);
    a_ubfx(1, 1, 0, IBTC_BITS);
    ldrpos = jp;
    a_ldrlit_x(2, 0);
    a_rrrx(ADD, 2, 2, 1, LSL_, 4);
    a_memi(LDRWI, 3, 2, 0);
    a_cmp(3, 0);
    a_bcond(CNE, 3); // -> miss
    a_memi(LDRXI, 3, 2, 8);
    a_br(3);
    a_b(t_exit); // miss: back to the dispatcher in C
    lit = jp;
    emit_lit64((u64)ibtc);
    patch_ldrlit(ldrpos, lit);

    t_fb = jp;
    ldrpos = jp;
    a_ldrlit_x(16, 0);
    a_br(16);
    lit = jp;
    emit_lit64((u64)jit_fb);
    patch_ldrlit(ldrpos, lit);
    t_fbc = jp;
    ldrpos = jp;
    a_ldrlit_x(16, 0);
    a_br(16);
    lit = jp;
    emit_lit64((u64)jit_fbc);
    patch_ldrlit(ldrpos, lit);

    t_wlog = jp;    // w16 = guest address, w17 = size; preserves everything incl. NZCV
    E(0xD10283FFu); // sub sp, sp, #160
    for (int k = 0; k < 8; k++)
        a_stpx(2 * k, 2 * k + 1, 31, 16 * k);
    a_memi(STRXI, 30, 31, 128);
    a_mrs_nzcv(0);
    a_memi(STRXI, 0, 31, 136);
    a_mov(0, 16);
    a_mov(1, 17);
    ldrpos = jp;
    a_ldrlit_x(16, 0);
    a_blr(16);
    a_memi(LDRXI, 0, 31, 136);
    a_msr_nzcv(0);
    a_memi(LDRXI, 30, 31, 128);
    for (int k = 0; k < 8; k++)
        a_ldpx(2 * k, 2 * k + 1, 31, 16 * k);
    E(0x910283FFu); // add sp, sp, #160
    a_ret();
    lit = jp;
    emit_lit64((u64)jit_wlog);
    patch_ldrlit(ldrpos, lit);
}

static void jit_reset(void) {
    wp(0);
    memset(bh, 0, sizeof(Block *) << BH_BITS);
    memset(phead, 0, sizeof phead);
    nblocks = 0;
    npend = 0;
    cache_top = cache_base;
    jp = cache_top;
    jit_gen_tramps();
    cache_top = jp;
    for (u32 i = 0; i < (1u << IBTC_BITS); i++) {
        ibtc[i].key = 0xffffffffu;
        ibtc[i].code = (u64)t_exit;
    } // an empty slot that matches (guest eip 0xffffffff) just takes the miss path
    wp(1);
    sys_icache_invalidate(cache, (u8 *)cache_top - cache);
}

static void jit_init(void) {
    cache =
        mmap(NULL, CACHE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (cache == MAP_FAILED) {
        perror("jit: mmap MAP_JIT");
        exit(1);
    }
    cache_base = (u32 *)cache;
    cache_lim = (u32 *)(cache + CACHE_SIZE - (1 << 20));
    bh = calloc(1u << BH_BITS, sizeof(Block *));
    bpool = calloc(MAXBLK, sizeof(Block));
    pend_pool = calloc(1u << 20, sizeof(Pend));
    ibtc = malloc(sizeof(IbtcEnt) << IBTC_BITS);
    jit_reset();
}

// ---- self-modifying code: translated guest pages are write-protected; a store to one flushes the whole cache ----
static u8 codepg[1 << 18]; // per 16KB host page: 1 = holds translated code and is mprotect()ed read-only
static void jit_unprotect_all(void) {
    for (u32 pg = 0; pg < (1u << 18); pg++)
        if (codepg[pg]) {
            mprotect(G + ((u64)pg << 14), 1 << 14, PROT_READ | PROT_WRITE);
            codepg[pg] = 0;
        }
}
static void jit_prot_range(u32 lo, u32 hi) {
    for (u32 pg = lo >> 14; pg <= (hi - 1) >> 14; pg++)
        if (!codepg[pg]) {
            codepg[pg] = 1;
            mprotect(G + ((u64)pg << 14), 1 << 14, PROT_READ);
        }
}
static void jit_segv(int sig, siginfo_t *si, void *uc) {
    u8 *a = (u8 *)si->si_addr;
    if (a >= G && a < G + (1ull << 32) && codepg[(u32)(a - G) >> 14]) {
        u32 pg = (u32)(a - G) >> 14;
        mprotect(G + ((u64)pg << 14), 1 << 14, PROT_READ | PROT_WRITE);
        codepg[pg] = 0;
        smc_flag = 1;
        st_smc++;
        if (cpu && !smc_hit) {
            smc_jb = cpu->jb;
            cpu->jb = -1;
            smc_hit = 1;
        } // (a second fault in the same run must not overwrite the saved budget)                       // JIT code leaves at the next block boundary; run loop then flushes
        return;
    }
    {
        ucontext_t *u = uc;
        u64 pc = u->uc_mcontext->__ss.__pc;
        Dl_info di;
        const char *nm = dladdr((void *)pc, &di) && di.dli_sname ? di.dli_sname : "?";
        fprintf(stderr, "[jit] fatal signal %d (G=%p) at pc=%p (%s%s) fault addr=%p guest-eip=%08x\n", sig,
                (void *)G, (void *)pc, pc >= (u64)cache_base && pc < (u64)cache_top ? "jit code, " : "", nm,
                si->si_addr, cpu ? cpu->eip : 0);
    }
    signal(sig, SIG_DFL); // genuine crash: let it fault again with the default action
}
static void jit_install_segv(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = jit_segv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}
static void jit_flush(void) {
    st_flush++;
    smc_flag = 0;
    jit_unprotect_all();
    jit_reset();
}

static void patch_b(u32 *site, u32 *target) {
    wp(0);
    *site = 0x14000000u | ((u32)(target - site) & 0x3ffffff);
    wp(1);
    sys_icache_invalidate(site, 4);
}
