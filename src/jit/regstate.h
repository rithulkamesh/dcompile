// regstate.h: per-block register and flag state; ALU/shift code generation.
//
// While in JIT code the eight guest registers live in host x19..x26. Flags are carried lazily in ARM NZCV inside a block (CF is
// inverted after sub-type operations) and materialised into the CPU struct at exits and helper calls. This file tracks that
// state and emits the integer ALU, inc/dec and shift/rotate instructions.

enum { FS_STRUCT, FS_NZCV };
static int fstate, cinv, need_load, need_store;
typedef struct {
    u32 *site;
    u32 tgt;
} Exit;
static Exit exits[2 * MAXB + 8];
static int nexits;

static void sync_load(void) {
    if (!need_load)
        return;
    a_ldpw(19, 20, XC, 0);
    a_ldpw(21, 22, XC, 8);
    a_ldpw(23, 24, XC, 16);
    a_ldpw(25, 26, XC, 24);
    need_load = 0;
    need_store = 0;
}
static void sync_store(void) {
    a_stpw(19, 20, XC, 0);
    a_stpw(21, 22, XC, 8);
    a_stpw(23, 24, XC, 16);
    a_stpw(25, 26, XC, 24);
    need_store = 0;
}

static void loadflags(void) { // NZCV <- CPU.sf/zf/cf/of (clobbers w0-w3)
    if (fstate == FS_NZCV)
        return;
    a_memi(LDRWI, 0, XC, OFF(sf));
    a_memi(LDRWI, 1, XC, OFF(zf));
    a_memi(LDRWI, 2, XC, OFF(cf));
    a_memi(LDRWI, 3, XC, OFF(of));
    a_lsli(0, 0, 31);
    a_rrr(ORR, 0, 0, 1, LSL_, 30);
    a_rrr(ORR, 0, 0, 2, LSL_, 29);
    a_rrr(ORR, 0, 0, 3, LSL_, 28);
    a_msr_nzcv(0);
    fstate = FS_NZCV;
    cinv = 0;
}
static void flush_flags(u8 mask) { // materialize live flags from NZCV into the CPU struct (clobbers w10-w13)
    if (fstate != FS_NZCV)
        return;
    mask &= F_C | F_Z | F_S | F_O;
    if (mask & F_C) {
        a_cset(10, cinv ? CCC : CCS);
        a_memi(STRWI, 10, XC, OFF(cf));
    }
    if (mask & F_Z) {
        a_cset(11, CEQ);
        a_memi(STRWI, 11, XC, OFF(zf));
    }
    if (mask & F_S) {
        a_cset(12, CMI);
        a_memi(STRWI, 12, XC, OFF(sf));
    }
    if (mask & F_O) {
        a_cset(13, CVS);
        a_memi(STRWI, 13, XC, OFF(of));
    }
}
// ARM condition for x86 condition cc (not parity); NZCV must be valid. May flip the carry convention.
static int arm_cond(int cc) {
    int base;
    switch (cc >> 1) {
    case 0:
        base = CVS;
        break;
    case 1:
        base = cinv ? CCC : CCS;
        break;
    case 2:
        base = CEQ;
        break;
    case 3:
        if (!cinv) {
            a_cfinv();
            cinv = 1;
        }
        base = CLS;
        break;
    case 4:
        base = CMI;
        break;
    case 6:
        base = CLT;
        break;
    default:
        base = CLE;
        break;
    }
    return cc & 1 ? base ^ 1 : base;
}

// ---- guest memory / register operand helpers ----
static void ld(int sz, int rt, int ea) {
    a_mem(sz == 4 ? LDRW : sz == 2 ? LDRH : LDRB, rt, XG, ea);
}
static void st(int sz, int rt, int ea) {
    if (jcheck) {
        a_mov(16, ea);
        a_movz(17, sz, 0);
        a_bl(t_wlog);
    }
    a_mem(sz == 4 ? STRW : sz == 2 ? STRH : STRB, rt, XG, ea);
}
static int add_const(int rd, int rn, i32 d, int tmp) {
    if (d > 0 && d < 4096)
        a_imm(ADDI, rd, rn, d);
    else if (d < 0 && d > -4096)
        a_imm(SUBI, rd, rn, -d);
    else {
        a_movi(tmp, (u32)d);
        a_rrr(ADD, rd, rn, tmp, 0, 0);
    }
    return rd;
}
// guest effective address of the memory operand in host register (t = scratch result, t2 = scratch)
static int ea_of(const Ins *I, int t, int t2) {
    int b = I->base, x = I->idx, r;
    i32 d = I->disp;
    if (b < 0 && x < 0) {
        a_movi(t, (u32)d);
        return t;
    }
    if (x >= 0) {
        if (b >= 0) {
            a_rrr(ADD, t, GR(b), GR(x), LSL_, I->sc);
            r = t;
        } else if (I->sc) {
            a_lsli(t, GR(x), I->sc);
            r = t;
        } else
            r = GR(x);
    } else
        r = GR(b);
    if (d)
        r = add_const(t, r, d, t2);
    return r;
}
static int reg_read(int r, int sz, int t) { // zero-extended value of register r (size sz) -> host reg
    if (sz == 4)
        return GR(r);
    if (sz == 2) {
        a_uxth(t, GR(r));
        return t;
    }
    if (r < 4)
        a_uxtb(t, GR(r));
    else
        a_ubfx(t, GR(r - 4), 8, 8);
    return t;
}
static void reg_write(int r, int sz, int v) {
    if (sz == 4) {
        if (v != GR(r))
            a_mov(GR(r), v);
    } else if (sz == 2)
        a_bfi(GR(r), v, 0, 16);
    else if (r < 4)
        a_bfi(GR(r), v, 0, 8);
    else
        a_bfi(GR(r - 4), v, 8, 8);
}
typedef struct {
    int k, r, ea;
} Opd; // k: 0 register, 1 memory (ea = host reg), 2 immediate
static Opd opd_reg(int r) {
    Opd o = {0, r, 0};
    return o;
}
static Opd opd_imm(void) {
    Opd o = {2, 0, 0};
    return o;
}
static Opd opd_rm(const Ins *I) {
    Opd o;
    if (I->isreg) {
        o.k = 0;
        o.r = I->rm;
        o.ea = 0;
    } else {
        o.k = 1;
        o.r = 0;
        o.ea = ea_of(I, 8, 9);
    }
    return o;
}
static int rdv(Opd o, int sz, int tmp, u32 imm) { // zero-extended value
    if (o.k == 0)
        return reg_read(o.r, sz, tmp);
    if (o.k == 1) {
        ld(sz, tmp, o.ea);
        return tmp;
    }
    a_movi(tmp, imm & mask_of(sz));
    return tmp;
}
static void wrv(Opd o, int sz, int v) {
    if (o.k == 0)
        reg_write(o.r, sz, v);
    else
        st(sz, v, o.ea);
}
static int al_read(Opd o, int sz, int tmp, u32 imm) { // value << (32 - 8*sz)
    int sh = 32 - 8 * sz;
    if (o.k == 2) {
        a_movi(tmp, (imm & mask_of(sz)) << sh);
        return tmp;
    }
    if (o.k == 1) {
        ld(sz, tmp, o.ea);
        if (sh)
            a_lsli(tmp, tmp, sh);
        return tmp;
    }
    if (sz == 4)
        return GR(o.r);
    if (sz == 2)
        a_lsli(tmp, GR(o.r), 16);
    else if (o.r < 4)
        a_lsli(tmp, GR(o.r), 24);
    else {
        a_ubfx(tmp, GR(o.r - 4), 8, 8);
        a_lsli(tmp, tmp, 24);
    }
    return tmp;
}
static void gen_pf(int r, int sh) { // CPU.pf <- parity of byte (r >> sh) & 0xff (clobbers w5)
    a_ubfx(5, r, sh, 8);
    a_rrr(EOR, 5, 5, 5, LSR_, 4);
    a_rrr(EOR, 5, 5, 5, LSR_, 2);
    a_rrr(EOR, 5, 5, 5, LSR_, 1);
    a_ubfx(5, 5, 0, 1);
    a_eor1(5, 5); // PF = !parity-bit
    a_memi(STRWI, 5, XC, OFF(pf));
}

// ---- ALU ----
// aop: 0 add 1 or 2 adc 3 sbb 4 and 5 sub 6 xor 7 cmp. Returns 0 (nothing emitted) when the combination is not handled natively.
static int alu_ok(int aop, int sz) {
    return !((aop == 2 || aop == 3) && sz < 4);
}
static void carry_in(int sbb) { // make ARM C usable by adc/sbc
    loadflags();
    if (sbb ? !cinv : cinv) {
        a_cfinv();
        cinv ^= 1;
    }
}
static void alu_gen(int aop, int sz, Opd d, Opd s, u32 imm, u8 live) {
    int store = aop != 7 && !(aop & 0x100), op = (aop == 7 ? 5 : aop) & 7, nf = live & F_ALL,
        wantpf = live & F_P;
    if (!store && !nf)
        return; // cmp / test whose flags nobody reads
    if (sz == 4) {
        int ra, rb = -1, tgt;
        if (op == 2 || op == 3)
            carry_in(op == 3);
        ra = rdv(d, 4, 0, 0);
        int immform = s.k == 2 && (op == 0 || op == 5) && imm < 4096;
        if (!nf && s.k == 2 && (op == 0 || op == 5) && (i32)imm < 0 &&
            (i32)imm > -4096) { // add/sub of a small negative imm, flags dead
            tgt = (store && d.k == 0) ? GR(d.r) : 2;
            a_imm(op == 0 ? SUBI : ADDI, tgt, ra, -(i32)imm);
            if (store && d.k == 1)
                st(4, tgt, d.ea);
            return;
        }
        if (!immform)
            rb = rdv(s, 4, 1, imm);
        tgt = (store && d.k == 0) ? GR(d.r) : 2;
        if (!store)
            tgt = wantpf ? 2 : XZR;
        int S = nf != 0 || op == 2 || op == 3;
        if (immform)
            a_imm(op == 0 ? (S ? ADDSI : ADDI) : (S ? SUBSI : SUBI), tgt, ra, imm);
        else
            switch (op) {
            case 0:
                a_rrr(S ? ADDS : ADD, tgt, ra, rb, 0, 0);
                break;
            case 1:
                a_rrr(ORR, tgt, ra, rb, 0, 0);
                if (nf)
                    a_tst(tgt, tgt);
                break;
            case 2:
                a_adc(ADCS, tgt, ra, rb);
                break;
            case 3:
                a_adc(SBCS, tgt, ra, rb);
                break;
            case 4:
                a_rrr(S ? ANDS : AND, tgt, ra, rb, 0, 0);
                break;
            case 5:
                a_rrr(S ? SUBS : SUB, tgt, ra, rb, 0, 0);
                break;
            case 6:
                a_rrr(EOR, tgt, ra, rb, 0, 0);
                if (nf)
                    a_tst(tgt, tgt);
                break;
            }
        if (store && d.k == 1)
            st(4, tgt, d.ea);
        if (S) {
            fstate = FS_NZCV;
            cinv = (op == 5 || op == 3);
        }
        if (wantpf)
            gen_pf(tgt, 0);
        return;
    }
    // 8 / 16 bit
    if (!nf) { // flags dead: plain arithmetic on zero-extended values, merge the low bits
        if (!store)
            return;
        int ra = rdv(d, sz, 0, 0), rb = rdv(s, sz, 1, imm);
        switch (op) {
        case 0:
            a_rrr(ADD, 2, ra, rb, 0, 0);
            break;
        case 1:
            a_rrr(ORR, 2, ra, rb, 0, 0);
            break;
        case 4:
            a_rrr(AND, 2, ra, rb, 0, 0);
            break;
        case 5:
            a_rrr(SUB, 2, ra, rb, 0, 0);
            break;
        default:
            a_rrr(EOR, 2, ra, rb, 0, 0);
            break;
        }
        wrv(d, sz, 2);
        return;
    }
    int sh = 32 - 8 * sz;
    int ra = al_read(d, sz, 0, 0), rb = al_read(s, sz, 1, imm);
    switch (op) {
    case 0:
        a_rrr(ADDS, 2, ra, rb, 0, 0);
        break;
    case 1:
        a_rrr(ORR, 2, ra, rb, 0, 0);
        a_tst(2, 2);
        break;
    case 4:
        a_rrr(ANDS, 2, ra, rb, 0, 0);
        break;
    case 5:
        a_rrr(SUBS, 2, ra, rb, 0, 0);
        break;
    default:
        a_rrr(EOR, 2, ra, rb, 0, 0);
        a_tst(2, 2);
        break;
    }
    fstate = FS_NZCV;
    cinv = op == 5;
    if (wantpf)
        gen_pf(2, sh);
    if (store) {
        a_lsri(3, 2, sh);
        wrv(d, sz, 3);
    }
}

// inc / dec
static int incdec_ok(int sz, u8 live) {
    return sz == 4 || !(live & F_C);
}
static void incdec_gen(int dec, int sz, Opd d, u8 live) {
    int need = live & F_NOC, cfl = live & F_C;
    if (sz != 4) { // CF is dead (checked by incdec_ok)
        if (!(live & F_ALL)) {
            int ra = rdv(d, sz, 0, 0);
            a_imm(dec ? SUBI : ADDI, 2, ra, 1);
            wrv(d, sz, 2);
            return;
        }
        int sh = 32 - 8 * sz, ra = al_read(d, sz, 0, 0);
        a_movi(1, 1u << sh);
        a_rrr(dec ? SUBS : ADDS, 2, ra, 1, 0, 0);
        cinv = dec;
        fstate = FS_NZCV;
        if (live & F_P)
            gen_pf(2, sh);
        a_lsri(3, 2, sh);
        wrv(d, sz, 3);
        return;
    }
    if (need && cfl)
        loadflags(); // clobbers w0-w3: do it before reading the operand
    int ra = rdv(d, 4, 0, 0), tgt = d.k == 0 ? GR(d.r) : 2;
    if (!need) {
        a_imm(dec ? SUBI : ADDI, tgt, ra, 1);
        if (d.k == 1)
            st(4, tgt, d.ea);
        return;
    }
    if (cfl) {                       // CF must survive: run the op, then put the old carry back
        a_cset(7, cinv ? CCC : CCS); // w7 = CF
        a_imm(dec ? SUBSI : ADDSI, tgt, ra, 1);
        if (dec)
            a_eor1(7, 7); // sub leaves C = !CF
        a_mrs_nzcv(6);
        a_bfi(6, 7, 29, 1);
        a_msr_nzcv(6);
    } else
        a_imm(dec ? SUBSI : ADDSI, tgt, ra, 1);
    cinv = dec;
    fstate = FS_NZCV;
    if (d.k == 1)
        st(4, tgt, d.ea);
    if (live & F_P)
        gen_pf(tgt, 0);
}

// ---- shifts / rotates (native only when no live flag depends on them) ----
static int shift_ok(Ins *I, int sz) {
    int sh = I->reg;
    if (sh == 2 || sh == 3)
        return 0;
    if (sz != 4 && sh < 2)
        return 0;
    if (I->op == 0xD2 || I->op == 0xD3)
        return !(I->live & F_ALL);
    return !(I->live & I->fw);
}
static int shift_flags_ok(Ins *I, int sz) { // shl/shr/sar by a constant 1..31 on a 32-bit operand
    if (sz != 4 || !(I->reg == 4 || I->reg == 5 || I->reg == 6 || I->reg == 7))
        return 0;
    if (I->op == 0xD1)
        return 1;
    return I->op == 0xC1 && (I->imm & 31) != 0;
}
static void shift_gen_flags(Ins *I) {
    int sh = I->reg, cnt = I->op == 0xD1 ? 1 : (int)(I->imm & 31);
    u8 live = I->live;
    Opd d = opd_rm(I);
    int ra = rdv(d, 4, 0, 0), tgt = d.k == 0 ? GR(d.r) : 2;
    int wantC = live & F_C, wantO = live & F_O;
    if (wantC || (wantO && (sh == 4 || sh == 6))) {
        if (sh == 4 || sh == 6)
            a_ubfx(6, ra, 32 - cnt, 1);
        else
            a_ubfx(6, ra, cnt - 1, 1);
    } // CF = last bit shifted out
    if (wantO && sh == 5)
        a_ubfx(7, ra, 31, 1); // shr: OF = old sign bit
    if (sh == 4 || sh == 6)
        a_lsli(tgt, ra, cnt);
    else if (sh == 5)
        a_lsri(tgt, ra, cnt);
    else
        a_asri(tgt, ra, cnt);
    if (wantO && (sh == 4 || sh == 6)) {
        a_lsri(7, tgt, 31);
        a_rrr(EOR, 7, 7, 6, 0, 0);
    } // shl: OF = msb(result) ^ CF
    a_tst(tgt, tgt); // N, Z (clears C, V)
    if (wantC || wantO) {
        a_mrs_nzcv(4);
        if (wantC)
            a_bfi(4, 6, 29, 1);
        if (wantO && sh != 7)
            a_bfi(4, 7, 28, 1);
        a_msr_nzcv(4);
    }
    fstate = FS_NZCV;
    cinv = 0;
    if (live & F_P)
        gen_pf(tgt, 0);
    if (d.k == 1)
        st(4, tgt, d.ea);
}
static void shift_gen(Ins *I, int sz) {
    int sh = I->reg;
    Opd d = opd_rm(I);
    int cnt = (I->op == 0xD2 || I->op == 0xD3)   ? -1
              : (I->op == 0xD0 || I->op == 0xD1) ? 1
                                                 : (int)(I->imm & 31);
    if (cnt == 0)
        return;
    int ra;
    if (sh == 7 && sz != 4) { // sar: sign-extend first
        if (d.k == 1) {
            a_mem(sz == 2 ? LDRSH : LDRSB, 0, XG, d.ea);
            ra = 0;
        } else if (sz == 2) {
            a_sxth(0, GR(d.r));
            ra = 0;
        } else if (d.r < 4) {
            a_sxtb(0, GR(d.r));
            ra = 0;
        } else {
            a_sbfx(0, GR(d.r - 4), 8, 8);
            ra = 0;
        }
    } else
        ra = rdv(d, sz, 0, 0);
    int tgt = (d.k == 0 && sz == 4) ? GR(d.r) : 2;
    if (cnt < 0) {
        switch (sh) {
        case 4:
        case 6:
            a_v(LSLV, tgt, ra, GR(ECX));
            break;
        case 5:
            a_v(LSRV, tgt, ra, GR(ECX));
            break;
        case 7:
            a_v(ASRV, tgt, ra, GR(ECX));
            break;
        case 0:
            a_rrr(SUB, 1, XZR, GR(ECX), 0, 0);
            a_v(RORV, tgt, ra, 1);
            break;
        case 1:
            a_v(RORV, tgt, ra, GR(ECX));
            break;
        }
    } else {
        switch (sh) {
        case 4:
        case 6:
            a_lsli(tgt, ra, cnt);
            break;
        case 5:
            a_lsri(tgt, ra, cnt);
            break;
        case 7:
            a_asri(tgt, ra, cnt);
            break;
        case 0:
            a_rori(tgt, ra, (32 - cnt) & 31);
            break;
        case 1:
            a_rori(tgt, ra, cnt);
            break;
        }
    }
    if (d.k == 1)
        st(sz, tgt, d.ea);
    else if (sz != 4)
        reg_write(d.r, sz, tgt);
}
