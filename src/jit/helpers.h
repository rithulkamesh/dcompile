// helpers.h: C helpers called from generated code, the memory-write log and statistics.
//
// The write log is what JITCHECK uses to compare the JIT against the interpreter byte for byte.

typedef struct {
    u32 addr;
    u8 sz;
    u8 old[16];
} WEnt;
typedef struct {
    WEnt *e;
    int n, cap;
} WLog;
static WLog log_jit, log_ref, *curlog;
static void wlog_note(u32 a, u32 sz) {
    WLog *l = curlog;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->e = realloc(l->e, l->cap * sizeof(WEnt));
    }
    WEnt *w = &l->e[l->n++];
    w->addr = a;
    w->sz = sz > 16 ? 16 : sz;
    memcpy(w->old, P(a), w->sz);
}
static void jit_wlog(u32 addr, u32 sz) {
    curlog = &log_jit;
    wlog_note(addr, sz);
}
static u32 check_len_errs;
static void jit_fb(CPU *c) {
    if (jstat) {
        u32 p = c->eip;
        u8 b;
        for (;;) {
            b = *P(p);
            if (b == 0x66 || b == 0xF2 || b == 0xF3 || b == 0xF0 || b == 0x2E || b == 0x36 || b == 0x3E ||
                b == 0x26 || b == 0x67)
                p++;
            else
                break;
        }
        fbhist[b == 0x0F ? 0x100 | *P(p + 1) : b]++;
        st_fbcount++;
    }
    exec(c);
}
static void jit_fbc(CPU *c, u32 expect) {
    u32 at = c->eip;
    exec(c);
    if (c->eip != expect) {
        Ins I;
        decode(at, &I);
        fprintf(stderr,
                "[jitcheck] length mismatch at %08x: decoded len=%d (next %08x) but interpreter went to %08x "
                "op=%03x\n",
                at, I.len, expect, c->eip, I.op);
        check_len_errs++;
    }
    if (jstat) {
        u8 b = *P(at);
        fbhist[b == 0x0F ? 0x100 | *P(at + 1) : b]++;
        st_fbcount++;
    }
}

static void jit_report(void) {
    if (!jstat)
        return;
    fprintf(stderr,
            "[jit] blocks translated=%llu flushes=%llu static: native=%llu fallback=%llu | dynamic "
            "fallbacks=%llu ibtc-miss=%llu smc=%llu\n",
            (unsigned long long)st_trans, (unsigned long long)st_flush, (unsigned long long)st_nat_static,
            (unsigned long long)st_fb_static, (unsigned long long)st_fbcount,
            (unsigned long long)st_ibtc_miss, (unsigned long long)st_smc);
    for (int k = 0; k < 24; k++) {
        int bi = 0;
        for (int i = 1; i < 0x200; i++)
            if (fbhist[i] > fbhist[bi])
                bi = i;
        if (!fbhist[bi])
            break;
        fprintf(stderr, "  fallback op %s%02x: %llu\n", bi >= 0x100 ? "0f " : "", bi & 0xff,
                (unsigned long long)fbhist[bi]);
        fbhist[bi] = 0;
    }
}
