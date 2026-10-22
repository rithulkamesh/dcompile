// sse.h: SSE / SSE2 / SSE3 for the interpreter.
//
// sse() decodes the 0F-opcode space for the vector unit; pfx selects the mandatory prefix (0 none, 1 = 66, 2 = F3, 3 = F2).
// It returns 0 without consuming bytes when the opcode is not an SSE one. Rounding honours MXCSR.RC.

#include <math.h>
#include <fenv.h>

static void xld(CPU *c, M *m, X128 *o, int n) { // read n bytes of operand, zero-extended to 128
    if (m->isreg) {
        *o = c->xmm[m->rm];
        return;
    }
    memset(o, 0, 16);
    if (m->ea < 0x1000)
        die("null read %08x", m->ea);
    memcpy(o, P(m->ea), n);
}
static void xst(CPU *c, M *m, const X128 *v,
                int n) { // write low n bytes of v (register: merge into low n bytes)
    if (m->isreg) {
        memcpy(&c->xmm[m->rm], v, n);
        return;
    }
    if (m->ea < 0x1000)
        die("null write %08x", m->ea);
    if (__builtin_expect(wlog_on_, 0))
        wlog_note(m->ea, n);
    memcpy(P(m->ea), v, n);
}

// MXCSR.RC (bits 13-14): 0 nearest-even, 1 down, 2 up, 3 truncate. Governs cvtss2si / cvtsd2si / cvtps2dq / cvtpd2dq.
static double mx_round(CPU *c, double x) {
    switch ((c->mxcsr >> 13) & 3) {
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
static int sse_is(u8 op) {
    return (op >= 0x10 && op <= 0x17) || (op >= 0x28 && op <= 0x2F) || (op >= 0x50 && op <= 0x7F) ||
           op == 0xAE || (op >= 0xC2 && op <= 0xC6 && op != 0xC3) || (op >= 0xD0 && op <= 0xFE);
}

#define LANES(T, f, n, expr)                                                                                 \
    do {                                                                                                     \
        for (int i_ = 0; i_ < (n); i_++) {                                                                   \
            T a = d.f[i_], b = s.f[i_];                                                                      \
            (void)a;                                                                                         \
            (void)b;                                                                                         \
            d.f[i_] = (expr);                                                                                \
        }                                                                                                    \
    } while (0)

static int sse(CPU *c, u8 op, int pfx) {
    if (!sse_is(op))
        return 0;
    M m;
    X128 d, s, r;
    int imm;
    if (op == 0xAE) {
        modrm(c, &m);
        if (m.isreg)
            return 1; // lfence / mfence / sfence
        if (m.reg == 2)
            c->mxcsr = rd(m.ea, 4);
        else if (m.reg == 3)
            wr(m.ea, c->mxcsr, 4);
        else if (m.reg != 7)
            die("0F AE /%d (fxsave/fxrstor) unimplemented", m.reg);
        return 1;
    }
    if (op == 0x77)
        return 1;
    modrm(c, &m);
    d = c->xmm[m.reg];
    X128 *D = &c->xmm[m.reg];
    int n = (pfx == 2) ? 4 : (pfx == 3) ? 8 : 16; // memory operand size for scalar forms
    switch (op) {
    // ---- moves ----
    case 0x10:
        xld(c, &m, &s, n);
        if (pfx == 2 || pfx == 3) {
            if (m.isreg) {
                memcpy(D, &s, n);
            } else {
                memset(D, 0, 16);
                memcpy(D, &s, n);
            }
        } else
            *D = s;
        return 1;
    case 0x11:
        if (pfx == 2 || pfx == 3)
            xst(c, &m, &d, n);
        else
            xst(c, &m, &d, 16);
        return 1;
    case 0x12:
        if (pfx == 3) {
            xld(c, &m, &s, 8);
            D->q[0] = D->q[1] = s.q[0];
            return 1;
        }
        if (pfx == 2) {
            xld(c, &m, &s, 16);
            D->d[0] = D->d[1] = s.d[0];
            D->d[2] = D->d[3] = s.d[2];
            return 1;
        }
        if (m.isreg)
            D->q[0] = c->xmm[m.rm].q[1];
        else
            D->q[0] = rd(m.ea, 4) | (u64)rd(m.ea + 4, 4) << 32;
        return 1;
    case 0x13:
        xst(c, &m, &d, 8);
        return 1;
    case 0x14:
        xld(c, &m, &s, 16);
        if (pfx == 1) {
            r.q[0] = d.q[0];
            r.q[1] = s.q[0];
        } else {
            r.d[0] = d.d[0];
            r.d[1] = s.d[0];
            r.d[2] = d.d[1];
            r.d[3] = s.d[1];
        }
        *D = r;
        return 1;
    case 0x15:
        xld(c, &m, &s, 16);
        if (pfx == 1) {
            r.q[0] = d.q[1];
            r.q[1] = s.q[1];
        } else {
            r.d[0] = d.d[2];
            r.d[1] = s.d[2];
            r.d[2] = d.d[3];
            r.d[3] = s.d[3];
        }
        *D = r;
        return 1;
    case 0x16:
        if (pfx == 2) {
            xld(c, &m, &s, 16);
            D->d[0] = D->d[1] = s.d[1];
            D->d[2] = D->d[3] = s.d[3];
            return 1;
        }
        if (m.isreg)
            D->q[1] = c->xmm[m.rm].q[0];
        else
            D->q[1] = rd(m.ea, 4) | (u64)rd(m.ea + 4, 4) << 32;
        return 1;
    case 0x17: {
        X128 t;
        t.q[0] = d.q[1];
        xst(c, &m, &t, 8);
        return 1;
    }
    case 0x28:
        xld(c, &m, &s, 16);
        *D = s;
        return 1;
    case 0x29:
        xst(c, &m, &d, 16);
        return 1;
    case 0x2A: {
        i32 v = m.isreg ? (i32)c->r[m.rm] : (i32)rd(m.ea, 4);
        if (pfx == 2)
            D->f[0] = (float)v;
        else if (pfx == 3)
            D->df[0] = (double)v;
        else
            die("cvtpi2ps (MMX)");
        return 1;
    }
    case 0x2C:
    case 0x2D: {
        xld(c, &m, &s, n);
        double v = pfx == 2 ? s.f[0] : s.df[0];
        c->r[m.reg] = (u32)(i32)(op == 0x2C ? trunc(v) : mx_round(c, v));
        return 1;
    }
    case 0x2E:
    case 0x2F: {
        xld(c, &m, &s, pfx == 1 ? 8 : 4);
        double a = pfx == 1 ? d.df[0] : d.f[0], b = pfx == 1 ? s.df[0] : s.f[0];
        c->of = c->sf = 0;
        if (isnan(a) || isnan(b)) {
            c->zf = c->pf = c->cf = 1;
        } else {
            c->pf = 0;
            c->zf = a == b;
            c->cf = a < b;
        }
        return 1;
    }
    case 0x50: {
        if (!m.isreg)
            die("movmskps mem");
        s = c->xmm[m.rm];
        u32 v = 0;
        if (pfx == 1)
            v = (s.q[0] >> 63) | ((s.q[1] >> 63) << 1);
        else
            for (int i = 0; i < 4; i++)
                v |= (s.d[i] >> 31) << i;
        c->r[m.reg] = v;
        return 1;
    }
    // ---- arithmetic: ps / pd / ss / sd ----
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x58:
    case 0x59:
    case 0x5C:
    case 0x5D:
    case 0x5E:
    case 0x5F: {
        xld(c, &m, &s, n);
        d = *D;
        int lanes = pfx == 0 ? 4 : pfx == 1 ? 2 : 1, dbl = (pfx == 1 || pfx == 3);
        for (int i = 0; i < lanes; i++) {
            if (dbl) {
                double a = d.df[i], b = s.df[i], x = 0;
                switch (op) {
                case 0x51:
                    x = sqrt(b);
                    break;
                case 0x58:
                    x = a + b;
                    break;
                case 0x59:
                    x = a * b;
                    break;
                case 0x5C:
                    x = a - b;
                    break;
                case 0x5D:
                    x = a < b ? a : b;
                    break;
                case 0x5E:
                    x = a / b;
                    break;
                case 0x5F:
                    x = a > b ? a : b;
                    break;
                default:
                    die("sse %02x pd", op);
                }
                d.df[i] = x;
            } else {
                float a = d.f[i], b = s.f[i], x = 0;
                switch (op) {
                case 0x51:
                    x = sqrtf(b);
                    break;
                case 0x52:
                    x = 1.0f / sqrtf(b);
                    break;
                case 0x53:
                    x = 1.0f / b;
                    break;
                case 0x58:
                    x = a + b;
                    break;
                case 0x59:
                    x = a * b;
                    break;
                case 0x5C:
                    x = a - b;
                    break;
                case 0x5D:
                    x = a < b ? a : b;
                    break;
                case 0x5E:
                    x = a / b;
                    break;
                default:
                    x = a > b ? a : b;
                    break;
                }
                d.f[i] = x;
            }
        }
        *D = d;
        return 1;
    }
    case 0x54:
        xld(c, &m, &s, 16);
        D->q[0] &= s.q[0];
        D->q[1] &= s.q[1];
        return 1;
    case 0x55:
        xld(c, &m, &s, 16);
        D->q[0] = ~D->q[0] & s.q[0];
        D->q[1] = ~D->q[1] & s.q[1];
        return 1;
    case 0x56:
        xld(c, &m, &s, 16);
        D->q[0] |= s.q[0];
        D->q[1] |= s.q[1];
        return 1;
    case 0x57:
        xld(c, &m, &s, 16);
        D->q[0] ^= s.q[0];
        D->q[1] ^= s.q[1];
        return 1;
    case 0x5A:
        xld(c, &m, &s, pfx == 0 ? 8 : pfx == 2 ? 4 : 16);
        if (pfx == 0) {
            double a = s.f[0], b = s.f[1];
            D->df[0] = a;
            D->df[1] = b;
        } else if (pfx == 1) {
            float a = (float)s.df[0], b = (float)s.df[1];
            D->f[0] = a;
            D->f[1] = b;
            D->q[1] = 0;
        } else if (pfx == 2)
            D->df[0] = (double)s.f[0];
        else
            D->f[0] = (float)s.df[0];
        return 1;
    case 0x5B:
        xld(c, &m, &s, 16);
        for (int i = 0; i < 4; i++) {
            if (pfx == 0)
                D->f[i] = (float)s.sd[i];
            else
                D->sd[i] = pfx == 1 ? (i32)mx_round(c, s.f[i]) : (i32)truncf(s.f[i]);
        }
        return 1;
    case 0xE6:
        xld(c, &m, &s, 16);
        if (pfx == 2) {
            double a = s.sd[0], b = s.sd[1];
            D->df[0] = a;
            D->df[1] = b;
        } else {
            i32 a = (i32)(pfx == 1 ? trunc(s.df[0]) : mx_round(c, s.df[0])),
                b = (i32)(pfx == 1 ? trunc(s.df[1]) : mx_round(c, s.df[1]));
            D->sd[0] = a;
            D->sd[1] = b;
            D->q[1] = 0;
        }
        return 1;
    case 0xC2: {
        xld(c, &m, &s, n);
        imm = f8(c) & 7;
        d = *D;
        int lanes = pfx == 0 ? 4 : pfx == 1 ? 2 : 1, dbl = (pfx == 1 || pfx == 3);
        for (int i = 0; i < lanes; i++) {
            double a = dbl ? d.df[i] : d.f[i], b = dbl ? s.df[i] : s.f[i];
            int t;
            switch (imm) {
            case 0:
                t = a == b;
                break;
            case 1:
                t = a < b;
                break;
            case 2:
                t = a <= b;
                break;
            case 3:
                t = isnan(a) || isnan(b);
                break;
            case 4:
                t = !(a == b);
                break;
            case 5:
                t = !(a < b);
                break;
            case 6:
                t = !(a <= b);
                break;
            default:
                t = !(isnan(a) || isnan(b));
            }
            if (dbl)
                d.q[i] = t ? ~0ull : 0;
            else
                d.d[i] = t ? ~0u : 0;
        }
        *D = d;
        return 1;
    }
    case 0xC6:
        xld(c, &m, &s, 16);
        imm = f8(c);
        if (pfx == 1) {
            r.q[0] = d.q[imm & 1];
            r.q[1] = s.q[(imm >> 1) & 1];
        } else {
            r.d[0] = d.d[imm & 3];
            r.d[1] = d.d[(imm >> 2) & 3];
            r.d[2] = s.d[(imm >> 4) & 3];
            r.d[3] = s.d[(imm >> 6) & 3];
        }
        *D = r;
        return 1;
    case 0xC4:
        imm = 0;
        {
            u32 v = m.isreg ? c->r[m.rm] : rd(m.ea, 2);
            imm = f8(c) & 7;
            D->w[imm] = (u16)v;
        }
        return 1;
    case 0xC5: {
        if (!m.isreg)
            die("pextrw mem");
        imm = f8(c) & 7;
        c->r[m.reg] = c->xmm[m.rm].w[imm];
        return 1;
    }
    // ---- SSE3 horizontal / addsub / lddqu ----
    case 0x7C:
    case 0x7D: {
        xld(c, &m, &s, 16);
        d = *D;
        int sub = op == 0x7D;
        if (pfx == 1) {
            r.df[0] = sub ? d.df[0] - d.df[1] : d.df[0] + d.df[1];
            r.df[1] = sub ? s.df[0] - s.df[1] : s.df[0] + s.df[1];
        } else {
            r.f[0] = sub ? d.f[0] - d.f[1] : d.f[0] + d.f[1];
            r.f[1] = sub ? d.f[2] - d.f[3] : d.f[2] + d.f[3];
            r.f[2] = sub ? s.f[0] - s.f[1] : s.f[0] + s.f[1];
            r.f[3] = sub ? s.f[2] - s.f[3] : s.f[2] + s.f[3];
        }
        *D = r;
        return 1;
    }
    case 0xD0: {
        xld(c, &m, &s, 16);
        d = *D;
        if (pfx == 1) {
            r.df[0] = d.df[0] - s.df[0];
            r.df[1] = d.df[1] + s.df[1];
        } else
            for (int i = 0; i < 4; i++)
                r.f[i] = (i & 1) ? d.f[i] + s.f[i] : d.f[i] - s.f[i];
        *D = r;
        return 1;
    }
    case 0xF0:
        xld(c, &m, &s, 16);
        *D = s;
        return 1;
    // ---- integer moves ----
    case 0x6E: {
        u32 v = m.isreg ? c->r[m.rm] : rd(m.ea, 4);
        memset(D, 0, 16);
        D->d[0] = v;
        return 1;
    }
    case 0x7E:
        if (pfx == 2) {
            xld(c, &m, &s, 8);
            D->q[0] = s.q[0];
            D->q[1] = 0;
            return 1;
        }
        if (m.isreg)
            c->r[m.rm] = d.d[0];
        else
            wr(m.ea, d.d[0], 4);
        return 1;
    case 0xD6:
        if (m.isreg) {
            c->xmm[m.rm].q[0] = d.q[0];
            c->xmm[m.rm].q[1] = 0;
        } else {
            if (__builtin_expect(wlog_on_, 0))
                wlog_note(m.ea, 8);
            memcpy(P(m.ea), &d, 8);
        }
        return 1;
    case 0x6F:
        xld(c, &m, &s, 16);
        *D = s;
        return 1;
    case 0x7F:
        xst(c, &m, &d, 16);
        return 1;
    case 0xD7: {
        if (!m.isreg)
            die("pmovmskb mem");
        s = c->xmm[m.rm];
        u32 v = 0;
        for (int i = 0; i < 16; i++)
            v |= (u32)(s.b[i] >> 7) << i;
        c->r[m.reg] = v;
        return 1;
    }
    case 0x70:
        xld(c, &m, &s, 16);
        imm = f8(c);
        r = s;
        if (pfx == 1)
            for (int i = 0; i < 4; i++)
                r.d[i] = s.d[(imm >> (2 * i)) & 3];
        else if (pfx == 3)
            for (int i = 0; i < 4; i++)
                r.w[i] = s.w[(imm >> (2 * i)) & 3];
        else
            for (int i = 0; i < 4; i++)
                r.w[4 + i] = s.w[4 + ((imm >> (2 * i)) & 3)];
        *D = r;
        return 1;
    case 0x71:
    case 0x72:
    case 0x73: {
        imm = f8(c);
        X128 *x = &c->xmm[m.rm];
        int k = m.reg;
        for (int i = 0; i < 16 / (op == 0x71 ? 2 : op == 0x72 ? 4 : 8); i++) {
            if (op == 0x71) {
                u16 v = x->w[i];
                x->w[i] = k == 2   ? (imm > 15 ? 0 : v >> imm)
                          : k == 4 ? (u16)((i16)v >> (imm > 15 ? 15 : imm))
                                   : (imm > 15 ? 0 : v << imm);
            } else if (op == 0x72) {
                u32 v = x->d[i];
                x->d[i] = k == 2   ? (imm > 31 ? 0 : v >> imm)
                          : k == 4 ? (u32)((i32)v >> (imm > 31 ? 31 : imm))
                                   : (imm > 31 ? 0 : v << imm);
            } else if (k == 2 || k == 6) {
                u64 v = x->q[i];
                x->q[i] = k == 2 ? (imm > 63 ? 0 : v >> imm) : (imm > 63 ? 0 : v << imm);
            }
        }
        if (op == 0x73 && (k == 3 || k == 7)) {
            u8 t[32] = {0};
            int sh = imm > 16 ? 16 : imm;
            if (k == 3) {
                memcpy(t, x->b, 16);
                memset(x->b, 0, 16);
                memcpy(x->b, t + sh, 16 - sh);
            } else {
                memcpy(t + 16, x->b, 16);
                memcpy(x->b, t + 16 - sh, 16);
            }
        }
        return 1;
    }
    }
    // ---- SSE2 integer binary ops: dest = f(dest, src) ----
    xld(c, &m, &s, 16);
    d = *D;
    r = d;
    switch (op) {
    case 0x60:
        for (int i = 0; i < 8; i++) {
            r.b[2 * i] = d.b[i];
            r.b[2 * i + 1] = s.b[i];
        }
        break;
    case 0x61:
        for (int i = 0; i < 4; i++) {
            r.w[2 * i] = d.w[i];
            r.w[2 * i + 1] = s.w[i];
        }
        break;
    case 0x62:
        r.d[0] = d.d[0];
        r.d[1] = s.d[0];
        r.d[2] = d.d[1];
        r.d[3] = s.d[1];
        break;
    case 0x68:
        for (int i = 0; i < 8; i++) {
            r.b[2 * i] = d.b[8 + i];
            r.b[2 * i + 1] = s.b[8 + i];
        }
        break;
    case 0x69:
        for (int i = 0; i < 4; i++) {
            r.w[2 * i] = d.w[4 + i];
            r.w[2 * i + 1] = s.w[4 + i];
        }
        break;
    case 0x6A:
        r.d[0] = d.d[2];
        r.d[1] = s.d[2];
        r.d[2] = d.d[3];
        r.d[3] = s.d[3];
        break;
    case 0x6C:
        r.q[0] = d.q[0];
        r.q[1] = s.q[0];
        break;
    case 0x6D:
        r.q[0] = d.q[1];
        r.q[1] = s.q[1];
        break;
    case 0x63:
        for (int i = 0; i < 8; i++) {
            int v = d.sw[i];
            r.b[i] = v > 127 ? 127 : v < -128 ? 0x80 : v;
            v = s.sw[i];
            r.b[8 + i] = v > 127 ? 127 : v < -128 ? 0x80 : v;
        }
        break;
    case 0x67:
        for (int i = 0; i < 8; i++) {
            int v = d.sw[i];
            r.b[i] = v > 255 ? 255 : v < 0 ? 0 : v;
            v = s.sw[i];
            r.b[8 + i] = v > 255 ? 255 : v < 0 ? 0 : v;
        }
        break;
    case 0x6B:
        for (int i = 0; i < 4; i++) {
            i64 v = d.sd[i];
            r.sw[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            v = s.sd[i];
            r.sw[4 + i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        }
        break;
    case 0x64:
        for (int i = 0; i < 16; i++)
            r.b[i] = (i8)d.b[i] > (i8)s.b[i] ? 0xff : 0;
        break;
    case 0x65:
        for (int i = 0; i < 8; i++)
            r.w[i] = d.sw[i] > s.sw[i] ? 0xffff : 0;
        break;
    case 0x66:
        for (int i = 0; i < 4; i++)
            r.d[i] = d.sd[i] > s.sd[i] ? ~0u : 0;
        break;
    case 0x74:
        for (int i = 0; i < 16; i++)
            r.b[i] = d.b[i] == s.b[i] ? 0xff : 0;
        break;
    case 0x75:
        for (int i = 0; i < 8; i++)
            r.w[i] = d.w[i] == s.w[i] ? 0xffff : 0;
        break;
    case 0x76:
        for (int i = 0; i < 4; i++)
            r.d[i] = d.d[i] == s.d[i] ? ~0u : 0;
        break;
    case 0xD4:
        r.q[0] = d.q[0] + s.q[0];
        r.q[1] = d.q[1] + s.q[1];
        break;
    case 0xFB:
        r.q[0] = d.q[0] - s.q[0];
        r.q[1] = d.q[1] - s.q[1];
        break;
    case 0xFC:
        for (int i = 0; i < 16; i++)
            r.b[i] = d.b[i] + s.b[i];
        break;
    case 0xFD:
        for (int i = 0; i < 8; i++)
            r.w[i] = d.w[i] + s.w[i];
        break;
    case 0xFE:
        for (int i = 0; i < 4; i++)
            r.d[i] = d.d[i] + s.d[i];
        break;
    case 0xF8:
        for (int i = 0; i < 16; i++)
            r.b[i] = d.b[i] - s.b[i];
        break;
    case 0xF9:
        for (int i = 0; i < 8; i++)
            r.w[i] = d.w[i] - s.w[i];
        break;
    case 0xFA:
        for (int i = 0; i < 4; i++)
            r.d[i] = d.d[i] - s.d[i];
        break;
    case 0xD5:
        for (int i = 0; i < 8; i++)
            r.w[i] = d.w[i] * s.w[i];
        break;
    case 0xE5:
        for (int i = 0; i < 8; i++)
            r.sw[i] = ((i32)d.sw[i] * s.sw[i]) >> 16;
        break;
    case 0xE4:
        for (int i = 0; i < 8; i++)
            r.w[i] = ((u32)d.w[i] * s.w[i]) >> 16;
        break;
    case 0xF4:
        r.q[0] = (u64)d.d[0] * s.d[0];
        r.q[1] = (u64)d.d[2] * s.d[2];
        break;
    case 0xF5:
        for (int i = 0; i < 4; i++)
            r.sd[i] = d.sw[2 * i] * s.sw[2 * i] + d.sw[2 * i + 1] * s.sw[2 * i + 1];
        break;
    case 0xDB:
        r.q[0] = d.q[0] & s.q[0];
        r.q[1] = d.q[1] & s.q[1];
        break;
    case 0xDF:
        r.q[0] = ~d.q[0] & s.q[0];
        r.q[1] = ~d.q[1] & s.q[1];
        break;
    case 0xEB:
        r.q[0] = d.q[0] | s.q[0];
        r.q[1] = d.q[1] | s.q[1];
        break;
    case 0xEF:
        r.q[0] = d.q[0] ^ s.q[0];
        r.q[1] = d.q[1] ^ s.q[1];
        break;
    case 0xDA:
        for (int i = 0; i < 16; i++)
            r.b[i] = d.b[i] < s.b[i] ? d.b[i] : s.b[i];
        break;
    case 0xDE:
        for (int i = 0; i < 16; i++)
            r.b[i] = d.b[i] > s.b[i] ? d.b[i] : s.b[i];
        break;
    case 0xEA:
        for (int i = 0; i < 8; i++)
            r.sw[i] = d.sw[i] < s.sw[i] ? d.sw[i] : s.sw[i];
        break;
    case 0xEE:
        for (int i = 0; i < 8; i++)
            r.sw[i] = d.sw[i] > s.sw[i] ? d.sw[i] : s.sw[i];
        break;
    case 0xD8:
        for (int i = 0; i < 16; i++)
            r.b[i] = d.b[i] > s.b[i] ? d.b[i] - s.b[i] : 0;
        break;
    case 0xD9:
        for (int i = 0; i < 8; i++)
            r.w[i] = d.w[i] > s.w[i] ? d.w[i] - s.w[i] : 0;
        break;
    case 0xDC:
        for (int i = 0; i < 16; i++) {
            int v = d.b[i] + s.b[i];
            r.b[i] = v > 255 ? 255 : v;
        }
        break;
    case 0xDD:
        for (int i = 0; i < 8; i++) {
            int v = d.w[i] + s.w[i];
            r.w[i] = v > 65535 ? 65535 : v;
        }
        break;
    case 0xE8:
        for (int i = 0; i < 16; i++) {
            int v = (i8)d.b[i] - (i8)s.b[i];
            r.b[i] = v > 127 ? 127 : v < -128 ? 0x80 : v;
        }
        break;
    case 0xE9:
        for (int i = 0; i < 8; i++) {
            int v = d.sw[i] - s.sw[i];
            r.sw[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        }
        break;
    case 0xEC:
        for (int i = 0; i < 16; i++) {
            int v = (i8)d.b[i] + (i8)s.b[i];
            r.b[i] = v > 127 ? 127 : v < -128 ? 0x80 : v;
        }
        break;
    case 0xED:
        for (int i = 0; i < 8; i++) {
            int v = d.sw[i] + s.sw[i];
            r.sw[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        }
        break;
    case 0xE0:
        for (int i = 0; i < 16; i++)
            r.b[i] = (d.b[i] + s.b[i] + 1) >> 1;
        break;
    case 0xE3:
        for (int i = 0; i < 8; i++)
            r.w[i] = (d.w[i] + s.w[i] + 1) >> 1;
        break;
    case 0xD1: {
        u64 k = s.q[0];
        for (int i = 0; i < 8; i++)
            r.w[i] = k > 15 ? 0 : d.w[i] >> k;
    } break;
    case 0xD2: {
        u64 k = s.q[0];
        for (int i = 0; i < 4; i++)
            r.d[i] = k > 31 ? 0 : d.d[i] >> k;
    } break;
    case 0xD3: {
        u64 k = s.q[0];
        for (int i = 0; i < 2; i++)
            r.q[i] = k > 63 ? 0 : d.q[i] >> k;
    } break;
    case 0xE1: {
        u64 k = s.q[0] > 15 ? 15 : s.q[0];
        for (int i = 0; i < 8; i++)
            r.sw[i] = d.sw[i] >> k;
    } break;
    case 0xE2: {
        u64 k = s.q[0] > 31 ? 31 : s.q[0];
        for (int i = 0; i < 4; i++)
            r.sd[i] = d.sd[i] >> k;
    } break;
    case 0xF1: {
        u64 k = s.q[0];
        for (int i = 0; i < 8; i++)
            r.w[i] = k > 15 ? 0 : d.w[i] << k;
    } break;
    case 0xF2: {
        u64 k = s.q[0];
        for (int i = 0; i < 4; i++)
            r.d[i] = k > 31 ? 0 : d.d[i] << k;
    } break;
    case 0xF3: {
        u64 k = s.q[0];
        for (int i = 0; i < 2; i++)
            r.q[i] = k > 63 ? 0 : d.q[i] << k;
    } break;
    default:
        die("unimplemented SSE 0F %02x (pfx %d)", op, pfx);
    }
    if (pfx != 1)
        die("MMX form of 0F %02x unimplemented", op);
    *D = r;
    return 1;
}
