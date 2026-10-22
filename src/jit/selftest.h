// selftest.h: random single-instruction differential tests (DC_JITSELFTEST=n).
//
// Builds random instructions on synthetic code and compares JIT vs interpreter. Needs no game files; this is the test that guards
// the translator.

enum {
    K_M,
    K_MI8,
    K_MIZ,
    K_I8,
    K_IZ,
    K_NONE,
    K_OPR,
    K_OPRI8,
    K_OPRIZ,
    K_MOFFS,
    K_J8,
    K_J32,
    K_RETI,
    K_PUSHIZ
};
typedef struct {
    u8 pre, op, op2, kind, regmask;
} Tpl;
static Tpl tpls[512];
static int ntpl;
static u32 rs = 0x12345678;
static u32 rnd(void) {
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}
static void tadd(int pre, int op, int op2, int kind, int regmask) {
    tpls[ntpl++] = (Tpl){pre, op, op2, kind, regmask};
}
static void tpl_init(void) {
    if (ntpl)
        return;
    for (int op = 0; op < 0x40; op++)
        if ((op & 7) < 6)
            tadd(0, op, 0, (op & 7) < 4 ? K_M : (op & 7) == 4 ? K_I8 : K_IZ, 0xff);
    for (int op = 0x40; op <= 0x4F; op++)
        tadd(0, op, 0, K_OPR, 0xff);
    for (int op = 0x50; op <= 0x5F; op++)
        tadd(0, op, 0, K_OPR, 0xff);
    tadd(0, 0x68, 0, K_PUSHIZ, 0xff);
    tadd(0, 0x6A, 0, K_I8, 0xff);
    tadd(0, 0x69, 0, K_MIZ, 0xff);
    tadd(0, 0x6B, 0, K_MI8, 0xff);
    tadd(0, 0x80, 0, K_MI8, 0xff);
    tadd(0, 0x81, 0, K_MIZ, 0xff);
    tadd(0, 0x83, 0, K_MI8, 0xff);
    for (int op = 0x84; op <= 0x8B; op++)
        tadd(0, op, 0, K_M, 0xff);
    tadd(0, 0x8D, 0, K_M, 0xff);
    tadd(0, 0x8F, 0, K_M, 0x01);
    for (int op = 0x90; op <= 0x99; op++)
        tadd(0, op, 0, K_NONE, 0xff);
    for (int op = 0xA0; op <= 0xA3; op++)
        tadd(0, op, 0, K_MOFFS, 0xff);
    tadd(0, 0xA8, 0, K_I8, 0xff);
    tadd(0, 0xA9, 0, K_IZ, 0xff);
    for (int op = 0xB0; op <= 0xB7; op++)
        tadd(0, op, 0, K_OPRI8, 0xff);
    for (int op = 0xB8; op <= 0xBF; op++)
        tadd(0, op, 0, K_OPRIZ, 0xff);
    tadd(0, 0xC0, 0, K_MI8, 0xf3);
    tadd(0, 0xC1, 0, K_MI8, 0xf3);
    for (int op = 0xD0; op <= 0xD3; op++)
        tadd(0, op, 0, K_M, 0xf3);
    tadd(0, 0xC6, 0, K_MI8, 0x01);
    tadd(0, 0xC7, 0, K_MIZ, 0x01);
    tadd(0, 0xC9, 0, K_NONE, 0xff);
    tadd(0, 0xF6, 0, K_M, 0x3f);
    tadd(0, 0xF7, 0, K_M, 0xff);
    tadd(0, 0xFE, 0, K_M, 0x03);
    tadd(0, 0xFF, 0, K_M, 0x57);
    tadd(0, 0xF8, 0, K_NONE, 0xff);
    tadd(0, 0xF9, 0, K_NONE, 0xff);
    tadd(0, 0xF5, 0, K_NONE, 0xff);
    tadd(0, 0xFC, 0, K_NONE, 0xff);
    for (int op = 0x70; op <= 0x7F; op++)
        tadd(0, op, 0, K_J8, 0xff);
    for (int op = 0x80; op <= 0x8F; op++)
        tadd(0, 0x0F, op, K_J32, 0xff);
    for (int op = 0x40; op <= 0x4F; op++)
        tadd(0, 0x0F, op, K_M, 0xff);
    for (int op = 0x90; op <= 0x9F; op++)
        tadd(0, 0x0F, op, K_M, 0xff);
    static const u8 m0f[] = {0xAF, 0xB6, 0xB7, 0xBE, 0xBF, 0x1F, 0xBC, 0xBD,
                             0xA3, 0xAB, 0xB3, 0xBB, 0xB0, 0xB1, 0xC0, 0xC1};
    for (unsigned k = 0; k < sizeof m0f; k++)
        tadd(0, 0x0F, m0f[k], K_M, 0xff);
    for (int op = 0xC8; op <= 0xCF; op++)
        tadd(0, 0x0F, op, K_NONE, 0xff);
    tadd(0, 0x0F, 0xBA, K_MI8, 0xf0);
    tadd(0, 0x9D, 0, K_NONE, 0xff);
    tadd(0, 0x9E, 0, K_NONE, 0xff);
    tadd(0, 0x9F, 0, K_NONE, 0xff);
    tadd(0, 0x0F, 0xA4, K_MI8, 0xff);
    tadd(0, 0x0F, 0xAC, K_MI8, 0xff);
    tadd(0, 0x0F, 0xA5, K_M, 0xff);
    tadd(0, 0x0F, 0xAD, K_M, 0xff);
    // SSE: no prefix (ps), 66 (pd / integer), F3 (ss), F2 (sd)
    static const u8 sse_all[] = {0x10, 0x11, 0x14, 0x15, 0x28, 0x29, 0x51, 0x52, 0x53, 0x54,
                                 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E,
                                 0x5F, 0x2E, 0x2F, 0x2A, 0x2C, 0x12, 0x16, 0x13, 0x17};
    for (unsigned k = 0; k < sizeof sse_all; k++)
        for (int p = 0; p < 4; p++)
            tadd(p == 0 ? 0 : p == 1 ? 0x66 : p == 2 ? 0xF3 : 0xF2, 0x0F, sse_all[k], K_M, 0xff);
    tadd(0, 0x0F, 0xC2, K_MI8, 0xff);
    tadd(0x66, 0x0F, 0xC2, K_MI8, 0xff);
    tadd(0xF3, 0x0F, 0xC2, K_MI8, 0xff);
    tadd(0xF2, 0x0F, 0xC2, K_MI8, 0xff);
    tadd(0, 0x0F, 0xC6, K_MI8, 0xff);
    tadd(0x66, 0x0F, 0xC6, K_MI8, 0xff);
    tadd(0x66, 0x0F, 0x70, K_MI8, 0xff);
    tadd(0x66, 0x0F, 0x6E, K_M, 0xff);
    tadd(0x66, 0x0F, 0x7E, K_M, 0xff);
    tadd(0xF3, 0x0F, 0x7E, K_M, 0xff);
    tadd(0x66, 0x0F, 0xD6, K_M, 0xff);
    tadd(0xF3, 0x0F, 0xE6, K_M, 0xff);
    tadd(0x66, 0x0F, 0x71, K_MI8, 0x54);
    tadd(0x66, 0x0F, 0x72, K_MI8, 0x54);
    tadd(0x66, 0x0F, 0x73, K_MI8, 0xcc);
    tadd(0x66, 0x0F, 0x6F, K_M, 0xff);
    tadd(0xF3, 0x0F, 0x6F, K_M, 0xff);
    tadd(0x66, 0x0F, 0x7F, K_M, 0xff);
    tadd(0x66, 0x0F, 0x50, K_M, 0xff);
    tadd(0, 0x0F, 0x50, K_M, 0xff);
    static const u8 sse_i[] = {0x60, 0x61, 0x62, 0x68, 0x69, 0x6A, 0x6C, 0x6D, 0x64, 0x65, 0x66, 0x74,
                               0x75, 0x76, 0xD4, 0xD5, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
                               0xE0, 0xE3, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF8, 0xF9,
                               0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xE5, 0xE4, 0xF4, 0xF5, 0x63, 0x67, 0x6B,
                               0xD1, 0xD2, 0xD3, 0xE1, 0xE2, 0xF1, 0xF2, 0xF3};
    for (unsigned k = 0; k < sizeof sse_i; k++)
        tadd(0x66, 0x0F, sse_i[k], K_M, 0xff);
    tadd(0, 0xC3, 0, K_NONE, 0xff);
    tadd(0, 0xC2, 0, K_RETI, 0xff);
    tadd(0, 0xE8, 0, K_J32, 0xff);
    tadd(0, 0xE9, 0, K_J32, 0xff);
    tadd(0, 0xEB, 0, K_J8, 0xff);
}
static u32 rand_val(void) {
    static const u32 sp[] = {0,          1,          2,          3,          0x7f,
                             0x80,       0xff,       0x100,      0x7fff,     0x8000,
                             0xffff,     0x10000,    0x7fffffff, 0x80000000, 0xffffffff,
                             0xfffffffe, 0xffffff80, 0x55555555, 0xaaaaaaaa};
    u32 r = rnd();
    return r % 3 == 0 ? rnd() : r % 3 == 1 ? sp[rnd() % (sizeof sp / 4)] : (rnd() & 0x1f);
}
static u32 rand_float(void) {
    static const u32 sp[] = {0,          0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
                             0x7fc00000, 0x7fa00000, 0x00000001, 0x007fffff, 0x40490fdb, 0x42f60000,
                             0xc2c80000, 0x3f000000, 0x4b000000, 0x7f7fffff};
    return rnd() & 1 ? sp[rnd() % (sizeof sp / 4)] : rnd();
}
#define DATA 0x50000000u
// builds one instruction at *out; returns its length. regonly forces mod=3.
static int gen_test_insn(u8 *out, int regonly) {
    for (;;) {
        Tpl *t = &tpls[rnd() % ntpl];
        u8 *p = out;
        int k = t->kind, op = t->op;
        if (regonly && (k == K_MOFFS || k == K_J8 || k == K_J32 || k == K_RETI || op == 0xC3 || op == 0xC2 ||
                        op == 0xE8 || op == 0xE9 || op == 0xEB || (op >= 0x70 && op <= 0x7F)))
            continue;
        if (regonly && (op == 0x8D || op == 0xC9 || (op >= 0x50 && op <= 0x5F) || op == 0x68 || op == 0x6A ||
                        op == 0x8F || (op == 0xFF)))
            continue;
        if (t->pre)
            *p++ = t->pre;
        else if (rnd() % 5 == 0 && op != 0x0F && op != 0x68 && !(op >= 0x50 && op <= 0x5F))
            *p++ = 0x66;
        *p++ = op;
        if (t->op2)
            *p++ = t->op2;
        int hasm = k == K_M || k == K_MI8 || k == K_MIZ;
        if (hasm) {
            int mod = regonly ? 3 : (rnd() % 100 < 40 ? 3 : rnd() % 3), rm = rnd() & 7, reg;
            do
                reg = rnd() & 7;
            while (!(t->regmask >> reg & 1));
            if (op == 0x8D) {
                mod = mod == 3 ? 1 : mod;
            }
            if (op == 0x0F && t->op2 >= 0x71 && t->op2 <= 0x73)
                mod = 3;
            if (regonly && (rm == ESP && op != 0xD6 && 1))
                rm = EAX; // keep ESP intact
            if (regonly && reg == ESP)
                reg = ECX;
            *p++ = mod << 6 | reg << 3 | rm;
            if ((op == 0xF6 || op == 0xF7) && reg < 2)
                k = op == 0xF6 ? K_MI8 : K_MIZ;
            if (mod != 3) {
                int sibbase = -1;
                if (rm == 4) {
                    int sc = rnd() & 3, idx = rnd() & 7;
                    sibbase = rnd() & 7;
                    *p++ = sc << 6 | idx << 3 | sibbase;
                }
                if (mod == 1)
                    *p++ = rnd() & 0xff;
                else if (mod == 2) {
                    u32 d = (rnd() % 2) ? (rnd() & 0x7ff) : (u32) - (i32)(rnd() & 0x7ff);
                    memcpy(p, &d, 4);
                    p += 4;
                } else if (rm == 5 || sibbase == 5) {
                    u32 d = DATA + (rnd() & 0xfff0);
                    memcpy(p, &d, 4);
                    p += 4;
                }
            }
        }
        if (k == K_MI8 || k == K_I8 || k == K_OPRI8 || k == K_J8)
            *p++ = (k == K_J8) ? (rnd() & 1 ? 0x10 : 0xf0) : (u8)rand_val();
        if (k == K_MIZ || k == K_IZ || k == K_OPRIZ) {
            u32 v = rand_val();
            memcpy(p, &v, 4);
            p += out[0] == 0x66 ? 2 : 4;
        }
        if (k == K_PUSHIZ) {
            u32 v = rand_val();
            memcpy(p, &v, 4);
            p += 4;
        }
        if (k == K_J32) {
            u32 d = 0x100;
            memcpy(p, &d, 4);
            p += 4;
        }
        if (k == K_RETI) {
            *p++ = 8;
            *p++ = 0;
        }
        if (k == K_MOFFS) {
            u32 a = DATA + (rnd() & 0xff00);
            memcpy(p, &a, 4);
            p += 4;
        }
        return (int)(p - out);
    }
}
static int tst_total, tst_skip;
static u8 tst_bytes[64];
static int tst_len;
static void jit_selftest(long iters) {
    tpl_init();
    if (dbg_env("JITSELFSEED"))
        rs = 0x9e3779b9u * (u32)atol(dbg_env("JITSELFSEED")) | 1;
    jit_init();
    jit_install_segv();
    jcheck = 1;
    jwin = 0;
    jnochain = jnoibtc = 1;
    jstat = 0;
    chk_quiet = 1;
    static u8 seen[0x200];
    int shown = 0;
    for (u32 a = DATA - 0x20000; a < DATA + 0x30000; a += 4)
        wr(a, rnd(), 4);
    for (long i = 0; i < iters; i++) {
        if ((i & 0x7fff) == 0x7fff)
            jit_flush();
        u32 code = 0x10000000u + (u32)(i & 0x7fff) * 0x4000u;
        u8 *q = P(code);
        int n1, n2 = 0, nn;
        for (;;) { // insn1 with a valid effective address, optional reg-only insn2
            n1 = gen_test_insn(q, 0);
            Ins I;
            decode(code, &I);
            if (I.len != n1) {
                fprintf(stderr, "[selftest] LENGTH: generated %d decoded %d:", n1, I.len);
                for (int k = 0; k < n1; k++)
                    fprintf(stderr, " %02x", q[k]);
                fprintf(stderr, "\n");
                chk_bad++;
            }
            if (I.len != n1) {
                n1 = I.len;
            }
            break;
        }
        if (rnd() % 2) {
            int cnt = 1 + (rnd() % 3 == 0 ? rnd() % 5 : 0);
            while (cnt--)
                n2 += gen_test_insn(q + n1 + n2, 1);
        }
        nn = n1 + n2; // tail: decides which flags are live after the instruction(s)
        switch (rnd() % 8) {
        case 0:
        case 1:
            q[nn++] = 0x9C;
            q[nn++] = 0xC3;
            break; // pushfd: everything live
        case 2:
            q[nn++] = rnd() & 1 ? 0x39 : 0x85;
            q[nn++] = 0xC0 | (rnd() & 7) << 3 | (rnd() & 7);
            q[nn++] = 0xC3;
            break; // cmp/test: kills all flags
        case 3:
        case 4:
            q[nn++] = 0x0F;
            q[nn++] = 0x90 | (rnd() & 15);
            q[nn++] = 0xC0 | (rnd() & 7);
            q[nn++] = 0xC3;
            break; // setcc: partial liveness
        case 5:
            q[nn++] = 0x70 | (rnd() & 15);
            q[nn++] = rnd() & 1 ? 0x10 : 0xf0;
            break; // jcc terminator
        case 6: {
            static const u8 t[][2] = {{0x83, 0xD0}, {0x19, 0xDB}, {0x11, 0xC8}, {0x83, 0xDA}};
            int k = rnd() & 3;
            q[nn++] = t[k][0];
            q[nn++] = t[k][1];
            if (t[k][0] == 0x83)
                q[nn++] = rnd() & 3;
            q[nn++] = 0xC3;
            break;
        } // adc/sbb: CF live
        default:
            q[nn++] = 0xC3;
            break; // ret: flags dead
        }
        Ins I1;
        decode(code, &I1);
        CPU c, c0;
        memset(&c, 0, sizeof c);
        c.mxcsr = 0x1f80;
        c.fcw = 0x37f;
        for (int tries = 0; tries < 50; tries++) {
            for (int r = 0; r < 8; r++)
                c.r[r] = rnd() % 2 ? DATA + (rnd() & 0xfff0) : rnd() % 3 ? (rnd() & 0xff) : rand_val();
            c.r[ESP] = DATA + 0x8000 + (rnd() & 0xff) * 4;
            if (rnd() % 4 == 0)
                c.r[EBP] = DATA + (rnd() & 0xfff0);
            u32 ea = (I1.base >= 0 ? c.r[I1.base] : 0) + (I1.idx >= 0 ? c.r[I1.idx] << I1.sc : 0) + I1.disp;
            if (I1.isreg || !I1.hasm || (ea >= DATA - 0x1000 && ea < DATA + 0x20000))
                break;
        }
        c.cf = rnd() & 1;
        c.pf = rnd() & 1;
        c.zf = rnd() & 1;
        c.sf = rnd() & 1;
        c.of = rnd() & 1;
        c.df = 0;
        for (int x = 0; x < 8; x++)
            for (int l = 0; l < 4; l++)
                c.xmm[x].d[l] = rand_float();
        c.eip = code;
        c0 = c;
        tst_len = nn;
        memcpy(tst_bytes, q, nn);
        tst_total++;
        if (DBG_FLAG("JITSELFVERBOSE")) {
            fprintf(stderr, "[t%ld]", i);
            for (int k = 0; k < nn; k++)
                fprintf(stderr, " %02x", q[k]);
            fprintf(stderr, "  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
                    c.r[0], c.r[1], c.r[2], c.r[3], c.r[ESP], c.r[5], c.r[6], c.r[7]);
        }
        if (dbg_env("JITSELFONLY") && i != atol(dbg_env("JITSELFONLY")))
            continue; // replay one test (the generator state is independent of execution)
        u64 bad0 = chk_bad;
        chk_msg[0] = 0;
        if (_setjmp(jb)) {
            tst_skip++;
            wlog_on_ = 0;
            cpu = NULL;
            continue;
        }
        Block *b = jit_translate(code);
        jit_check_block(&c, b);
        if (smc_flag)
            jit_flush(); // a random store hit translated code: start over
        if (chk_bad != bad0 && !seen[I1.op & 0x1ff]++ && shown++ < 80) {
            fprintf(stderr, "[selftest] MISMATCH #%ld %s\n   insn bytes:", i, chk_msg);
            for (int k = 0; k < nn; k++)
                fprintf(stderr, " %02x", tst_bytes[k]);
            fprintf(stderr,
                    "\n   regs before: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x "
                    "edi=%08x flags cf%d pf%d zf%d sf%d of%d\n",
                    c0.r[0], c0.r[1], c0.r[2], c0.r[3], c0.r[4], c0.r[5], c0.r[6], c0.r[7], c0.cf, c0.pf,
                    c0.zf, c0.sf, c0.of);
        }
    }
    fprintf(stderr,
            "[selftest] %d tests, %d skipped (guest fault), %llu mismatches (translated insns: %llu native, "
            "%llu via interpreter)\n",
            tst_total, tst_skip, (unsigned long long)chk_bad, (unsigned long long)st_nat_static,
            (unsigned long long)st_fb_static);
}
static void jit_selftest_main(void) {
    if (dbg_env("JITSELFTEST")) {
        jit_selftest(atol(dbg_env("JITSELFTEST")));
        exit(chk_bad ? 1 : 0);
    }
}

static void jit_check_summary(void) {
    fprintf(stderr, "[jitcheck] %llu blocks compared, %llu mismatches, %u length errors\n",
            (unsigned long long)chk_blocks, (unsigned long long)chk_bad, check_len_errs);
}
