// libc_atomic.h: OSAtomic*, 64-bit division helpers.
//

H(OSAtomicAdd32) {
    u32 v = rd(A(1), 4) + A(0);
    wr(A(1), v, 4);
    RET(v);
    return 0;
}
H(OSAtomicAdd64) {
    u64 v = ((u64)rd(A(2), 4) | (u64)rd(A(2) + 4, 4) << 32) + ((u64)A(0) | (u64)A(1) << 32);
    wr(A(2), (u32)v, 4);
    wr(A(2) + 4, v >> 32, 4);
    RET((u32)v);
    c->r[EDX] = v >> 32;
    return 0;
}
H(OSAtomicCompareAndSwap32Barrier) {
    if (rd(A(2), 4) == A(0)) {
        wr(A(2), A(1), 4);
        RET(1);
    } else
        RET(0);
    return 0;
}
#define A64(n) ((u64)A(n) | (u64)A((n) + 1) << 32)
#define RET64(v)                                                                                             \
    do {                                                                                                     \
        u64 _v = (v);                                                                                        \
        RET((u32)_v);                                                                                        \
        c->r[EDX] = (u32)(_v >> 32);                                                                         \
    } while (0)
H(__divdi3) {
    RET64((u64)((int64_t)A64(0) / (int64_t)A64(2)));
    return 0;
}
H(__moddi3) {
    RET64((u64)((int64_t)A64(0) % (int64_t)A64(2)));
    return 0;
}
H(__udivdi3) {
    RET64(A64(0) / A64(2));
    return 0;
}
H(__umoddi3) {
    RET64(A64(0) % A64(2));
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_atomic_imports[] = {
    IMPORT(OSAtomicAdd32), IMPORT(OSAtomicAdd64), IMPORT(OSAtomicCompareAndSwap32Barrier), IMPORT(__divdi3),
    IMPORT(__moddi3), IMPORT(__udivdi3), IMPORT(__umoddi3),
    {0, 0}};
// clang-format on
