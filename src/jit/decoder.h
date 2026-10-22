// decoder.h: x86 instruction decoder for the translator.
//
// Computes instruction length, operand shape, control-flow class and the flags an instruction reads (fr) and always writes (fw).
// fw is only set where the interpreter certainly writes the flag; it is the kill set of the flag-liveness analysis. The decoder
// mirrors exec()'s notion of prefixes and operand sizes, including its quirks.

enum { F_C = 1, F_P = 2, F_Z = 4, F_S = 8, F_O = 16, F_ALL = 31, F_NOC = 30 };
enum { CL_NAT, CL_FB, CL_TFB, CL_JMP, CL_JCC, CL_CALL, CL_RET, CL_JMPI, CL_CALLI };

typedef struct {
    u32 addr, imm, tgt;
    i32 disp;
    u16 op;
    u8 len, cls, osz, rep, hasm, mod, reg, rm, sc, isreg, fr, fw, live;
    i8 base, idx;
} Ins;

static const u8 CC_FLAGS[8] = {F_O, F_C, F_Z, F_C | F_Z, F_S, F_P, F_S | F_O, F_Z | F_S | F_O};

static inline u32 rd32g(u32 a) {
    u32 v;
    memcpy(&v, P(a), 4);
    return v;
}

static int op0f_has_modrm(u8 o) {
    if ((o >= 0x05 && o <= 0x09) || o == 0x0B || o == 0x0E || (o >= 0x30 && o <= 0x37) || o == 0x77 ||
        o == 0xA0 || o == 0xA1 || o == 0xA2 || o == 0xA8 || o == 0xA9 || o == 0xAA ||
        (o >= 0xC8 && o <= 0xCF) || (o >= 0x80 && o <= 0x8F))
        return 0;
    return 1;
}

// Decodes one instruction at guest address a. Mirrors exec()'s notion of prefixes and operand sizes (including its quirks).
static void decode(u32 a, Ins *I) {
    memset(I, 0, sizeof *I);
    I->addr = a;
    I->base = I->idx = -1;
    u32 p = a;
    int osz = 4, rep = 0;
    for (;;) {
        u8 b = *P(p);
        if (b == 0x66)
            osz = 2;
        else if (b == 0xF3)
            rep = 1;
        else if (b == 0xF2)
            rep = 2;
        else if (b == 0xF0 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x67)
            ;
        else
            break;
        p++;
    }
    I->osz = osz;
    I->rep = rep;
    u8 op = *P(p++);
    u16 op16 = op;
    int m = 0, imm = 0, cls = CL_NAT;
    u8 fr = 0, fw = 0;
    // imm kinds: 1 = imm8 zero-ext, 2 = z (16/32 by opsize), 3 = imm8 sign-ext, 4 = imm32, 5 = imm16
    if (op == 0x0F) {
        u8 o2 = *P(p++);
        op16 = 0x100 | o2;
        if (o2 == 0x38 || o2 == 0x3A) {
            p++;
            m = 1;
            cls = CL_TFB;
        } else if (o2 >= 0x80 && o2 <= 0x8F) {
            I->imm = rd32g(p);
            p += 4;
            I->tgt = p + I->imm;
            cls = CL_JCC;
            fr = CC_FLAGS[(o2 & 15) >> 1];
        } else {
            m = op0f_has_modrm(o2);
            if ((o2 >= 0x70 && o2 <= 0x73) || o2 == 0xA4 || o2 == 0xAC || o2 == 0xBA || o2 == 0xC2 ||
                (o2 >= 0xC4 && o2 <= 0xC6))
                imm = 1;
            if ((o2 >= 0x40 && o2 <= 0x4F) || (o2 >= 0x90 && o2 <= 0x9F))
                fr = CC_FLAGS[(o2 & 15) >> 1];
            else if (o2 == 0xAF)
                fw = F_C | F_O;
            else if (o2 == 0xBC || o2 == 0xBD)
                fw = F_Z;
            else if (o2 == 0xA3 || o2 == 0xAB || o2 == 0xB3 || o2 == 0xBB || o2 == 0xBA)
                fw = F_C;
            else if (o2 == 0xB0 || o2 == 0xB1 || o2 == 0xC0 || o2 == 0xC1 || o2 == 0x2E || o2 == 0x2F)
                fw = F_ALL;
            if (o2 == 0x31 || o2 == 0xA2)
                cls = CL_FB;
        }
    } else if (op < 0x40 && (op & 7) < 6) {
        int aop = op >> 3, form = op & 7;
        if (form < 4)
            m = 1;
        else if (form == 4)
            imm = 1;
        else
            imm = 2;
        fw = F_ALL;
        if (aop == 2 || aop == 3)
            fr = F_C;
    } else if (op >= 0x40 && op <= 0x4F)
        fw = F_NOC;
    else if (op >= 0x50 && op <= 0x5F)
        ;
    else if (op >= 0x70 && op <= 0x7F) {
        i8 d = *P(p++);
        I->tgt = p + d;
        cls = CL_JCC;
        fr = CC_FLAGS[(op & 15) >> 1];
    } else if (op >= 0x91 && op <= 0x97)
        ;
    else if (op >= 0xB0 && op <= 0xB7)
        imm = 1;
    else if (op >= 0xB8 && op <= 0xBF)
        imm = 2;
    else if (op >= 0xA4 && op <= 0xAF && op != 0xA8 && op != 0xA9) {
        cls = CL_FB;
        if ((op & 0xFE) == 0xA6 || (op & 0xFE) == 0xAE) {
            if (!rep)
                fw = F_ALL;
        }
    } else
        switch (op) {
        case 0x68:
            imm = 4;
            break;
        case 0x6A:
            imm = 3;
            break;
        case 0x69:
            m = 1;
            imm = 2;
            fw = F_C | F_O;
            break;
        case 0x6B:
            m = 1;
            imm = 3;
            fw = F_C | F_O;
            break;
        case 0x80:
            m = 1;
            imm = 1;
            fw = F_ALL;
            break;
        case 0x81:
            m = 1;
            imm = 2;
            fw = F_ALL;
            break;
        case 0x83:
            m = 1;
            imm = 3;
            fw = F_ALL;
            break;
        case 0x84:
        case 0x85:
            m = 1;
            fw = F_ALL;
            break;
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8A:
        case 0x8B:
        case 0x8D:
        case 0x8F:
            m = 1;
            break;
        case 0x90:
        case 0x98:
        case 0x99:
        case 0x9B:
            break;
        case 0x9C:
            fr = F_ALL;
            break;
        case 0x9D:
            fw = F_ALL;
            break;
        case 0x9E:
            fw = F_C | F_P | F_Z | F_S;
            break;
        case 0x9F:
            fr = F_C | F_P | F_Z | F_S;
            break;
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
            imm = 4;
            break;
        case 0xA8:
            imm = 1;
            fw = F_ALL;
            break;
        case 0xA9:
            imm = 2;
            fw = F_ALL;
            break;
        case 0xC0:
        case 0xC1:
            m = 1;
            imm = 1;
            break;
        case 0xD0:
        case 0xD1:
        case 0xD2:
        case 0xD3:
            m = 1;
            break;
        case 0xC2:
            imm = 5;
            cls = CL_RET;
            break;
        case 0xC3:
            cls = CL_RET;
            break;
        case 0xC6:
            m = 1;
            imm = 1;
            break;
        case 0xC7:
            m = 1;
            imm = 2;
            break;
        case 0xC9:
            break;
        case 0xE0:
        case 0xE1:
        case 0xE2:
        case 0xE3:
            p++;
            cls = CL_TFB;
            if (op == 0xE0 || op == 0xE1)
                fr = F_Z;
            break;
        case 0xE8:
            I->imm = rd32g(p);
            p += 4;
            I->tgt = p + I->imm;
            cls = CL_CALL;
            break;
        case 0xE9:
            I->imm = rd32g(p);
            p += 4;
            I->tgt = p + I->imm;
            cls = CL_JMP;
            break;
        case 0xEB: {
            i8 d = *P(p++);
            I->tgt = p + d;
            cls = CL_JMP;
            break;
        }
        case 0xF5:
            fr = fw = F_C;
            break;
        case 0xF8:
        case 0xF9:
            fw = F_C;
            break;
        case 0xFA:
        case 0xFB:
        case 0xFC:
        case 0xFD:
            break;
        case 0xF6:
        case 0xF7:
        case 0xFE:
        case 0xFF:
            m = 1;
            break;
        default:
            if (op >= 0xD8 && op <= 0xDF) {
                m = 1;
                cls = CL_FB;
                break;
            }
            cls = CL_TFB; // unknown / dies in exec(): length is irrelevant
            break;
        }
    if (m) {
        u8 b = *P(p++);
        I->hasm = 1;
        I->mod = b >> 6;
        I->rm = b & 7;
        I->reg = (b >> 3) & 7;
        if (I->mod == 3)
            I->isreg = 1;
        else {
            if (I->rm == 4) {
                u8 s = *P(p++);
                I->sc = s >> 6;
                int idx = (s >> 3) & 7, base = s & 7;
                if (base == 5 && I->mod == 0) {
                    I->disp = (i32)rd32g(p);
                    p += 4;
                } else
                    I->base = base;
                if (idx != 4)
                    I->idx = idx;
            } else if (I->rm == 5 && I->mod == 0) {
                I->disp = (i32)rd32g(p);
                p += 4;
            } else
                I->base = I->rm;
            if (I->mod == 1)
                I->disp += (i8)*P(p++);
            else if (I->mod == 2) {
                I->disp += (i32)rd32g(p);
                p += 4;
            }
        }
        if (op == 0xF6 || op == 0xF7) {
            if (I->reg < 2) {
                imm = op == 0xF6 ? 1 : 2;
                fw = F_ALL;
            } else if (I->reg == 3)
                fw = F_ALL;
            else if (I->reg == 4 || I->reg == 5)
                fw = F_C | F_O;
        } else if (op == 0xFE || op == 0xFF) {
            if (I->reg < 2)
                fw = F_NOC;
            else if (op == 0xFF && I->reg == 2)
                cls = CL_CALLI;
            else if (op == 0xFF && I->reg == 4)
                cls = CL_JMPI;
            else if (op == 0xFF && I->reg == 6)
                ;
            else
                cls = CL_TFB;
        } else if (op == 0xC0 || op == 0xC1 || op == 0xD0 || op == 0xD1 || op == 0xD2 || op == 0xD3) {
            int sh = I->reg;
            int cnt = op < 0xD0 ? *P(p) : op < 0xD2 ? 1 : -1; // -1: count in cl
            if (sh == 2 || sh == 3) {
                fr = F_C;
                fw = 0;
            } else if (cnt < 0 || (cnt & 31) == 0)
                fw = 0;
            else
                fw = sh < 2 ? (F_C | F_O) : F_ALL;
        } else if (op == 0x80 || op == 0x81 || op == 0x83) {
            if (I->reg == 2 || I->reg == 3)
                fr = F_C;
        } else if (op >= 0xD8 && op <= 0xDF) {
            if ((op == 0xDA || op == 0xDB) && I->mod == 3 && I->reg < 4)
                fr = F_C | F_Z | F_P; // fcmovcc
        }
    }
    switch (imm) {
    case 1:
        I->imm = *P(p);
        p += 1;
        break;
    case 2:
        if (osz == 2) {
            I->imm = *P(p) | *P(p + 1) << 8;
            p += 2;
        } else {
            I->imm = rd32g(p);
            p += 4;
        }
        break;
    case 3:
        I->imm = (u32)(i32)(i8)*P(p);
        p += 1;
        break;
    case 4:
        I->imm = rd32g(p);
        p += 4;
        break;
    case 5:
        I->imm = *P(p) | *P(p + 1) << 8;
        p += 2;
        break;
    }
    if (op == 0xA0 || op == 0xA1 || op == 0xA2 || op == 0xA3)
        I->disp = (i32)I->imm;
    I->op = op16;
    I->len = (u8)(p - a);
    I->cls = cls;
    I->fr = fr;
    I->fw = fw;
}

// Flags live on entry to guest address a: bounded depth-first walk over the code that follows (treats ret / indirect jumps and
// calls into host thunks as flag-dead, per the ABI; anything it cannot follow is conservatively live).
static u8 fl_in(u32 a, int depth, int *budget) {
    u8 need = 0, killed = 0;
    Ins I;
    for (;;) {
        if (a >= THUNK_BASE)
            return need;
        if (*budget <= 0 || a < 0x1000)
            return need | (F_ALL & ~killed);
        (*budget)--;
        decode(a, &I);
        need |= I.fr & ~killed;
        killed |= I.fw;
        if (killed == F_ALL)
            return need;
        switch (I.cls) {
        case CL_JMP:
        case CL_CALL:
            a = I.tgt;
            continue;
        case CL_JCC: {
            if (depth <= 0)
                return need | (F_ALL & ~killed);
            u8 t = fl_in(I.tgt, depth - 1, budget), f = fl_in(a + I.len, depth - 1, budget);
            return need | ((t | f) & ~killed);
        }
        case CL_RET:
        case CL_JMPI:
        case CL_CALLI:
            return need;
        case CL_TFB:
            return need | (F_ALL & ~killed);
        default:
            a += I.len;
        }
    }
}
static u8 flags_in(u32 a) {
    int budget = 48;
    return fl_in(a, 3, &budget);
}
