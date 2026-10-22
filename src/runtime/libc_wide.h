// libc_wide.h: wide characters and ctype (wchar_t is 32-bit on Darwin; only the ASCII / "C" locale is supported).
//

// ---------------- wide chars & ctype (ASCII / "C" locale only) ----------------
H(__maskrune) {
    RET(A(0) < 128 ? __maskrune((int)A(0), A(1)) : 0);
    return 0;
}
H(wctob) {
    RET(A(0) < 128 ? A(0) : (u32)-1);
    return 0;
}
H(btowc) {
    RET((int)A(0) >= 0 && A(0) < 128 ? A(0) : (u32)-1);
    return 0;
}
H(mbrtowc) {
    u32 pwc = A(0), s = A(1), n = A(2);
    if (!s) {
        RET(0);
        return 0;
    }
    if (!n) {
        RET((u32)-2);
        return 0;
    }
    u8 ch = *P(s);
    if (pwc)
        wr(pwc, ch, 4);
    RET(ch ? 1 : 0);
    return 0;
}
H(wcrtomb) {
    if (A(0))
        *P(A(0)) = (u8)A(1);
    RET(1);
    return 0;
}
H(mbstowcs) {
    u32 d = A(0), s = A(1), n = A(2), i = 0;
    for (; i < n; i++) {
        u8 ch = *P(s + i);
        if (d)
            wr(d + 4 * i, ch, 4);
        if (!ch)
            return RET(i), 0;
    }
    RET(i);
    return 0;
}
H(wcstombs) {
    u32 d = A(0), s = A(1), n = A(2), i = 0;
    for (; i < n; i++) {
        u32 w = rd(s + 4 * i, 4);
        if (d)
            *P(d + i) = (u8)w;
        if (!w)
            return RET(i), 0;
    }
    RET(i);
    return 0;
}
H(wcslen) {
    u32 i = 0;
    while (rd(A(0) + 4 * i, 4))
        i++;
    RET(i);
    return 0;
}
static int wcmp(u32 a, u32 b, u32 n) {
    for (u32 i = 0; i < n; i++) {
        u32 x = rd(a + 4 * i, 4), y = rd(b + 4 * i, 4);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            break;
    }
    return 0;
}
H(wcscmp) {
    RET(wcmp(A(0), A(1), 0xffffffffu));
    return 0;
}
H(wcsncmp) {
    RET(wcmp(A(0), A(1), A(2)));
    return 0;
}
H(wcscpy) {
    u32 i = 0, w;
    do {
        w = rd(A(1) + 4 * i, 4);
        wr(A(0) + 4 * i, w, 4);
        i++;
    } while (w);
    RET(A(0));
    return 0;
}
H(wmemcpy) {
    memmove(P(A(0)), P(A(1)), A(2) * 4);
    RET(A(0));
    return 0;
}
H(wmemset) {
    for (u32 i = 0; i < A(2); i++)
        wr(A(0) + 4 * i, A(1), 4);
    RET(A(0));
    return 0;
}
H(wmemcmp) {
    for (u32 i = 0; i < A(2); i++) {
        u32 x = rd(A(0) + 4 * i, 4), y = rd(A(1) + 4 * i, 4);
        if (x != y)
            return RET(x < y ? -1 : 1), 0;
    }
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_wide_imports[] = {
    IMPORT(__maskrune), IMPORT(wctob), IMPORT(btowc), IMPORT(mbrtowc), IMPORT(wcrtomb), IMPORT(mbstowcs),
    IMPORT(wcstombs), IMPORT(wcslen), IMPORT(wcscmp), IMPORT(wcsncmp), IMPORT(wcscpy), IMPORT(wmemcpy),
    IMPORT(wmemset), IMPORT(wmemcmp),
    {0, 0}};
// clang-format on
