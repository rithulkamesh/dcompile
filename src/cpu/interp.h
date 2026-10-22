// interp.h: the reference x86-32 interpreter.
//
// exec() fetches, decodes and executes exactly one guest instruction. It is the semantic ground truth: the JIT (src/jit)
// translates the hot subset natively and calls exec() for everything else, and its self-test compares both engines.

static void exec(CPU *c) {
    int osz = 4, rep = 0;
    u8 op;
    M m;
    u32 a, b, r;
    int sz;
    for (;;) {
        op = f8(c);
        if (op == 0x66)
            osz = 2;
        else if (op == 0xF3)
            rep = 1;
        else if (op == 0xF2)
            rep = 2;
        else if (op == 0xF0 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0x26 || op == 0x67)
            continue;
        else
            break;
    }
    if (op < 0x40 && (op & 7) < 6) {
        int aop = op >> 3, form = op & 7;
        sz = (form & 1) ? osz : 1;
        if (form < 4) {
            modrm(c, &m);
            a = mget(c, &m, sz);
            b = rget(c, m.reg, sz);
            if (form < 2) {
                r = alu(c, aop, a, b, sz);
                if (aop != 7)
                    mset(c, &m, r, sz);
            } else {
                r = alu(c, aop, b, a, sz);
                if (aop != 7)
                    rset(c, m.reg, r, sz);
            }
        } else {
            b = sz == 1 ? f8(c) : imm_z(c, osz);
            r = alu(c, aop, rget(c, EAX, sz), b, sz);
            if (aop != 7)
                rset(c, EAX, r, sz);
        }
        return;
    }
    if (op >= 0x40 && op <= 0x4F) {
        int cf = c->cf, reg = op & 7;
        r = alu(c, op < 0x48 ? 0 : 5, rget(c, reg, osz), 1, osz);
        rset(c, reg, r, osz);
        c->cf = cf;
        return;
    }
    if (op >= 0x50 && op <= 0x57) {
        push(c, c->r[op & 7]);
        return;
    }
    if (op >= 0x58 && op <= 0x5F) {
        c->r[op & 7] = pop(c);
        return;
    }
    if (op >= 0x70 && op <= 0x7F) {
        i8 d = f8(c);
        if (cond(c, op & 15))
            c->eip += d;
        return;
    }
    if (op >= 0x91 && op <= 0x97) {
        a = rget(c, op & 7, osz);
        rset(c, op & 7, rget(c, EAX, osz), osz);
        rset(c, EAX, a, osz);
        return;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        r8set(c, op & 7, f8(c));
        return;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        rset(c, op & 7, imm_z(c, osz), osz);
        return;
    }
    if (op >= 0xA4 && op <= 0xAF && op != 0xA8 && op != 0xA9) {
        strop(c, op, osz, rep);
        return;
    }
    switch (op) {
    case 0x0F:
        twobyte(c, osz, rep);
        return;
    case 0x68:
        push(c, imm_z(c, 4));
        return;
    case 0x6A:
        push(c, (u32)(i8)f8(c));
        return;
    case 0x69:
    case 0x6B: {
        modrm(c, &m);
        a = mget(c, &m, osz);
        b = op == 0x69 ? imm_z(c, osz) : (u32)(i8)f8(c);
        i64 t = (i64)sx(a, osz) * sx(b, osz);
        r = (u32)t & mask_of(osz);
        rset(c, m.reg, r, osz);
        c->cf = c->of = t != sx(r, osz);
        return;
    }
    case 0x80:
    case 0x81:
    case 0x83:
        sz = op == 0x80 ? 1 : osz;
        modrm(c, &m);
        a = mget(c, &m, sz);
        b = op == 0x81 ? imm_z(c, osz) : (u32)(i8)f8(c);
        r = alu(c, m.reg, a, b, sz);
        if (m.reg != 7)
            mset(c, &m, r, sz);
        return;
    case 0x84:
    case 0x85:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        alu(c, 4, mget(c, &m, sz), rget(c, m.reg, sz), sz);
        return;
    case 0x86:
    case 0x87:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        a = mget(c, &m, sz);
        mset(c, &m, rget(c, m.reg, sz), sz);
        rset(c, m.reg, a, sz);
        return;
    case 0x88:
    case 0x89:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        mset(c, &m, rget(c, m.reg, sz), sz);
        return;
    case 0x8A:
    case 0x8B:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        rset(c, m.reg, mget(c, &m, sz), sz);
        return;
    case 0x8D:
        modrm(c, &m);
        if (m.isreg)
            die("lea reg");
        rset(c, m.reg, m.ea, osz);
        return;
    case 0x8F:
        modrm(c, &m);
        a = pop(c);
        mset(c, &m, a, 4);
        return;
    case 0x90:
    case 0x9B:
        return;
    case 0x98:
        if (osz == 4)
            c->r[EAX] = (u32)(i16)c->r[EAX];
        else
            rset(c, EAX, (u32)(i8)c->r[EAX], 2);
        return;
    case 0x99:
        if (osz == 4)
            c->r[EDX] = (i32)c->r[EAX] < 0 ? 0xffffffffu : 0;
        else
            rset(c, EDX, (i16)c->r[EAX] < 0 ? 0xffff : 0, 2);
        return;
    case 0x9C:
        push(c, getflags(c));
        return;
    case 0x9D:
        setflags(c, pop(c));
        return;
    case 0x9E:
        setflags(c, (getflags(c) & ~0xd5u) | ((c->r[EAX] >> 8) & 0xd5));
        return;
    case 0x9F:
        rset(c, EAX, (getflags(c) & 0xff) << 8 | (c->r[EAX] & 0xff), 2);
        return;
    case 0xA0:
    case 0xA1:
        sz = op & 1 ? osz : 1;
        rset(c, EAX, rd(f32(c), sz), sz);
        return;
    case 0xA2:
    case 0xA3:
        sz = op & 1 ? osz : 1;
        wr(f32(c), rget(c, EAX, sz), sz);
        return;
    case 0xA8:
        alu(c, 4, c->r[EAX], f8(c), 1);
        return;
    case 0xA9:
        alu(c, 4, c->r[EAX], imm_z(c, osz), osz);
        return;
    case 0xC0:
    case 0xC1:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3: {
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        a = mget(c, &m, sz);
        int n = op < 0xD0 ? f8(c) : op < 0xD2 ? 1 : c->r[ECX] & 0xff;
        r = shift(c, m.reg, a, n, sz);
        mset(c, &m, r, sz);
        return;
    }
    case 0xC2:
        a = f16(c);
        c->eip = pop(c);
        c->r[ESP] += a;
        return;
    case 0xC3:
        c->eip = pop(c);
        return;
    case 0xC6:
    case 0xC7:
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        b = sz == 1 ? f8(c) : imm_z(c, osz);
        mset(c, &m, b, sz);
        return;
    case 0xC9:
        c->r[ESP] = c->r[EBP];
        c->r[EBP] = pop(c);
        return;
    case 0xE0:
    case 0xE1:
    case 0xE2: {
        i8 d = f8(c);
        c->r[ECX]--;
        if (c->r[ECX] && (op == 0xE2 || (op == 0xE1) == c->zf))
            c->eip += d;
        return;
    }
    case 0xE3: {
        i8 d = f8(c);
        if (!c->r[ECX])
            c->eip += d;
        return;
    }
    case 0xE8: {
        i32 d = f32(c);
        push(c, c->eip);
        c->eip += d;
        return;
    }
    case 0xE9: {
        i32 d = f32(c);
        c->eip += d;
        return;
    }
    case 0xEB: {
        i8 d = f8(c);
        c->eip += d;
        return;
    }
    case 0xF4:
        die("hlt");
    case 0xF5:
        c->cf = !c->cf;
        return;
    case 0xF8:
        c->cf = 0;
        return;
    case 0xF9:
        c->cf = 1;
        return;
    case 0xFA:
    case 0xFB:
        return;
    case 0xFC:
        c->df = 0;
        return;
    case 0xFD:
        c->df = 1;
        return;
    case 0xF6:
    case 0xF7: {
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        a = mget(c, &m, sz);
        switch (m.reg) {
        case 0:
        case 1:
            alu(c, 4, a, sz == 1 ? f8(c) : imm_z(c, osz), sz);
            return;
        case 2:
            mset(c, &m, ~a, sz);
            return;
        case 3:
            r = alu(c, 5, 0, a, sz);
            c->cf = a != 0;
            mset(c, &m, r, sz);
            return;
        case 4:
        case 5: {
            if (sz == 4) {
                if (m.reg == 4) {
                    u64 t = (u64)c->r[EAX] * a;
                    c->r[EAX] = (u32)t;
                    c->r[EDX] = t >> 32;
                    c->cf = c->of = c->r[EDX] != 0;
                } else {
                    i64 t = (i64)(i32)c->r[EAX] * (i32)a;
                    c->r[EAX] = (u32)t;
                    c->r[EDX] = (u64)t >> 32;
                    c->cf = c->of = t != (i32)t;
                }
            } else if (sz == 2) {
                if (m.reg == 4) {
                    u32 t = (c->r[EAX] & 0xffff) * (a & 0xffff);
                    rset(c, EAX, t, 2);
                    rset(c, EDX, t >> 16, 2);
                    c->cf = c->of = (t >> 16) != 0;
                } else {
                    i32 t = (i16)c->r[EAX] * (i16)a;
                    rset(c, EAX, t, 2);
                    rset(c, EDX, (u32)t >> 16, 2);
                    c->cf = c->of = t != (i16)t;
                }
            } else { // AX = AL * r/m8
                if (m.reg == 4) {
                    u32 t = (c->r[EAX] & 0xff) * (a & 0xff);
                    rset(c, EAX, t, 2);
                    c->cf = c->of = (t >> 8) != 0;
                } else {
                    i32 t = (i8)c->r[EAX] * (i8)a;
                    rset(c, EAX, t, 2);
                    c->cf = c->of = t != (i8)t;
                }
            }
            return;
        }
        default: {
            if (!(a & mask_of(sz)))
                die("divide by zero");
            if (sz == 4) {
                if (m.reg == 6) {
                    u64 n = ((u64)c->r[EDX] << 32) | c->r[EAX];
                    c->r[EAX] = (u32)(n / a);
                    c->r[EDX] = (u32)(n % a);
                } else {
                    i64 n = (i64)(((u64)c->r[EDX] << 32) | c->r[EAX]);
                    c->r[EAX] = (u32)(n / (i32)a);
                    c->r[EDX] = (u32)(n % (i32)a);
                }
            } else if (sz == 2) {
                u32 n = ((c->r[EDX] & 0xffff) << 16) | (c->r[EAX] & 0xffff);
                if (m.reg == 6) {
                    rset(c, EAX, n / (a & 0xffff), 2);
                    rset(c, EDX, n % (a & 0xffff), 2);
                } else {
                    i32 sn = (i32)n;
                    rset(c, EAX, sn / (i16)a, 2);
                    rset(c, EDX, sn % (i16)a, 2);
                }
            } else {
                u32 n = c->r[EAX] & 0xffff;
                if (m.reg == 6) {
                    u32 q = n / (a & 0xff), r = n % (a & 0xff);
                    rset(c, EAX, (r << 8) | (q & 0xff), 2);
                } else {
                    i32 sn = (i16)n;
                    i32 q = sn / (i8)a, r = sn % (i8)a;
                    rset(c, EAX, ((r & 0xff) << 8) | (q & 0xff), 2);
                }
            }
            return;
        }
        }
    }
    case 0xFE:
    case 0xFF: {
        sz = op & 1 ? osz : 1;
        modrm(c, &m);
        if (m.reg < 2) {
            int cf = c->cf;
            a = mget(c, &m, sz);
            r = alu(c, m.reg ? 5 : 0, a, 1, sz);
            mset(c, &m, r, sz);
            c->cf = cf;
            return;
        }
        if (op == 0xFE)
            die("FE /%d", m.reg);
        a = mget(c, &m, 4);
        if (m.reg == 2) {
            push(c, c->eip);
            c->eip = a;
            return;
        }
        if (m.reg == 4) {
            c->eip = a;
            return;
        }
        if (m.reg == 6) {
            push(c, a);
            return;
        }
        die("FF /%d", m.reg);
    }
    }
    if (op >= 0xD8 && op <= 0xDF) {
        fpu(c, op);
        return;
    }
    die("unimplemented opcode %02x", op);
}
