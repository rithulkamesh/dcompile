// codegen.h: per-instruction code generation.
//
// Translates one decoded x86 instruction to ARM64: moves, ALU, stack ops, control flow, string ops and the SSE/SSE2 subset.
// Every generator returns 1 when it handled the instruction natively and emitted nothing otherwise (the caller then falls back
// to calling exec() for exactly that instruction).

static void gen_fb(Ins *I) {
    flush_flags((I->live & ~I->fw) | I->fr);
    if (need_store)
        sync_store();
    a_movi(0, I->addr);
    a_memi(STRWI, 0, XC, OFF(eip));
    a_movx_r(0, XC);
    if (jcheck) {
        a_movi(1, I->addr + I->len);
        a_bl(t_fbc);
    } else
        a_bl(t_fb);
    fstate = FS_STRUCT;
    need_load = 1;
    st_fb_static++;
}

static void set_flo(u8 m) {
    if (!jcheck)
        return;
    a_movi(14, m);
    a_memi(STRWI, 14, XC, OFF(flo));
}
static void gen_fb_noret(
    Ins *
        I) { // inline interpreter call on a path that cannot continue (the instruction dies); keeps the translator state
    int sf = fstate, sc = cinv, nl = need_load, ns = need_store;
    gen_fb(I);
    fstate = sf;
    cinv = sc;
    need_load = nl;
    need_store = ns;
    st_fb_static--;
}
static void exit_to(u32 tgt) {
    Block *b = (tgt < THUNK_BASE && !jnochain) ? blk_find(tgt) : NULL;
    if (b)
        a_b(b->code);
    else {
        exits[nexits].site = jp;
        exits[nexits++].tgt = tgt;
        E(0x14000000u);
    }
}

// condition (cc) -> flag to test through memory for parity conditions; returns 1 if it was a parity condition and set up w0 = pf
static void gen_jcc(Ins *I) {
    int cc = I->op & 15;
    u32 fall = I->addr + I->len;
    if (need_load)
        sync_load();
    flush_flags(I->live);
    int pfc = (cc >> 1) == 5;
    if (pfc) { // jp / jnp: PF lives in the struct
        a_memi(LDRWI, 0, XC, OFF(pf));
        if (cc & 1)
            a_cbnz(0, 2);
        else
            a_cbz(0, 2); // skip the taken exit unless the condition holds
    } else {
        loadflags();
        int ac = arm_cond(cc);
        a_bcond(ac ^ 1, 2);
    }
    exit_to(I->tgt);
    exit_to(fall);
}

// ---- SSE / SSE2 (guest xmm registers stay in the CPU struct; each op is load, NEON op, store) ----
#define XM(i) ((int)offsetof(CPU, xmm) + 16 * (i))
static void vld_mem(int n, int v, int ea) {
    a_mem(n == 4 ? LDRS : n == 8 ? LDRD : LDRQ, v, XG, ea);
}
static void vst_mem(int n, int v, int ea) {
    if (jcheck) {
        a_mov(16, ea);
        a_movz(17, n, 0);
        a_bl(t_wlog);
    }
    a_mem(n == 4 ? STRS : n == 8 ? STRD : STRQ, v, XG, ea);
}
static void vld_x(int n, int v, int x) {
    a_memqi(n == 4 ? LDRSI : n == 8 ? LDRDI : LDRQI, v, XC, XM(x));
}
static void vst_x(int n, int v, int x) {
    a_memqi(n == 4 ? STRSI : n == 8 ? STRDI : STRQI, v, XC, XM(x));
}
static void vsrc(Ins *I, int n, int v) { // r/m operand of n bytes -> vector register v
    if (I->isreg)
        vld_x(n, v, I->rm);
    else
        vld_mem(n, v, ea_of(I, 8, 9));
}

static const struct {
    u16 op;
    u32 enc;
} SSEI[] = {
    // 66-prefixed integer ops with a direct NEON equivalent: dest = f(dest, src)
    {0xFC, 0x4E208400}, {0xFD, 0x4E608400}, {0xFE, 0x4EA08400}, {0xD4, 0x4EE08400}, {0xF8, 0x6E208400},
    {0xF9, 0x6E608400}, {0xFA, 0x6EA08400}, {0xFB, 0x6EE08400}, {0x74, 0x6E208C00}, {0x75, 0x6E608C00},
    {0x76, 0x6EA08C00}, {0x64, 0x4E203400}, {0x65, 0x4E603400}, {0x66, 0x4EA03400}, {0xD5, 0x4E609C00},
    {0xDA, 0x6E206C00}, {0xDE, 0x6E206400}, {0xEA, 0x4E606C00}, {0xEE, 0x4E606400}, {0xD8, 0x6E202C00},
    {0xD9, 0x6E602C00}, {0xDC, 0x6E200C00}, {0xDD, 0x6E600C00}, {0xE8, 0x4E202C00}, {0xE9, 0x4E602C00},
    {0xEC, 0x4E200C00}, {0xED, 0x4E600C00}, {0xE0, 0x6E201400}, {0xE3, 0x6E601400}, {0x60, 0x4E003800},
    {0x61, 0x4E403800}, {0x62, 0x4E803800}, {0x6C, 0x4EC03800}, {0x68, 0x4E007800}, {0x69, 0x4E407800},
    {0x6A, 0x4E807800}, {0x6D, 0x4EC07800}, {0xDB, 0x4E201C00}, {0xEB, 0x4EA01C00}, {0xEF, 0x6E201C00},
};

// returns 1 if handled natively (nothing is emitted when returning 0)
static int gen_sse(Ins *I) {
    int op = I->op & 0xff, pfx = I->osz == 2 ? 1 : I->rep == 1 ? 2 : I->rep == 2 ? 3 : 0;
    int n = pfx == 2 ? 4 : pfx == 3 ? 8 : 16, dst = I->reg;
    int lanes_d = pfx == 1 || pfx == 3; // double precision forms
    switch (op) {
    case 0x10:
    case 0x28:
    case 0x6F:
    case 0xF0: // loads that replace the whole register
        if (op == 0x10 && pfx >= 2) {
            if (I->isreg) {
                vld_x(n, 0, I->rm);
                vst_x(n, 0, dst);
            } else {
                vsrc(I, n, 0);
                vst_x(16, 0, dst);
            } // memory form zeroes the rest (ldr s/d clears the upper bits)
            return 1;
        }
        vsrc(I, 16, 0);
        vst_x(16, 0, dst);
        return 1;
    case 0x11:
    case 0x29:
    case 0x7F: {
        int sn = (op == 0x11 && pfx >= 2) ? n : 16;
        vld_x(sn, 0, dst);
        if (I->isreg)
            vst_x(sn, 0, I->rm);
        else
            vst_mem(sn, 0, ea_of(I, 8, 9));
        return 1;
    }
    case 0x13:
    case 0xD6: // movlps / movq store (8 bytes)
        if (op == 0xD6 && pfx != 1)
            return 0;
        if (I->isreg) {
            if (op == 0x13)
                return 0;
            vld_x(8, 0, dst);
            vst_x(16, 0, I->rm);
        } else {
            vld_x(8, 0, dst);
            vst_mem(8, 0, ea_of(I, 8, 9));
        }
        return 1;
    case 0x12:
    case 0x16: // movlps/movhps/movhlps/movlhps (pfx 0/1 only)
        if (pfx > 1)
            return 0;
        if (I->isreg) {
            vld_x(16, 0, dst);
            vld_x(16, 1, I->rm);
            a_ins_d(0, op == 0x12 ? 0 : 1, 1, op == 0x12 ? 1 : 0);
            vst_x(16, 0, dst);
        } else {
            vld_x(16, 0, dst);
            vld_mem(8, 1, ea_of(I, 8, 9));
            a_ins_d(0, op == 0x12 ? 0 : 1, 1, 0);
            vst_x(16, 0, dst);
        }
        return 1;
    case 0x14:
    case 0x15: {
        if (pfx > 1)
            return 0;
        vld_x(16, 0, dst);
        vsrc(I, 16, 1);
        a_v3((pfx ? (op == 0x14 ? 0x4EC03800 : 0x4EC07800) : (op == 0x14 ? 0x4E803800 : 0x4E807800)), 2, 0,
             1);
        vst_x(16, 2, dst);
        return 1;
    }
    case 0x17:
        if (pfx > 1 || I->isreg)
            return 0;
        vld_x(16, 0, dst);
        a_ins_d(1, 0, 0, 1);
        vst_mem(8, 1, ea_of(I, 8, 9));
        return 1;
    case 0xE6:
        if (pfx != 2)
            return 0;
        vsrc(I, 8, 1);
        a_v2(0x0F20A400, 0, 1);
        a_v2(0x4E61D800, 0, 0);
        vst_x(16, 0, dst);
        return 1;
    case 0x71:
    case 0x72:
    case 0x73: {
        if (pfx != 1 || !I->isreg)
            return 0;
        int k = I->reg, imm = I->imm & 0xff, x = I->rm;
        u32 ushr, sshr, shl;
        int bits;
        if (op == 0x71) {
            bits = 16;
            ushr = 0x6F000400 | (32 - imm) << 16;
            sshr = 0x4F000400 | (32 - (imm > 15 ? 15 : imm)) << 16;
            shl = 0x4F005400 | (16 + imm) << 16;
        } else if (op == 0x72) {
            bits = 32;
            ushr = 0x6F000400 | (64 - imm) << 16;
            sshr = 0x4F000400 | (64 - (imm > 31 ? 31 : imm)) << 16;
            shl = 0x4F005400 | (32 + imm) << 16;
        } else {
            bits = 64;
            ushr = 0x6F000400 | (128 - imm) << 16;
            sshr = 0;
            shl = 0x4F005400 | (64 + imm) << 16;
        }
        if (op == 0x73 && (k == 3 || k == 7)) { // psrldq / pslldq: whole-register byte shifts
            int sh = imm > 16 ? 16 : imm;
            if (sh == 0)
                return 1;
            if (sh == 16) {
                a_stpx(XZR, XZR, XC, XM(x));
                return 1;
            }
            vld_x(16, 0, x);
            a_v2(0x4F00E400, 1, 0); // movi v1.2d, #0 (rn field unused: encoded as constant)
            if (k == 3)
                a_ext(2, 0, 1, sh);
            else
                a_ext(2, 1, 0, 16 - sh);
            vst_x(16, 2, x);
            return 1;
        }
        if (k != 2 && k != 4 && k != 6)
            return 0;
        if (op == 0x73 && k == 4)
            return 0;
        if (imm == 0)
            return 1; // shift by 0: nothing to do
        if (k != 4 && imm >= bits) {
            a_stpx(XZR, XZR, XC, XM(x));
            return 1;
        }
        vld_x(16, 0, x);
        a_v2(k == 2 ? ushr : k == 4 ? sshr : shl, 0, 0);
        vst_x(16, 0, x);
        return 1;
    }
    case 0x2A:
        if (pfx < 2)
            return 0;
        if (I->isreg) {
            a_v2(pfx == 2 ? 0x1E220000 : 0x1E620000, 0, GR(I->rm));
        } else {
            int ea = ea_of(I, 8, 9);
            ld(4, 0, ea);
            a_v2(pfx == 2 ? 0x1E220000 : 0x1E620000, 0, 0);
        }
        vst_x(n, 0, dst);
        return 1;
    case 0x2C:
        if (pfx < 2)
            return 0;
        vsrc(I, n, 1);
        E((pfx == 2 ? 0x1E380000u : 0x1E780000u) | 1 << 5 | GR(dst)); // fcvtzs wDst, s1/d1
        return 1;
    case 0x2E:
    case 0x2F: { // (u)comiss / (u)comisd: sets all five flags
        if (pfx > 1)
            return 0;
        u8 live = I->live;
        vld_x(pfx ? 8 : 4, 0, dst);
        vsrc(I, pfx ? 8 : 4, 1);
        E((pfx ? 0x1E602000u : 0x1E202000u) | 1 << 16 | 0 << 5); // fcmp s0, s1 / d0, d1
        // CF = lt, ZF = eq | vs, PF = vs, OF = SF = 0   (NaN: lt and vs both true; eq false)
        if (live & F_C) {
            a_cset(10, CLT);
            a_memi(STRWI, 10, XC, OFF(cf));
        }
        if (live & F_Z) {
            a_cset(11, CEQ);
            a_cset(12, CVS);
            a_rrr(ORR, 11, 11, 12, 0, 0);
            a_memi(STRWI, 11, XC, OFF(zf));
        }
        if (live & F_P) {
            a_cset(12, CVS);
            a_memi(STRWI, 12, XC, OFF(pf));
        }
        if (live & F_S)
            a_memi(STRWI, XZR, XC, OFF(sf));
        if (live & F_O)
            a_memi(STRWI, XZR, XC, OFF(of));
        fstate = FS_STRUCT;
        return 1;
    }
    case 0x50: { // movmskps / movmskpd (register source only)
        if (!I->isreg || pfx > 1)
            return 0;
        vld_x(16, 0, I->rm);
        if (pfx == 0) {
            a_v2(0x6F210400, 1, 0); // ushr v1.4s, v0.4s, #31
            a_umov_s(0, 1, 0);
            for (int k = 1; k < 4; k++) {
                a_umov_s(2, 1, k);
                a_rrr(ORR, 0, 0, 2, LSL_, k);
            }
        } else {
            a_v2(0x6F410400, 1, 0); // ushr v1.2d, v0.2d, #63
            a_umov_s(0, 1, 0);
            a_umov_s(2, 1, 2);
            a_rrr(ORR, 0, 0, 2, LSL_, 1);
        }
        a_mov(GR(dst), 0);
        return 1;
    }
    case 0x51:
    case 0x58:
    case 0x59:
    case 0x5C:
    case 0x5E:
    case 0x5D:
    case 0x5F:
    case 0x52:
    case 0x53: {
        if (op == 0x52 || op == 0x53) {
            if (pfx > 1)
                return 0;
        }
        vld_x(16, 0, dst);
        vsrc(I, n, 1);
        int scalar = pfx >= 2, dbl = lanes_d;
        if (op == 0x5D || op == 0x5F) {
            u32 gt = dbl ? 0x6EE0E400 : 0x6EA0E400;
            if (op == 0x5D)
                a_v3(gt, 2, 1, 0);
            else
                a_v3(gt, 2, 0, 1);
            a_v3(0x6E601C00, 2, 0, 1); // bsl v2, v0, v1
            if (scalar)
                vst_x(n, 2, dst);
            else
                vst_x(16, 2, dst);
            return 1;
        }
        u32 base;
        if (!scalar) {
            switch (op) {
            case 0x58:
                base = dbl ? 0x4E60D400 : 0x4E20D400;
                break;
            case 0x5C:
                base = dbl ? 0x4EE0D400 : 0x4EA0D400;
                break;
            case 0x59:
                base = dbl ? 0x6E60DC00 : 0x6E20DC00;
                break;
            case 0x5E:
                base = dbl ? 0x6E60FC00 : 0x6E20FC00;
                break;
            default:
                base = 0;
                break;
            }
            if (op == 0x51) {
                a_v2(dbl ? 0x6EE1F800 : 0x6EA1F800, 0, 1);
            } else if (op == 0x52) {
                a_v2(0x6EA1F800, 1, 1);
                a_v2(0x4F03F600, 2, 0);
                a_v3(0x6E20FC00, 0, 2, 1);
            } else if (op == 0x53) {
                a_v2(0x4F03F600, 2, 0);
                a_v3(0x6E20FC00, 0, 2, 1);
            } else
                a_v3(base, 0, 0, 1);
            vst_x(16, 0, dst);
        } else {
            switch (op) {
            case 0x58:
                base = dbl ? 0x1E602800 : 0x1E202800;
                break;
            case 0x5C:
                base = dbl ? 0x1E603800 : 0x1E203800;
                break;
            case 0x59:
                base = dbl ? 0x1E600800 : 0x1E200800;
                break;
            case 0x5E:
                base = dbl ? 0x1E601800 : 0x1E201800;
                break;
            default:
                base = 0;
                break;
            }
            if (op == 0x51)
                a_v2(dbl ? 0x1E61C000 : 0x1E21C000, 0, 1);
            else
                a_v3(base, 0, 0, 1);
            vst_x(n, 0, dst);
        }
        return 1;
    }
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57: {
        if (op == 0x57 && I->isreg && I->rm == dst) {
            a_stpx(XZR, XZR, XC, XM(dst));
            return 1;
        }
        vld_x(16, 0, dst);
        vsrc(I, 16, 1);
        if (op == 0x54)
            a_v3(0x4E201C00, 0, 0, 1);
        else if (op == 0x55)
            a_v3(0x4E601C00, 0, 1, 0);
        else if (op == 0x56)
            a_v3(0x4EA01C00, 0, 0, 1);
        else
            a_v3(0x6E201C00, 0, 0, 1);
        vst_x(16, 0, dst);
        return 1;
    }
    case 0x5A:
        if (pfx == 0) {
            vsrc(I, 8, 1);
            a_v2(0x0E617800, 0, 1);
            vst_x(16, 0, dst);
            return 1;
        } // cvtps2pd
        if (pfx == 1) {
            vsrc(I, 16, 1);
            a_v2(0x0E616800, 0, 1);
            vst_x(16, 0, dst);
            return 1;
        } // cvtpd2ps (upper cleared)
        if (pfx == 2) {
            vsrc(I, 4, 1);
            a_v2(0x1E22C000, 0, 1);
            vst_x(8, 0, dst);
            return 1;
        } // cvtss2sd
        vsrc(I, 8, 1);
        a_v2(0x1E624000, 0, 1);
        vst_x(4, 0, dst);
        return 1; // cvtsd2ss
    case 0x5B:
        if (pfx == 0) {
            vsrc(I, 16, 1);
            a_v2(0x4E21D800, 0, 1);
            vst_x(16, 0, dst);
            return 1;
        }
        if (pfx == 2) {
            vsrc(I, 16, 1);
            a_v2(0x4EA1B800, 0, 1);
            vst_x(16, 0, dst);
            return 1;
        }
        return 0;
    case 0xC2: {
        int imm = *P(I->addr + I->len - 1) & 7;
        vld_x(16, 0, dst);
        vsrc(I, n, 1);
        u32 eq = lanes_d ? 0x4E60E400 : 0x4E20E400, ge = lanes_d ? 0x6E60E400 : 0x6E20E400,
            gt = lanes_d ? 0x6EE0E400 : 0x6EA0E400;
        switch (imm & 3) {
        case 0:
            a_v3(eq, 2, 0, 1);
            break;
        case 1:
            a_v3(gt, 2, 1, 0);
            break;
        case 2:
            a_v3(ge, 2, 1, 0);
            break;
        default:
            a_v3(eq, 2, 0, 0);
            a_v3(eq, 3, 1, 1);
            a_v3(0x4E201C00, 2, 2, 3);
            break; // ordered
        }
        if ((imm & 3) == 3 ? !(imm & 4) : (imm & 4))
            a_v2(0x6E205800, 2, 2); // unord(3) / neq,nlt,nle(4-6): invert
        if (pfx >= 2)
            vst_x(n, 2, dst);
        else
            vst_x(16, 2, dst);
        return 1;
    }
    case 0xC6: {
        if (pfx > 1)
            return 0;
        int imm = *P(I->addr + I->len - 1);
        vld_x(16, 0, dst);
        vsrc(I, 16, 1);
        if (pfx == 0) {
            a_ins_s(2, 0, 0, imm & 3);
            a_ins_s(2, 1, 0, (imm >> 2) & 3);
            a_ins_s(2, 2, 1, (imm >> 4) & 3);
            a_ins_s(2, 3, 1, (imm >> 6) & 3);
        } else {
            a_ins_d(2, 0, 0, imm & 1);
            a_ins_d(2, 1, 1, (imm >> 1) & 1);
        }
        vst_x(16, 2, dst);
        return 1;
    }
    case 0x70: {
        if (pfx != 1)
            return 0;
        int imm = *P(I->addr + I->len - 1);
        vsrc(I, 16, 1);
        if (((imm >> 2) & 3) == (imm & 3) && ((imm >> 4) & 3) == (imm & 3) && ((imm >> 6) & 3) == (imm & 3))
            a_dup_s(2, 1, imm & 3);
        else
            for (int k = 0; k < 4; k++)
                a_ins_s(2, k, 1, (imm >> (2 * k)) & 3);
        vst_x(16, 2, dst);
        return 1;
    }
    case 0x6E:
        if (pfx != 1 && pfx != 0)
            return 0;
        if (I->isreg)
            a_v2(0x1E270000, 0, GR(I->rm));
        else {
            ld(4, 0, ea_of(I, 8, 9));
            a_v2(0x1E270000, 0, 0);
        }
        vst_x(16, 0, dst);
        return 1;
    case 0x7E:
        if (pfx == 2) {
            vsrc(I, 8, 0);
            vst_x(16, 0, dst);
            return 1;
        }
        a_memi(LDRWI, 0, XC, XM(dst));
        if (I->isreg)
            a_mov(GR(I->rm), 0);
        else
            st(4, 0, ea_of(I, 8, 9));
        return 1;
    }
    if (pfx == 1) { // integer SSE2 binary ops
        for (unsigned k = 0; k < sizeof SSEI / sizeof SSEI[0]; k++)
            if (SSEI[k].op == op) {
                if (op == 0xEF && I->isreg && I->rm == dst) {
                    a_stpx(XZR, XZR, XC, XM(dst));
                    return 1;
                }
                vld_x(16, 0, dst);
                vsrc(I, 16, 1);
                a_v3(SSEI[k].enc, 0, 0, 1);
                vst_x(16, 0, dst);
                return 1;
            }
        if (op == 0xDF) {
            vld_x(16, 0, dst);
            vsrc(I, 16, 1);
            a_v3(0x4E601C00, 0, 1, 0);
            vst_x(16, 0, dst);
            return 1;
        }
    }
    return 0;
}

static int gen_nat(Ins *I);

static void gen_ins(Ins *I) {
    if (I->cls == CL_NAT) {
        if (need_load)
            sync_load();
        if (gen_nat(I)) {
            need_store = 1;
            st_nat_static++;
            return;
        } // gen_nat bails out before emitting anything
    }
    gen_fb(I);
}

// returns 1 if the instruction was translated natively
static int gen_nat(Ins *I) {
    u8 live = I->live;
    int osz = I->osz, sz;
    u16 op = I->op;
    Opd d, s;
    if (op < 0x40 && (op & 7) < 6) {
        int aop = op >> 3, form = op & 7;
        sz = (form & 1) ? osz : 1;
        if (!alu_ok(aop, sz))
            return 0;
        if (form < 2) {
            d = opd_rm(I);
            s = opd_reg(I->reg);
            alu_gen(aop, sz, d, s, 0, live);
        } else if (form < 4) {
            s = opd_rm(I);
            d = opd_reg(I->reg);
            alu_gen(aop, sz, d, s, 0, live);
        } else {
            d = opd_reg(EAX);
            s = opd_imm();
            alu_gen(aop, sz, d, s, I->imm, live);
        }
        return 1;
    }
    if (op >= 0x40 && op <= 0x4F) {
        if (!incdec_ok(osz, live))
            return 0;
        incdec_gen(op >= 0x48, osz, opd_reg(op & 7), live);
        return 1;
    }
    if (op >= 0x50 && op <= 0x57) {
        int r = op & 7;
        if (r == ESP) {
            a_mov(0, GR(ESP));
            a_imm(SUBI, GR(ESP), GR(ESP), 4);
            st(4, 0, GR(ESP));
        } else {
            a_imm(SUBI, GR(ESP), GR(ESP), 4);
            st(4, GR(r), GR(ESP));
        }
        return 1;
    }
    if (op >= 0x58 && op <= 0x5F) {
        int r = op & 7;
        a_mem(LDRW, GR(r), XG, GR(ESP));
        if (r != ESP)
            a_imm(ADDI, GR(ESP), GR(ESP), 4);
        return 1;
    }
    if (op == 0x68 || op == 0x6A) {
        a_movi(0, I->imm);
        a_imm(SUBI, GR(ESP), GR(ESP), 4);
        st(4, 0, GR(ESP));
        return 1;
    }
    if (op >= 0x91 && op <= 0x97) {
        if (osz != 4)
            return 0;
        a_mov(0, GR(op & 7));
        a_mov(GR(op & 7), GR(EAX));
        a_mov(GR(EAX), 0);
        return 1;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        int r = op & 7;
        a_movi(0, I->imm);
        reg_write(r, 1, 0);
        return 1;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        int r = op & 7;
        if (osz == 4)
            a_movi(GR(r), I->imm);
        else {
            a_movi(0, I->imm);
            reg_write(r, 2, 0);
        }
        return 1;
    }
    switch (op) {
    case 0x69:
    case 0x6B: {
        if (osz != 4 || (live & (F_C | F_O)))
            return 0;
        s = opd_rm(I);
        int rs = rdv(s, 4, 0, 0);
        a_movi(1, I->imm);
        a_mul(GR(I->reg), rs, 1);
        return 1;
    }
    case 0x80:
    case 0x81:
    case 0x83: {
        sz = op == 0x80 ? 1 : osz;
        if (!alu_ok(I->reg, sz))
            return 0;
        d = opd_rm(I);
        alu_gen(I->reg, sz, d, opd_imm(), I->imm, live);
        return 1;
    }
    case 0x84:
    case 0x85: {
        sz = op & 1 ? osz : 1;
        d = opd_rm(I);
        alu_gen(4 | 0x100, sz, d, opd_reg(I->reg), 0, live);
        return 1;
    }
    case 0xA8:
    case 0xA9:
        sz = op & 1 ? osz : 1;
        alu_gen(4 | 0x100, sz, opd_reg(EAX), opd_imm(), I->imm, live);
        return 1;
    case 0x86:
    case 0x87: {
        if (op == 0x86 || osz != 4)
            return 0;
        d = opd_rm(I);
        if (d.k == 0) {
            a_mov(0, GR(d.r));
            a_mov(GR(d.r), GR(I->reg));
            a_mov(GR(I->reg), 0);
        } else {
            ld(4, 0, d.ea);
            st(4, GR(I->reg), d.ea);
            a_mov(GR(I->reg), 0);
        }
        return 1;
    }
    case 0x88:
    case 0x89:
        sz = op & 1 ? osz : 1;
        d = opd_rm(I);
        {
            int v = reg_read(I->reg, sz, 0);
            wrv(d, sz, v);
        }
        return 1;
    case 0x8A:
    case 0x8B: {
        sz = op & 1 ? osz : 1;
        s = opd_rm(I);
        if (sz == 4) {
            if (s.k == 1)
                ld(4, GR(I->reg), s.ea);
            else
                a_mov(GR(I->reg), GR(s.r));
        } else {
            int v = rdv(s, sz, 0, 0);
            reg_write(I->reg, sz, v);
        }
        return 1;
    }
    case 0x8D: {
        if (I->isreg)
            return 0;
        if (osz == 4) {
            int r = ea_of(I, GR(I->reg), 9);
            if (r != GR(I->reg))
                a_mov(GR(I->reg), r);
        } else {
            int r = ea_of(I, 0, 9);
            reg_write(I->reg, 2, r);
        }
        return 1;
    }
    case 0x90:
    case 0x9B:
        return 1;
    case 0x98:
        if (osz != 4)
            return 0;
        a_sxth(GR(EAX), GR(EAX));
        return 1;
    case 0x99:
        if (osz != 4)
            return 0;
        a_asri(GR(EDX), GR(EAX), 31);
        return 1;
    case 0xA0:
    case 0xA1:
        sz = op & 1 ? osz : 1;
        a_movi(8, I->imm);
        {
            int v = rdv((Opd){1, 0, 8}, sz, 0, 0);
            reg_write(EAX, sz, v);
        }
        return 1;
    case 0xA2:
    case 0xA3:
        sz = op & 1 ? osz : 1;
        a_movi(8, I->imm);
        {
            int v = reg_read(EAX, sz, 0);
            st(sz, v, 8);
        }
        return 1;
    case 0xC0:
    case 0xC1:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3: {
        sz = op & 1 ? osz : 1;
        if ((I->live & I->fw) && shift_flags_ok(I, sz)) {
            shift_gen_flags(I);
            return 1;
        }
        if (!shift_ok(I, sz))
            return 0;
        shift_gen(I, sz);
        return 1;
    }
    case 0xC6:
    case 0xC7:
        sz = op & 1 ? osz : 1;
        d = opd_rm(I);
        if (d.k == 0) {
            a_movi(0, I->imm);
            reg_write(d.r, sz, 0);
        } else {
            a_movi(0, I->imm & mask_of(sz));
            st(sz, 0, d.ea);
        }
        return 1;
    case 0xC9:
        a_mov(GR(ESP), GR(EBP));
        a_mem(LDRW, GR(EBP), XG, GR(ESP));
        a_imm(ADDI, GR(ESP), GR(ESP), 4);
        return 1;
    case 0xF6:
    case 0xF7: {
        sz = op & 1 ? osz : 1;
        switch (I->reg) {
        case 0:
        case 1:
            d = opd_rm(I);
            alu_gen(4 | 0x100, sz, d, opd_imm(), I->imm, live);
            return 1;
        case 2: {
            d = opd_rm(I);
            int ra = rdv(d, sz, 0, 0);
            a_rrr(ORN, 2, XZR, ra, 0, 0);
            wrv(d, sz, 2);
            return 1;
        }
        case 3: {
            if (sz != 4 && (live & F_ALL))
                return 0;
            d = opd_rm(I);
            if (sz == 4) {
                int ra = rdv(d, 4, 0, 0), tgt = d.k == 0 ? GR(d.r) : 2;
                a_rrr(live & F_ALL ? SUBS : SUB, tgt, XZR, ra, 0, 0);
                if (d.k == 1)
                    st(4, tgt, d.ea);
                if (live & F_ALL) {
                    fstate = FS_NZCV;
                    cinv = 1;
                    if (live & F_P)
                        gen_pf(tgt, 0);
                }
            } else {
                int ra = rdv(d, sz, 0, 0);
                a_rrr(SUB, 2, XZR, ra, 0, 0);
                wrv(d, sz, 2);
            }
            return 1;
        }
        case 6:
        case 7: { // div / idiv r/m32: EDX:EAX / src
            if (sz != 4)
                return 0;
            s = opd_rm(I);
            int rs = rdv(s, 4, 0, 0);
            u32 *cb = jp;
            a_cbnz(rs, 0); // divisor 0: let the interpreter report it
            gen_fb_noret(I);
            *cb |= ((u32)(jp - cb) & 0x7ffff) << 5;
            a_rrrx(ORR, 1, GR(EAX), GR(EDX), LSL_, 32);
            if (I->reg == 6) {
                a_udivx(2, 1, rs);
                a_msubx(3, 2, rs, 1);
            } else {
                a_sxtw(4, rs);
                a_sdivx(2, 1, 4);
                a_msubx(3, 2, 4, 1);
            }
            a_mov(GR(EAX), 2);
            a_mov(GR(EDX), 3);
            return 1;
        }
        case 4:
        case 5:
            if (sz != 4 || (live & (F_C | F_O)))
                return 0;
            s = opd_rm(I);
            {
                int rs = rdv(s, 4, 0, 0);
                if (I->reg == 4)
                    a_umull(1, GR(EAX), rs);
                else
                    a_smull(1, GR(EAX), rs);
                a_mov(GR(EAX), 1);
                a_lsrix(2, 1, 32);
                a_mov(GR(EDX), 2);
            }
            return 1;
        }
        return 0;
    }
    case 0xFE:
    case 0xFF: {
        sz = op & 1 ? osz : 1;
        if (I->reg < 2) {
            if (!incdec_ok(sz, live))
                return 0;
            d = opd_rm(I);
            incdec_gen(I->reg, sz, d, live);
            return 1;
        }
        if (op == 0xFF && I->reg == 6) {
            s = opd_rm(I);
            int v = rdv(s, 4, 0, 0);
            if (v == GR(ESP)) {
                a_mov(0, v);
                v = 0;
            }
            a_imm(SUBI, GR(ESP), GR(ESP), 4);
            st(4, v, GR(ESP));
            return 1;
        }
        return 0;
    }
    case 0x100 | 0x40:
    case 0x100 | 0x41:
    case 0x100 | 0x42:
    case 0x100 | 0x43:
    case 0x100 | 0x44:
    case 0x100 | 0x45:
    case 0x100 | 0x46:
    case 0x100 | 0x47:
    case 0x100 | 0x48:
    case 0x100 | 0x49:
    case 0x100 | 0x4A:
    case 0x100 | 0x4B:
    case 0x100 | 0x4C:
    case 0x100 | 0x4D:
    case 0x100 | 0x4E:
    case 0x100 | 0x4F: {
        int cc = op & 15;
        if (osz != 4 || (cc >> 1) == 5)
            return 0;
        loadflags();
        int ac = arm_cond(cc);
        s = opd_rm(I);
        int v = rdv(s, 4, 0, 0);
        a_csel(CSEL, GR(I->reg), v, GR(I->reg), ac);
        return 1;
    }
    case 0x100 | 0x90:
    case 0x100 | 0x91:
    case 0x100 | 0x92:
    case 0x100 | 0x93:
    case 0x100 | 0x94:
    case 0x100 | 0x95:
    case 0x100 | 0x96:
    case 0x100 | 0x97:
    case 0x100 | 0x98:
    case 0x100 | 0x99:
    case 0x100 | 0x9A:
    case 0x100 | 0x9B:
    case 0x100 | 0x9C:
    case 0x100 | 0x9D:
    case 0x100 | 0x9E:
    case 0x100 | 0x9F: {
        int cc = op & 15;
        if ((cc >> 1) == 5)
            return 0;
        loadflags();
        int ac = arm_cond(cc);
        d = opd_rm(I);
        a_cset(0, ac);
        wrv(d, 1, 0);
        return 1;
    }
    case 0x100 | 0xBC:
    case 0x100 | 0xBD: {
        if (osz != 4 || (live & (F_C | F_S | F_O | F_P)))
            return 0;
        s = opd_rm(I);
        int ra = rdv(s, 4, 0, 0);
        if (op == 0x1BC) {
            a_rbit(1, ra);
            a_clz(1, 1);
        } else {
            a_clz(1, ra);
            a_eor31(1, 1);
        }
        a_imm(SUBSI, XZR, ra, 0);                     // ZF = (src == 0)
        a_csel(CSEL, GR(I->reg), 1, GR(I->reg), CNE); // dest unchanged when src == 0
        fstate = FS_NZCV;
        cinv = 1;
        return 1;
    }
    case 0x100 | 0xAF: {
        if (osz != 4 || (live & (F_C | F_O)))
            return 0;
        s = opd_rm(I);
        int rs = rdv(s, 4, 0, 0);
        a_mul(GR(I->reg), GR(I->reg), rs);
        return 1;
    }
    case 0x100 | 0xB6:
    case 0x100 | 0xB7:
    case 0x100 | 0xBE:
    case 0x100 | 0xBF: {
        int ssz = (op & 1) ? 2 : 1, sxn = op >= 0x1BE;
        s = opd_rm(I);
        int dst = osz == 4 ? GR(I->reg) : 0;
        if (s.k == 1)
            a_mem(sxn ? (ssz == 2 ? LDRSH : LDRSB) : (ssz == 2 ? LDRH : LDRB), dst, XG, s.ea);
        else if (ssz == 2) {
            if (sxn)
                a_sxth(dst, GR(s.r));
            else
                a_uxth(dst, GR(s.r));
        } else if (s.r < 4) {
            if (sxn)
                a_sxtb(dst, GR(s.r));
            else
                a_uxtb(dst, GR(s.r));
        } else {
            if (sxn)
                a_sbfx(dst, GR(s.r - 4), 8, 8);
            else
                a_ubfx(dst, GR(s.r - 4), 8, 8);
        }
        if (osz != 4)
            reg_write(I->reg, 2, dst);
        return 1;
    }
    }
    if (op >= 0x1C8 && op <= 0x1CF) {
        a_rev(GR(op & 7), GR(op & 7));
        return 1;
    }
    if (op == 0x11F || op == 0x118 || op == 0x10D)
        return 1; // nop Ev / prefetch
    if (op >= 0x110)
        return gen_sse(I);
    return 0;
}
