// twobyte.h: the 0F-prefixed opcode map (Jcc rel32, SETcc, CMOVcc, MOVZX/SX, BT*, SHLD/SHRD, IMUL, ... plus SSE dispatch).
//
// Called from exec() when it sees the 0F escape byte.

static void twobyte(CPU *c, int osz, int rep) {
    u8 op = f8(c);
    M m;
    u32 a, b, r;
    int sz;
    if (sse(c, op, osz == 2 ? 1 : rep == 1 ? 2 : rep == 2 ? 3 : 0))
        return;
    if (op >= 0x40 && op <= 0x4F) {
        modrm(c, &m);
        a = mget(c, &m, osz);
        if (cond(c, op & 15))
            rset(c, m.reg, a, osz);
        return;
    }
    if (op >= 0x80 && op <= 0x8F) {
        i32 d = f32(c);
        if (cond(c, op & 15))
            c->eip += d;
        return;
    }
    if (op >= 0x90 && op <= 0x9F) {
        modrm(c, &m);
        mset(c, &m, cond(c, op & 15), 1);
        return;
    }
    if (op >= 0xC8 && op <= 0xCF) {
        c->r[op & 7] = __builtin_bswap32(c->r[op & 7]);
        return;
    }
    switch (op) {
    case 0x0D:
    case 0x18:
        modrm(c, &m);
        return; // prefetch
    case 0x1F:
        modrm(c, &m);
        return; // nop Ev
    case 0x31: {
        u64 t = 0;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(t));
        c->r[EAX] = (u32)t;
        c->r[EDX] = (u32)(t >> 32);
        return;
    }
    case 0xA2:
        c->r[EAX] = 1;
        c->r[EBX] = c->r[ECX] = 0;
        c->r[EDX] = 0x00008001;
        return; // cpuid: fpu+cmov only, no SSE
    case 0xAF:
        modrm(c, &m);
        {
            i64 t = (i64)sx(rget(c, m.reg, osz), osz) * sx(mget(c, &m, osz), osz);
            r = (u32)t & mask_of(osz);
            rset(c, m.reg, r, osz);
            c->cf = c->of = t != sx(r, osz);
        }
        return;
    case 0xB6:
        modrm(c, &m);
        rset(c, m.reg, mget(c, &m, 1), osz);
        return;
    case 0xB7:
        modrm(c, &m);
        rset(c, m.reg, mget(c, &m, 2), osz);
        return;
    case 0xBE:
        modrm(c, &m);
        rset(c, m.reg, (u32)sx(mget(c, &m, 1), 1), osz);
        return;
    case 0xBF:
        modrm(c, &m);
        rset(c, m.reg, (u32)sx(mget(c, &m, 2), 2), osz);
        return;
    case 0xBC:
    case 0xBD:
        modrm(c, &m);
        a = mget(c, &m, osz) & mask_of(osz);
        c->zf = a == 0;
        if (a)
            rset(c, m.reg, op == 0xBC ? __builtin_ctz(a) : 31 - __builtin_clz(a), osz);
        return;
    case 0xA3:
    case 0xAB:
    case 0xB3:
    case 0xBB:
    case 0xBA: {
        modrm(c, &m);
        int bit, k;
        if (op == 0xBA) {
            bit = f8(c);
            k = m.reg - 4;
            if (k < 0)
                die("0F BA /%d", m.reg);
        } else {
            bit = rget(c, m.reg, osz);
            k = op == 0xA3 ? 0 : op == 0xAB ? 1 : op == 0xB3 ? 2 : 3;
            if (!m.isreg)
                m.ea += ((i32)bit >> 5) * 4;
        }
        bit &= 31;
        a = mget(c, &m, 4);
        c->cf = (a >> bit) & 1;
        if (k == 1)
            a |= 1u << bit;
        else if (k == 2)
            a &= ~(1u << bit);
        else if (k == 3)
            a ^= 1u << bit;
        if (k)
            mset(c, &m, a, 4);
        return;
    }
    case 0xB0:
    case 0xB1:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        a = mget(c, &m, sz);
        alu(c, 7, rget(c, EAX, sz), a, sz);
        if (c->zf)
            mset(c, &m, rget(c, m.reg, sz), sz);
        else
            rset(c, EAX, a, sz);
        return;
    case 0xC0:
    case 0xC1:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        a = mget(c, &m, sz);
        b = rget(c, m.reg, sz);
        r = alu(c, 0, a, b, sz);
        rset(c, m.reg, a, sz);
        mset(c, &m, r, sz);
        return;
    case 0xA4:
    case 0xA5:
    case 0xAC:
    case 0xAD: {
        modrm(c, &m);
        int n = (op & 1) ? c->r[ECX] & 31 : f8(c) & 31;
        a = mget(c, &m, osz);
        b = rget(c, m.reg, osz);
        if (!n)
            return;
        int bits = osz * 8;
        if (op < 0xA8) {
            r = (u32)((((u64)a << n) | ((u64)b >> (bits - n))) & mask_of(osz));
            c->cf = (a >> (bits - n)) & 1;
        } else {
            r = (u32)((((u64)a >> n) | ((u64)b << (bits - n))) & mask_of(osz));
            c->cf = (a >> (n - 1)) & 1;
        }
        zsp(c, r, osz);
        mset(c, &m, r, osz);
        return;
    }
    }
    die("unimplemented 0F %02x", op);
}
