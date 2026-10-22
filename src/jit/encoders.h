// encoders.h: ARM64 instruction encoders.
//
// Tiny emit helpers (one function per instruction form) writing into the code buffer at `jp`: data processing, loads/stores,
// branches, conditional select, and the NEON/FP forms the SSE translation uses.

static u32 *jp; // emit pointer
static inline void E(u32 x) {
    *jp++ = x;
}

enum { CEQ, CNE, CCS, CCC, CMI, CPL, CVS, CVC, CHI, CLS, CGE, CLT, CGT, CLE };
#define XZR 31
#define SF64 0x80000000u

static void a_movz(int rd, u32 imm, int hw) {
    E(0x52800000u | hw << 21 | (imm & 0xffff) << 5 | rd);
}
static void a_movk(int rd, u32 imm, int hw) {
    E(0x72800000u | hw << 21 | (imm & 0xffff) << 5 | rd);
}
static void a_movn(int rd, u32 imm, int hw) {
    E(0x12800000u | hw << 21 | (imm & 0xffff) << 5 | rd);
}
static void a_movi(int rd, u32 v) { // 32-bit constant -> wRd
    if ((v >> 16) == 0)
        a_movz(rd, v, 0);
    else if ((v & 0xffff) == 0)
        a_movz(rd, v >> 16, 1);
    else if ((v >> 16) == 0xffff)
        a_movn(rd, ~v, 0);
    else if ((v & 0xffff) == 0xffff)
        a_movn(rd, ~v >> 16, 1);
    else {
        a_movz(rd, v, 0);
        a_movk(rd, v >> 16, 1);
    }
}

enum { ADDI = 0x11000000, SUBI = 0x51000000, ADDSI = 0x31000000, SUBSI = 0x71000000 };
static void a_imm(u32 op, int rd, int rn, u32 imm12) {
    E(op | imm12 << 10 | rn << 5 | rd);
} // W form
static void a_immx(u32 op, int rd, int rn, u32 imm12) {
    E(SF64 | op | imm12 << 10 | rn << 5 | rd);
} // X form

enum {
    ADD = 0x0B000000,
    ADDS = 0x2B000000,
    SUB = 0x4B000000,
    SUBS = 0x6B000000,
    AND = 0x0A000000,
    ORR = 0x2A000000,
    EOR = 0x4A000000,
    ANDS = 0x6A000000,
    BIC = 0x0A200000,
    ORN = 0x2A200000
};
enum { LSL_, LSR_, ASR_, ROR_ };
static void a_rrr(u32 op, int rd, int rn, int rm, int sh, int amt) {
    E(op | sh << 22 | rm << 16 | amt << 10 | rn << 5 | rd);
} // W form
static void a_rrrx(u32 op, int rd, int rn, int rm, int sh, int amt) {
    E(SF64 | op | sh << 22 | rm << 16 | amt << 10 | rn << 5 | rd);
}
enum { ADC = 0x1A000000, ADCS = 0x3A000000, SBC = 0x5A000000, SBCS = 0x7A000000 };
static void a_adc(u32 op, int rd, int rn, int rm) {
    E(op | rm << 16 | rn << 5 | rd);
}
enum {
    LSLV = 0x1AC02000,
    LSRV = 0x1AC02400,
    ASRV = 0x1AC02800,
    RORV = 0x1AC02C00,
    UDIV = 0x1AC00800,
    SDIV = 0x1AC00C00
};
static void a_v(u32 op, int rd, int rn, int rm) {
    E(op | rm << 16 | rn << 5 | rd);
}
static void a_mov(int rd, int rm) {
    a_rrr(ORR, rd, XZR, rm, 0, 0);
}
static void a_movx_r(int rd, int rm) {
    a_rrrx(ORR, rd, XZR, rm, 0, 0);
}
static void a_cmp(int rn, int rm) {
    a_rrr(SUBS, XZR, rn, rm, 0, 0);
}
static void a_tst(int rn, int rm) {
    a_rrr(ANDS, XZR, rn, rm, 0, 0);
}

static void a_ubfm(int rd, int rn, int immr, int imms) {
    E(0x53000000u | immr << 16 | imms << 10 | rn << 5 | rd);
}
static void a_sbfm(int rd, int rn, int immr, int imms) {
    E(0x13000000u | immr << 16 | imms << 10 | rn << 5 | rd);
}
static void a_bfm(int rd, int rn, int immr, int imms) {
    E(0x33000000u | immr << 16 | imms << 10 | rn << 5 | rd);
}
static void a_lsli(int rd, int rn, int n) {
    a_ubfm(rd, rn, (32 - n) & 31, 31 - n);
}
static void a_lsri(int rd, int rn, int n) {
    a_ubfm(rd, rn, n, 31);
}
static void a_asri(int rd, int rn, int n) {
    a_sbfm(rd, rn, n, 31);
}
static void a_ubfx(int rd, int rn, int lsb, int w) {
    a_ubfm(rd, rn, lsb, lsb + w - 1);
}
static void a_sbfx(int rd, int rn, int lsb, int w) {
    a_sbfm(rd, rn, lsb, lsb + w - 1);
}
static void a_bfi(int rd, int rn, int lsb, int w) {
    a_bfm(rd, rn, (32 - lsb) & 31, w - 1);
}
static void a_bfxil(int rd, int rn, int lsb, int w) {
    a_bfm(rd, rn, lsb, lsb + w - 1);
}
static void a_rori(int rd, int rn, int n) {
    E(0x13800000u | rn << 16 | n << 10 | rn << 5 | rd);
} // extr rd, rn, rn, #n
static void a_lsrix(int rd, int rn, int n) {
    E(0xD340FC00u | n << 16 | rn << 5 | rd);
} // lsr xd, xn, #n

enum {
    LDRW = 0xB8604800,
    LDRH = 0x78604800,
    LDRB = 0x38604800,
    LDRSB = 0x38E04800,
    LDRSH = 0x78E04800,
    LDRX = 0xF8604800,
    STRW = 0xB8204800,
    STRH = 0x78204800,
    STRB = 0x38204800,
    STRX = 0xF8204800
};
static void a_mem(u32 op, int rt, int xn, int wm) {
    E(op | wm << 16 | xn << 5 | rt);
} // [xn, wm, uxtw]
enum {
    LDRWI = 0xB9400000,
    STRWI = 0xB9000000,
    LDRXI = 0xF9400000,
    STRXI = 0xF9000000,
    LDRBI = 0x39400000,
    STRBI = 0x39000000,
    LDRHI = 0x79400000,
    STRHI = 0x79000000
};
static void a_memi(u32 op, int rt, int xn, u32 off) { // unsigned scaled offset
    int sc = (op >> 30) & 3;
    E(op | (off >> sc) << 10 | xn << 5 | rt);
}
static void a_ldpw(int rt, int rt2, int rn, int off) {
    E(0x29400000u | ((off / 4) & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt);
}
static void a_stpw(int rt, int rt2, int rn, int off) {
    E(0x29000000u | ((off / 4) & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt);
}
static void a_ldpx(int rt, int rt2, int rn, int off) {
    E(0xA9400000u | ((off / 8) & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt);
}
static void a_stpx(int rt, int rt2, int rn, int off) {
    E(0xA9000000u | ((off / 8) & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt);
}
static void a_stpx_pre(int rt, int rt2, int rn, int off) {
    E(0xA9800000u | ((off / 8) & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt);
}
static void a_ldpx_post(int rt, int rt2, int rn, int off) {
    E(0xA8C00000u | ((off / 8) & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt);
}

static void a_b(u32 *target) {
    E(0x14000000u | (((target - jp)) & 0x3ffffff));
}
static void a_bl(u32 *target) {
    E(0x94000000u | (((target - jp)) & 0x3ffffff));
}
static void a_bcond(int cc, int off_words) {
    E(0x54000000u | (off_words & 0x7ffff) << 5 | cc);
}
static void a_cbz(int rt, int off_words) {
    E(0x34000000u | (off_words & 0x7ffff) << 5 | rt);
}
static void a_cbnz(int rt, int off_words) {
    E(0x35000000u | (off_words & 0x7ffff) << 5 | rt);
}
static void a_tbnz(int rt, int bit, int off_words) {
    E(0x37000000u | (bit & 32) << 26 | (bit & 31) << 19 | (off_words & 0x3fff) << 5 | rt);
}
static void a_br(int rn) {
    E(0xD61F0000u | rn << 5);
}
static void a_blr(int rn) {
    E(0xD63F0000u | rn << 5);
}
static void a_ret(void) {
    E(0xD65F03C0u);
}
static void a_ldrlit_x(int rt, int off_words) {
    E(0x58000000u | (off_words & 0x7ffff) << 5 | rt);
}

enum { CSEL = 0x1A800000, CSINC = 0x1A800400, CSINV = 0x5A800000, CSNEG = 0x5A800400 };
static void a_csel(u32 op, int rd, int rn, int rm, int cc) {
    E(op | rm << 16 | cc << 12 | rn << 5 | rd);
}
static void a_cset(int rd, int cc) {
    a_csel(CSINC, rd, XZR, XZR, cc ^ 1);
}
static void a_mul(int rd, int rn, int rm) {
    E(0x1B007C00u | rm << 16 | rn << 5 | rd);
}
static void a_smull(int rd, int rn, int rm) {
    E(0x9B207C00u | rm << 16 | rn << 5 | rd);
}
static void a_umull(int rd, int rn, int rm) {
    E(0x9BA07C00u | rm << 16 | rn << 5 | rd);
}
static void a_msub(int rd, int rn, int rm, int ra) {
    E(0x1B008000u | rm << 16 | ra << 10 | rn << 5 | rd);
}
static void a_rev(int rd, int rn) {
    E(0x5AC00800u | rn << 5 | rd);
}
static void a_sxtb(int rd, int rn) {
    a_sbfm(rd, rn, 0, 7);
}
static void a_sxth(int rd, int rn) {
    a_sbfm(rd, rn, 0, 15);
}
static void a_uxtb(int rd, int rn) {
    a_ubfm(rd, rn, 0, 7);
}
static void a_uxth(int rd, int rn) {
    a_ubfm(rd, rn, 0, 15);
}
static void a_mrs_nzcv(int rt) {
    E(0xD53B4200u | rt);
}
static void a_msr_nzcv(int rt) {
    E(0xD51B4200u | rt);
}
static void a_cfinv(void) {
    E(0xD500401Fu);
}
static void a_nop(void) {
    E(0xD503201Fu);
}
static void a_eor31(int rd, int rn) {
    E(0x52001000u | rn << 5 | rd);
} // eor wd, wn, #31
static void a_eor1(int rd, int rn) {
    E(0x52000000u | rn << 5 | rd);
} // eor wd, wn, #1

// ---- NEON / FP ----
enum {
    LDRQI = 0x3DC00000,
    STRQI = 0x3D800000,
    LDRDI = 0xFD400000,
    STRDI = 0xFD000000,
    LDRSI = 0xBD400000,
    STRSI = 0xBD000000
};
enum {
    LDRQ = 0x3CE04800,
    STRQ = 0x3CA04800,
    LDRD = 0xFC604800,
    STRD = 0xFC204800,
    LDRS = 0xBC604800,
    STRS = 0xBC204800
};
static void a_memqi(u32 op, int rt, int xn, u32 off) {
    int sc = op == LDRQI || op == STRQI ? 4 : (op >> 30) & 3;
    E(op | (off >> sc) << 10 | xn << 5 | rt);
}
static void a_v3(u32 op, int rd, int rn, int rm) {
    E(op | rm << 16 | rn << 5 | rd);
} // vector/scalar three-register forms
static void a_v2(u32 op, int rd, int rn) {
    E(op | rn << 5 | rd);
} // two-register forms
static void a_ins_s(int rd, int di, int rn, int si) {
    E(0x6E000400u | ((di << 3 | 4) << 16) | (si << 2) << 11 | rn << 5 | rd);
} // ins vd.s[di], vn.s[si]
static void a_ins_d(int rd, int di, int rn, int si) {
    E(0x6E000400u | ((di << 4 | 8) << 16) | (si << 3) << 11 | rn << 5 | rd);
} // ins vd.d[di], vn.d[si]
static void a_dup_s(int rd, int rn, int si) {
    E(0x4E000400u | ((si << 3 | 4) << 16) | rn << 5 | rd);
} // dup vd.4s, vn.s[si]
static void a_umov_s(int wd, int rn, int si) {
    E(0x0E003C00u | ((si << 3 | 4) << 16) | rn << 5 | wd);
} // umov wd, vn.s[si]
static void a_sxtw(int rd, int rn) {
    E(0x93407C00u | rn << 5 | rd);
}
static void a_udivx(int rd, int rn, int rm) {
    E(0x9AC00800u | rm << 16 | rn << 5 | rd);
}
static void a_sdivx(int rd, int rn, int rm) {
    E(0x9AC00C00u | rm << 16 | rn << 5 | rd);
}
static void a_msubx(int rd, int rn, int rm, int ra) {
    E(0x9B008000u | rm << 16 | ra << 10 | rn << 5 | rd);
}
static void a_rbit(int rd, int rn) {
    E(0x5AC00000u | rn << 5 | rd);
}
static void a_clz(int rd, int rn) {
    E(0x5AC01000u | rn << 5 | rd);
}
static void a_ext(int rd, int rn, int rm, int imm) {
    E(0x6E000000u | rm << 16 | imm << 11 | rn << 5 | rd);
} // ext vd.16b, vn.16b, vm.16b, #imm
