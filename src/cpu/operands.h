// operands.h: x86 operand decoding and the integer ALU.
//
// ModRM/SIB decoding, register access at 8/16/32 bits, flag computation (lazy-free: flags are materialised eagerly in the
// CPU struct), shifts/rotates, condition codes and the string instructions. Shared by the interpreter and the JIT fallback.

typedef struct {
    int isreg, reg, rm;
    u32 ea;
} M;
static u32 r8get(CPU *c, int i) {
    return i < 4 ? c->r[i] & 0xff : (c->r[i - 4] >> 8) & 0xff;
}
static void r8set(CPU *c, int i, u32 v) {
    if (i < 4)
        c->r[i] = (c->r[i] & ~0xffu) | (v & 0xff);
    else
        c->r[i - 4] = (c->r[i - 4] & ~0xff00u) | ((v & 0xff) << 8);
}
static u32 rget(CPU *c, int i, int sz) {
    return sz == 1 ? r8get(c, i) : sz == 2 ? c->r[i] & 0xffff : c->r[i];
}
static void rset(CPU *c, int i, u32 v, int sz) {
    if (sz == 1)
        r8set(c, i, v);
    else if (sz == 2)
        c->r[i] = (c->r[i] & 0xffff0000u) | (v & 0xffff);
    else
        c->r[i] = v;
}
static u32 mget(CPU *c, M *m, int sz) {
    return m->isreg ? rget(c, m->rm, sz) : rd(m->ea, sz);
}
static void mset(CPU *c, M *m, u32 v, int sz) {
    if (m->isreg)
        rset(c, m->rm, v, sz);
    else
        wr(m->ea, v, sz);
}

static void modrm(CPU *c, M *m) {
    u8 b = f8(c);
    int mod = b >> 6, rm = b & 7;
    m->reg = (b >> 3) & 7;
    if (mod == 3) {
        m->isreg = 1;
        m->rm = rm;
        return;
    }
    m->isreg = 0;
    u32 ea;
    if (rm == 4) {
        u8 s = f8(c);
        int sc = s >> 6, idx = (s >> 3) & 7, base = s & 7;
        ea = (base == 5 && mod == 0) ? f32(c) : c->r[base];
        if (idx != 4)
            ea += c->r[idx] << sc;
    } else if (rm == 5 && mod == 0)
        ea = f32(c);
    else
        ea = c->r[rm];
    if (mod == 1)
        ea += (i8)f8(c);
    else if (mod == 2)
        ea += f32(c);
    m->ea = ea;
}

static inline u32 mask_of(int sz) {
    return sz == 4 ? 0xffffffffu : sz == 2 ? 0xffffu : 0xffu;
}
static inline i32 sx(u32 v, int sz) {
    return sz == 1 ? (i8)v : sz == 2 ? (i16)v : (i32)v;
}
static void zsp(CPU *c, u32 r, int sz) {
    c->zf = r == 0;
    c->sf = (r >> (sz * 8 - 1)) & 1;
    c->pf = !__builtin_parity(r & 0xff);
}

// op: 0 add 1 or 2 adc 3 sbb 4 and 5 sub 6 xor 7 cmp
static u32 alu(CPU *c, int op, u32 a, u32 b, int sz) {
    u32 mask = mask_of(sz), sign = (mask >> 1) + 1, r;
    u64 t;
    int cin = (op == 2 || op == 3) ? c->cf : 0;
    a &= mask;
    b &= mask;
    switch (op) {
    case 0:
    case 2:
        t = (u64)a + b + cin;
        r = t & mask;
        c->cf = (t >> (sz * 8)) & 1;
        c->of = ((a ^ r) & (b ^ r) & sign) != 0;
        break;
    case 1:
        r = a | b;
        c->cf = c->of = 0;
        break;
    case 4:
        r = a & b;
        c->cf = c->of = 0;
        break;
    case 6:
        r = a ^ b;
        c->cf = c->of = 0;
        break;
    default:
        t = (u64)a - b - cin;
        r = t & mask;
        c->cf = (u64)a < (u64)b + cin;
        c->of = ((a ^ b) & (a ^ r) & sign) != 0;
        break;
    }
    zsp(c, r, sz);
    return r;
}

static u32 shift(CPU *c, int op, u32 a, int n, int sz) {
    int bits = sz * 8;
    u32 mask = mask_of(sz), r;
    a &= mask;
    n &= 31;
    if (!n)
        return a;
    switch (op) {
    case 0: {
        int k = n % bits;
        r = k ? (u32)((((u64)a << k) | ((u64)a >> (bits - k))) & mask) : a;
        c->cf = r & 1;
        c->of = ((r >> (bits - 1)) & 1) ^ c->cf;
        return r;
    }
    case 1: {
        int k = n % bits;
        r = k ? (u32)((((u64)a >> k) | ((u64)a << (bits - k))) & mask) : a;
        c->cf = (r >> (bits - 1)) & 1;
        c->of = c->cf ^ ((r >> (bits - 2)) & 1);
        return r;
    }
    case 4:
    case 6:
        r = (u32)(((u64)a << n) & mask);
        c->cf = n <= bits ? ((u64)a >> (bits - n)) & 1 : 0;
        c->of = ((r >> (bits - 1)) & 1) ^ c->cf;
        zsp(c, r, sz);
        return r;
    case 5:
        r = n >= 32 ? 0 : a >> n;
        c->cf = (a >> (n - 1)) & 1;
        c->of = (a >> (bits - 1)) & 1;
        zsp(c, r, sz);
        return r;
    case 7: {
        i32 s = sx(a, sz);
        r = (u32)(s >> (n > 31 ? 31 : n)) & mask;
        c->cf = (s >> (n - 1)) & 1;
        c->of = 0;
        zsp(c, r, sz);
        return r;
    }
    default:
        die("shift op %d (rcl/rcr) unimplemented", op);
    }
}

static int cond(CPU *c, int cc) {
    int v;
    switch (cc >> 1) {
    case 0:
        v = c->of;
        break;
    case 1:
        v = c->cf;
        break;
    case 2:
        v = c->zf;
        break;
    case 3:
        v = c->cf || c->zf;
        break;
    case 4:
        v = c->sf;
        break;
    case 5:
        v = c->pf;
        break;
    case 6:
        v = c->sf != c->of;
        break;
    default:
        v = c->zf || (c->sf != c->of);
    }
    return v ^ (cc & 1);
}

static u32 getflags(CPU *c) {
    return 0x202 | c->cf | (c->pf << 2) | (c->zf << 6) | (c->sf << 7) | (c->df << 10) | (c->of << 11);
}
static void setflags(CPU *c, u32 f) {
    c->cf = f & 1;
    c->pf = (f >> 2) & 1;
    c->zf = (f >> 6) & 1;
    c->sf = (f >> 7) & 1;
    c->df = (f >> 10) & 1;
    c->of = (f >> 11) & 1;
}

static void strop(CPU *c, u8 op, int osz, int rep) {
    int sz = (op & 1) ? osz : 1, d = c->df ? -sz : sz, cmpk = (op & 0xFE) == 0xA6 || (op & 0xFE) == 0xAE;
    do {
        if (rep && c->r[ECX] == 0)
            break;
        switch (op & 0xFE) {
        case 0xA4:
            wr(c->r[EDI], rd(c->r[ESI], sz), sz);
            c->r[ESI] += d;
            c->r[EDI] += d;
            break;
        case 0xA6:
            alu(c, 7, rd(c->r[ESI], sz), rd(c->r[EDI], sz), sz);
            c->r[ESI] += d;
            c->r[EDI] += d;
            break;
        case 0xAA:
            wr(c->r[EDI], rget(c, EAX, sz), sz);
            c->r[EDI] += d;
            break;
        case 0xAC:
            rset(c, EAX, rd(c->r[ESI], sz), sz);
            c->r[ESI] += d;
            break;
        default:
            alu(c, 7, rget(c, EAX, sz), rd(c->r[EDI], sz), sz);
            c->r[EDI] += d;
            break;
        }
        if (rep) {
            c->r[ECX]--;
            if (cmpk && ((rep == 1 && !c->zf) || (rep == 2 && c->zf)))
                break;
        }
    } while (rep);
}

static u32 imm_z(CPU *c, int osz) {
    return osz == 2 ? f16(c) : f32(c);
}
