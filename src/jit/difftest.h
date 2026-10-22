// difftest.h: differential checking (DC_JITCHECK).
//
// Runs each block in the JIT and the same number of instructions in the interpreter on a copy of the CPU, then compares
// registers, live flags and every byte written. The run continues from the reference (interpreter) state.

static u64 chk_blocks, chk_bad;
static int chk_quiet;
static char chk_msg[256];
static void chk_report(const char *what, Block *b, u32 a, u32 x, u32 y) {
    if (chk_quiet) {
        if (!chk_msg[0])
            snprintf(chk_msg, sizeof chk_msg, "%s %08x: jit=%08x ref=%08x", what, a, x, y);
        chk_bad++;
        return;
    }
    if (chk_bad++ < 40)
        fprintf(stderr, "[jitcheck] MISMATCH block %08x..%08x (%d insns): %s %08x: jit=%08x ref=%08x\n",
                b->guest, b->gend, b->nins, what, a, x, y);
}
static long jwin = 0;
static int jcheck_pending, jcheck_nochain;
static u64
    jcheck_at; // JITCHECK=2: check windows of ~JITWIN instructions with chaining enabled (JITCHECK=1: one block at a time)
static long jit_check_block(CPU *c, Block *b) {
    CPU c0 = *c, cr;
    cpu = c;
    if (!(b->livein & F_C))
        c->cf ^= 1; // poison the flags this block claims not to need: it must not depend on them
    if (!(b->livein & F_Z))
        c->zf ^= 1;
    if (!(b->livein & F_S))
        c->sf ^= 1;
    if (!(b->livein & F_O))
        c->of ^= 1;
    if (!(b->livein & F_P))
        c->pf ^= 1;
    wlog_on_ = 1;
    curlog = &log_jit;
    log_jit.n = 0;
    long budget = jwin ? jwin : (1ll << 40);
    smc_hit = 0;
    c->jb = budget;
    ((void (*)(CPU *, void *))t_enter)(c, b->code);
    long done = budget - (smc_hit ? smc_jb : c->jb);
    smc_hit = 0;
    wlog_on_ = 0;
    if (done > 100000 || done < 0) {
        fprintf(stderr, "[jitcheck] block %08x: implausible instruction count %ld (jb=%ld, exit eip=%08x)\n",
                b->guest, done, (long)c->jb, c->eip);
        exit(2);
    }
    // JIT result is in *c and memory; remember what it left in the written locations, then roll memory back
    int nj = log_jit.n;
    WEnt *J = malloc(sizeof(WEnt) * (nj + 1));
    for (int i = 0; i < nj; i++) {
        J[i] = log_jit.e[i];
    }
    u8(*jpost)[16] = malloc(16 * (nj + 1));
    for (int i = 0; i < nj; i++)
        memcpy(jpost[i], G + J[i].addr, J[i].sz);
    for (int i = nj - 1; i >= 0; i--)
        memcpy(G + J[i].addr, J[i].old, J[i].sz);
    CPU jc = *c;
    cr = c0;
    cr.jb = 0;
    cpu = &cr;
    wlog_on_ = 1;
    curlog = &log_ref;
    log_ref.n = 0;
    static u32 path[4096];
    for (long k = 0; k < done; k++) {
        if (k < 4096)
            path[k] = cr.eip;
        exec(&cr);
    }
    wlog_on_ = 0;
    cpu = c;
    chk_blocks++;
    u64 bad0 = chk_bad;
    // registers / eip / df / fpu / sse
    for (int i = 0; i < 8; i++)
        if (jc.r[i] != cr.r[i])
            chk_report("reg", b, i, jc.r[i], cr.r[i]);
    if (jc.eip != cr.eip)
        chk_report("eip", b, 0, jc.eip, cr.eip);
    if (jc.df != cr.df)
        chk_report("df", b, 0, jc.df, cr.df);
    u8 lo = jc.flo;
    if ((lo & F_C) && jc.cf != cr.cf)
        chk_report("cf", b, 0, jc.cf, cr.cf);
    if ((lo & F_Z) && jc.zf != cr.zf)
        chk_report("zf", b, 0, jc.zf, cr.zf);
    if ((lo & F_S) && jc.sf != cr.sf)
        chk_report("sf", b, 0, jc.sf, cr.sf);
    if ((lo & F_O) && jc.of != cr.of)
        chk_report("of", b, 0, jc.of, cr.of);
    if ((lo & F_P) && jc.pf != cr.pf)
        chk_report("pf", b, 0, jc.pf, cr.pf);
    if (memcmp(jc.st, cr.st, sizeof cr.st) || jc.ftop != cr.ftop || jc.fsw != cr.fsw || jc.fcw != cr.fcw ||
        jc.mxcsr != cr.mxcsr)
        chk_report("x87/mxcsr", b, 0, 0, 0);
    if (memcmp(jc.xmm, cr.xmm, sizeof cr.xmm))
        chk_report("xmm", b, 0, 0, 0);
    // memory: every byte either engine wrote (64-bit addresses: a store at 0xfffffffe spills into the slack above 4GB)
    for (int pass = 0; pass < 2; pass++) {
        WEnt *L = pass ? log_ref.e : J;
        int nl = pass ? log_ref.n : nj;
        for (int i = 0; i < nl; i++)
            for (u32 k = 0; k < L[i].sz; k++) {
                u64 ad = (u64)L[i].addr + k;
                u32 refv = G[ad], jv = 0;
                int inj = 0;
                for (int q = nj - 1; q >= 0; q--)
                    if (ad >= J[q].addr && ad < (u64)J[q].addr + J[q].sz) {
                        jv = jpost[q][ad - J[q].addr];
                        inj = 1;
                        break;
                    }
                if (!inj) {
                    for (int q = 0; q < log_ref.n; q++)
                        if (ad >= log_ref.e[q].addr && ad < (u64)log_ref.e[q].addr + log_ref.e[q].sz) {
                            jv = log_ref.e[q].old[ad - log_ref.e[q].addr];
                            break;
                        }
                }
                if (jv != refv) {
                    chk_report(inj ? "mem(jit wrote)" : "mem(jit MISSED store)", b, (u32)ad, jv, refv);
                    goto memdone;
                }
            }
    }
memdone:
    if (chk_bad != bad0 && chk_bad < 60 && !chk_quiet) {
        fprintf(stderr, "  ref path (%ld insns):", done);
        for (long k = 0; k < done && k < 64; k++)
            fprintf(stderr, " %x", path[k]);
        fprintf(stderr, "\n  jit final eip %08x ref %08x\n", jc.eip, cr.eip);
    }
    *c = cr;
    c->jb = 0; // continue from the reference state
    free(J);
    free(jpost);
    return done;
}
