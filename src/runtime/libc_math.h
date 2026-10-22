// libc_math.h: <math.h>. Results are returned in st(0) as the i386 ABI requires.
//

#define M1(n)                                                                                                \
    H(n) {                                                                                                   \
        fpush(c, n(Ad(c, 0)));                                                                               \
        return 0;                                                                                            \
    }                                                                                                        \
    H(n##f) {                                                                                                \
        fpush(c, n##f(Af(c, 0)));                                                                            \
        return 0;                                                                                            \
    }
M1(sin) M1(cos) M1(tan) M1(asin) M1(acos) M1(atan) M1(exp) M1(log) M1(log10) M1(floor) M1(ceil) H(atan2) {
    fpush(c, atan2(Ad(c, 0), Ad(c, 2)));
    return 0;
}
H(atan2f) {
    fpush(c, atan2f(Af(c, 0), Af(c, 1)));
    return 0;
}
H(pow) {
    fpush(c, pow(Ad(c, 0), Ad(c, 2)));
    return 0;
}
H(powf) {
    fpush(c, powf(Af(c, 0), Af(c, 1)));
    return 0;
}
H(fmod) {
    fpush(c, fmod(Ad(c, 0), Ad(c, 2)));
    return 0;
}
H(fmodf) {
    fpush(c, fmodf(Af(c, 0), Af(c, 1)));
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_math_imports[] = {
    IMPORT(sin), IMPORT(sinf), IMPORT(cos), IMPORT(cosf), IMPORT(tan), IMPORT(tanf), IMPORT(asin),
    IMPORT(asinf), IMPORT(acos), IMPORT(acosf), IMPORT(atan), IMPORT(atanf), IMPORT(exp), IMPORT(expf),
    IMPORT(log), IMPORT(logf), IMPORT(log10), IMPORT(log10f), IMPORT(floor), IMPORT(floorf), IMPORT(ceil),
    IMPORT(ceilf), IMPORT(atan2), IMPORT(atan2f), IMPORT(pow), IMPORT(powf), IMPORT(fmod), IMPORT(fmodf),
    {0, 0}};
// clang-format on
