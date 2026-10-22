// fpu.h: the x87 FPU for the interpreter.
//
// Registers are host doubles (53-bit mantissa) rather than 80-bit extended values. The control word's rounding-control and
// precision-control fields are honoured; single-precision PC rounds through float.

#define ST(i) (c->st[(c->ftop + (i)) & 7])
static void fpush_(CPU *c, double v) {
    c->ftop = (c->ftop - 1) & 7;
    ST(0) = v;
}
static void fpop_(CPU *c) {
    c->ftop = (c->ftop + 1) & 7;
}

static double ld80(u32 ea) {
    u64 mant = (u64)rd(ea, 4) | (u64)rd(ea + 4, 4) << 32;
    u32 se = rd(ea + 8, 2);
    int e = se & 0x7fff, neg = se >> 15;
    double v;
    if (e == 0 && mant == 0)
        v = 0;
    else if (e == 0x7fff)
        v = (mant << 1) ? NAN : INFINITY;
    else
        v = ldexp((double)mant, e - 16383 - 63);
    return neg ? -v : v;
}
static void st80(u32 ea, double v) {
    u64 mant = 0;
    u32 se = 0;
    if (signbit(v)) {
        se = 0x8000;
        v = -v;
    }
    if (v == 0) {
    } else if (isinf(v)) {
        se |= 0x7fff;
        mant = 1ull << 63;
    } else if (isnan(v)) {
        se |= 0x7fff;
        mant = 3ull << 62;
    } else {
        int e;
        double f = frexp(v, &e);
        mant = (u64)ldexp(f, 64);
        se |= (e - 1 + 16383);
    }
    wr(ea, (u32)mant, 4);
    wr(ea + 4, (u32)(mant >> 32), 4);
    wr(ea + 8, se, 2);
}

static double fround(CPU *c, double x) {
    switch ((c->fcw >> 10) & 3) {
    case 0:
        return nearbyint(x);
    case 1:
        return floor(x);
    case 2:
        return ceil(x);
    default:
        return trunc(x);
    }
}
// x87 precision control (fcw bits 8-9): 0 = 24-bit mantissa, 2 = 53-bit; 3 = 64-bit (approximated by double). Single precision rounds each result to float.
static int fpu_single;                         // set from fcw before each arithmetic op
static double fop(int k, double a, double b) { // a OP b ; k: 0 add 1 mul 4 sub 5 subr 6 div 7 divr
    double r;
    switch (k) {
    case 0:
        r = a + b;
        break;
    case 1:
        r = a * b;
        break;
    case 4:
        r = a - b;
        break;
    case 5:
        r = b - a;
        break;
    case 6:
        r = a / b;
        break;
    default:
        r = b / a;
    }
    return fpu_single ? (double)(float)r : r;
}
static void fcmp(CPU *c, double a, double b) { // sets C3/C2/C0 in the status word
    c->fsw &= ~0x4500;
    if (isnan(a) || isnan(b))
        c->fsw |= 0x4500;
    else if (a < b)
        c->fsw |= 0x0100;
    else if (a == b)
        c->fsw |= 0x4000;
}
static void fcomi(CPU *c, double a, double b) {
    c->of = c->sf = 0;
    if (isnan(a) || isnan(b))
        c->zf = c->pf = c->cf = 1;
    else {
        c->pf = 0;
        c->zf = a == b;
        c->cf = a < b;
    }
}
static u32 fist(CPU *c, double v,
                int bits) { // convert with current rounding; out of range -> "integer indefinite"
    v = fround(c, v);
    if (bits == 16)
        return (v >= -32768 && v <= 32767) ? (u16)(i16)v : 0x8000;
    if (bits == 32)
        return (v >= -2147483648.0 && v <= 2147483647.0) ? (u32)(i32)v : 0x80000000u;
    return 0;
}

static void fpu(CPU *c, u8 op) {
    M m;
    modrm(c, &m);
    int k = m.reg;
    fpu_single = (c->fcw & 0x300) == 0;
    if (!m.isreg) {
        u32 ea = m.ea;
        double v;
        switch (op) {
        case 0xD8:
        case 0xDC:
        case 0xDA:
        case 0xDE: {
            u64 t;
            float f;
            if (op == 0xD8) {
                u32 w = rd(ea, 4);
                memcpy(&f, &w, 4);
                v = f;
            } else if (op == 0xDC) {
                t = (u64)rd(ea, 4) | (u64)rd(ea + 4, 4) << 32;
                memcpy(&v, &t, 8);
            } else if (op == 0xDA)
                v = (i32)rd(ea, 4);
            else
                v = (i16)rd(ea, 2);
        }
            if (k == 2 || k == 3) {
                fcmp(c, ST(0), v);
                if (k == 3)
                    fpop_(c);
            } else
                ST(0) = fop(k, ST(0), v);
            return;
        case 0xD9:
            switch (k) {
            case 0: {
                u32 w = rd(ea, 4);
                float f;
                memcpy(&f, &w, 4);
                fpush_(c, f);
                return;
            }
            case 2:
            case 3: {
                float f = (float)ST(0);
                u32 w;
                memcpy(&w, &f, 4);
                wr(ea, w, 4);
                if (k == 3)
                    fpop_(c);
                return;
            }
            case 5:
                c->fcw = rd(ea, 2);
                return;
            case 7:
                wr(ea, c->fcw, 2);
                return;
            default:
                die("D9 /%d (fldenv/fnstenv) unimplemented", k);
            }
        case 0xDB:
            switch (k) {
            case 0:
                fpush_(c, (i32)rd(ea, 4));
                return;
            case 1:
                wr(ea, (i32)ST(0), 4);
                fpop_(c);
                return; // fisttp
            case 2:
            case 3:
                wr(ea, fist(c, ST(0), 32), 4);
                if (k == 3)
                    fpop_(c);
                return;
            case 5:
                fpush_(c, ld80(ea));
                return;
            case 7:
                st80(ea, ST(0));
                fpop_(c);
                return;
            default:
                die("DB /%d", k);
            }
        case 0xDD:
            switch (k) {
            case 0: {
                u64 t = (u64)rd(ea, 4) | (u64)rd(ea + 4, 4) << 32;
                memcpy(&v, &t, 8);
                fpush_(c, v);
                return;
            }
            case 1: {
                i64 q = (i64)trunc(ST(0));
                wr(ea, (u32)q, 4);
                wr(ea + 4, (u32)((u64)q >> 32), 4);
                fpop_(c);
                return;
            }
            case 2:
            case 3: {
                u64 t;
                v = ST(0);
                memcpy(&t, &v, 8);
                wr(ea, (u32)t, 4);
                wr(ea + 4, (u32)(t >> 32), 4);
                if (k == 3)
                    fpop_(c);
                return;
            }
            case 7:
                wr(ea, (c->fsw & ~0x3800) | (c->ftop << 11), 2);
                return;
            default:
                die("DD /%d (frstor/fnsave) unimplemented", k);
            }
        case 0xDF:
            switch (k) {
            case 0:
                fpush_(c, (i16)rd(ea, 2));
                return;
            case 1:
                wr(ea, (i16)ST(0), 2);
                fpop_(c);
                return;
            case 2:
            case 3:
                wr(ea, fist(c, ST(0), 16), 2);
                if (k == 3)
                    fpop_(c);
                return;
            case 5: {
                i64 q = (i64)(u64)rd(ea, 4) | (i64)((u64)rd(ea + 4, 4) << 32);
                fpush_(c, (double)q);
                return;
            }
            case 7: {
                double r = fround(c, ST(0));
                i64 q = (r >= -9.2233720368547758e18 && r < 9.2233720368547758e18) ? (i64)r : INT64_MIN;
                wr(ea, (u32)q, 4);
                wr(ea + 4, (u32)((u64)q >> 32), 4);
                fpop_(c);
                return;
            }
            default:
                die("DF /%d (fbld/fbstp)", k);
            }
        }
        die("x87 mem op %02x /%d", op, k);
    }
    int i = m.rm;
    u8 mb = 0xC0 | (k << 3) | i;
    switch (op) {
    case 0xD8:
        if (k == 2 || k == 3) {
            fcmp(c, ST(0), ST(i));
            if (k == 3)
                fpop_(c);
        } else
            ST(0) = fop(k, ST(0), ST(i));
        return;
    case 0xDC: {
        int kk = k == 4 ? 5 : k == 5 ? 4 : k == 6 ? 7 : k == 7 ? 6 : k;
        if (k == 2 || k == 3)
            die("DC com");
        ST(i) = fop(kk, ST(i), ST(0));
        return;
    }
    case 0xDE:
        if (mb == 0xD9) {
            fcmp(c, ST(0), ST(1));
            fpop_(c);
            fpop_(c);
            return;
        }
        {
            int kk = k == 4 ? 5 : k == 5 ? 4 : k == 6 ? 7 : k == 7 ? 6 : k;
            if (k == 2 || k == 3)
                die("DE %02x", mb);
            ST(i) = fop(kk, ST(i), ST(0));
            fpop_(c);
            return;
        }
    case 0xD9:
        if (k == 0) {
            double v = ST(i);
            fpush_(c, v);
            return;
        }
        if (k == 1) {
            double t = ST(0);
            ST(0) = ST(i);
            ST(i) = t;
            return;
        }
        switch (mb) {
        case 0xD0:
            return;
        case 0xE0:
            ST(0) = -ST(0);
            return;
        case 0xE1:
            ST(0) = fabs(ST(0));
            return;
        case 0xE4:
            fcmp(c, ST(0), 0.0);
            return;
        case 0xE5: {
            double v = ST(0);
            c->fsw &= ~0x4700;
            if (isnan(v))
                c->fsw |= 0x100;
            else if (isinf(v))
                c->fsw |= 0x500;
            else if (v == 0)
                c->fsw |= 0x4000;
            else
                c->fsw |= 0x400;
            if (signbit(v))
                c->fsw |= 0x200;
            return;
        }
        case 0xE8:
            fpush_(c, 1.0);
            return;
        case 0xE9:
            fpush_(c, M_LN10 / M_LN2);
            return;
        case 0xEA:
            fpush_(c, M_LOG2E);
            return;
        case 0xEB:
            fpush_(c, M_PI);
            return;
        case 0xEC:
            fpush_(c, M_LN2 / M_LN10);
            return;
        case 0xED:
            fpush_(c, M_LN2);
            return;
        case 0xEE:
            fpush_(c, 0.0);
            return;
        case 0xF0:
            ST(0) = exp2(ST(0)) - 1;
            return;
        case 0xF1:
            ST(1) = ST(1) * log2(ST(0));
            fpop_(c);
            return;
        case 0xF2:
            ST(0) = tan(ST(0));
            fpush_(c, 1.0);
            c->fsw &= ~0x400;
            return;
        case 0xF3:
            ST(1) = atan2(ST(1), ST(0));
            fpop_(c);
            return;
        case 0xF5:
            ST(0) = remainder(ST(0), ST(1));
            c->fsw &= ~0x400;
            return;
        case 0xF6:
            c->ftop = (c->ftop - 1) & 7;
            return;
        case 0xF7:
            c->ftop = (c->ftop + 1) & 7;
            return;
        case 0xF8:
            ST(0) = fmod(ST(0), ST(1));
            c->fsw &= ~0x400;
            return;
        case 0xF9:
            ST(1) = ST(1) * log2(ST(0) + 1);
            fpop_(c);
            return;
        case 0xFA:
            ST(0) = fpu_single ? (double)sqrtf((float)ST(0)) : sqrt(ST(0));
            return;
        case 0xFB: {
            double a = ST(0);
            ST(0) = sin(a);
            fpush_(c, cos(a));
            c->fsw &= ~0x400;
            return;
        }
        case 0xFC:
            ST(0) = fround(c, ST(0));
            return;
        case 0xFD:
            ST(0) = ldexp(ST(0), (int)trunc(ST(1)));
            return;
        case 0xFE:
            ST(0) = sin(ST(0));
            c->fsw &= ~0x400;
            return;
        case 0xFF:
            ST(0) = cos(ST(0));
            c->fsw &= ~0x400;
            return;
        }
        die("D9 %02x", mb);
    case 0xDA:
        if (mb == 0xE9) {
            fcmp(c, ST(0), ST(1));
            fpop_(c);
            fpop_(c);
            return;
        }
        if (k < 4) {
            int t = k == 0 ? c->cf : k == 1 ? c->zf : k == 2 ? (c->cf || c->zf) : c->pf;
            if (t)
                ST(0) = ST(i);
            return;
        }
        die("DA %02x", mb);
    case 0xDB:
        if (k < 4) {
            int t = k == 0 ? c->cf : k == 1 ? c->zf : k == 2 ? (c->cf || c->zf) : c->pf;
            if (!t)
                ST(0) = ST(i);
            return;
        }
        if (mb == 0xE2) {
            c->fsw &= ~0x80ff;
            return;
        }
        if (mb == 0xE3) {
            c->fcw = 0x37f;
            c->fsw = 0;
            c->ftop = 0;
            return;
        }
        if (k == 5 || k == 6) {
            fcomi(c, ST(0), ST(i));
            return;
        }
        die("DB %02x", mb);
    case 0xDD:
        if (k == 0)
            return; // ffree
        if (k == 2 || k == 3) {
            ST(i) = ST(0);
            if (k == 3)
                fpop_(c);
            return;
        }
        if (k == 4 || k == 5) {
            fcmp(c, ST(0), ST(i));
            if (k == 5)
                fpop_(c);
            return;
        }
        die("DD %02x", mb);
    case 0xDF:
        if (mb == 0xE0) {
            rset(c, EAX, (c->fsw & ~0x3800) | (c->ftop << 11), 2);
            return;
        }
        if (k == 5 || k == 6) {
            fcomi(c, ST(0), ST(i));
            fpop_(c);
            return;
        }
        die("DF %02x", mb);
    }
}
