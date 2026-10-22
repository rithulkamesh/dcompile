// libc_printf.h: the printf family operating on guest varargs.
//
// gfmt() formats a guest format string using the guest stack slots at `ap`; it understands the conversions the game and the
// Ubisoft dylibs use (including %@, %S, %ls, 64-bit integers and long double). gfmt_w() is the wchar_t variant.

// Formats guest `fmt` using guest varargs at `ap` (advanced as consumed). Returns length (snprintf semantics).
static const char *obj_desc(u32 o); // cocoa/foundation.h
static int gfmt_h(const char *fmtp, u32 ap, char *out, size_t cap) {
    size_t n = 0;
    char tmp[512];
#define EMIT(...)                                                                                            \
    do {                                                                                                     \
        int emit_len = snprintf(tmp, sizeof tmp, __VA_ARGS__);                                               \
        if (emit_len < 0)                                                                                    \
            emit_len = 0;                                                                                    \
        if (emit_len >= (int)sizeof tmp)                                                                     \
            emit_len = sizeof tmp - 1;                                                                       \
        for (int q = 0; q < emit_len; q++, n++)                                                              \
            if (n + 1 < cap)                                                                                 \
                out[n] = tmp[q];                                                                             \
    } while (0)
    for (const char *p = fmtp; *p; p++) {
        if (*p != '%') {
            if (n + 1 < cap)
                out[n] = *p;
            n++;
            continue;
        }
        char spec[32];
        int sl = 0, lng = 0, big = 0;
        spec[sl++] = *p++;
        while (strchr("-+ #0", *p))
            spec[sl++] = *p++;
        if (*p == '*') {
            sl += sprintf(spec + sl, "%d", (int)rd(ap, 4));
            ap += 4;
            p++;
        } else
            while (isdigit((u8)*p))
                spec[sl++] = *p++;
        if (*p == '.') {
            spec[sl++] = *p++;
            if (*p == '*') {
                sl += sprintf(spec + sl, "%d", (int)rd(ap, 4));
                ap += 4;
                p++;
            } else
                while (isdigit((u8)*p))
                    spec[sl++] = *p++;
        }
        for (;; p++) {
            if (*p == 'l')
                lng++;
            else if (*p == 'L')
                big = 1;
            else if (*p == 'z' || *p == 't')
                ;
            else if (*p == 'j' || *p == 'q')
                lng = 2;
            else if (*p == 'h')
                ;
            else
                break;
        }
        char cv = *p;
        spec[sl] = 0;
        switch (cv) {
        case 'd':
        case 'i': {
            long long v = lng >= 2 ? (long long)((u64)rd(ap, 4) | (u64)rd(ap + 4, 4) << 32) : (int)rd(ap, 4);
            ap += lng >= 2 ? 8 : 4;
            strcat(spec, "lld");
            EMIT(spec, v);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v = lng >= 2 ? ((u64)rd(ap, 4) | (u64)rd(ap + 4, 4) << 32) : rd(ap, 4);
            ap += lng >= 2 ? 8 : 4;
            strcat(spec, "ll");
            sl = strlen(spec);
            spec[sl] = cv;
            spec[sl + 1] = 0;
            EMIT(spec, v);
            break;
        }
        case 'c': {
            int v = (int)rd(ap, 4);
            ap += 4;
            strcat(spec, "c");
            EMIT(spec, v);
            break;
        }
        case 's':
        case 'S': {
            u32 sp = rd(ap, 4);
            ap += 4;
            strcat(spec, "s");
            if ((cv == 'S' || lng) && sp) {
                char w[512];
                size_t k = 0;
                for (; rd(sp + 4 * k, 4) && k < 511; k++) {
                    u32 ch = rd(sp + 4 * k, 4);
                    w[k] = ch < 128 ? (char)ch : '?';
                }
                w[k] = 0;
                EMIT(spec, w);
            } else
                EMIT(spec, sp ? GS(sp) : "(null)");
            break;
        }
        case 'p': {
            u32 v = rd(ap, 4);
            ap += 4;
            EMIT("0x%x", v);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a': {
            double d;
            if (big) {
                u64 m = (u64)rd(ap, 4) | (u64)rd(ap + 4, 4) << 32;
                u16 se = rd(ap + 8, 2);
                ap += 12;
                int e = (se & 0x7fff) - 16383;
                d = ldexp((double)m / 9223372036854775808.0, e + 1) * ((se & 0x8000) ? -1 : 1);
            } else {
                u64 v = (u64)rd(ap, 4) | (u64)rd(ap + 4, 4) << 32;
                memcpy(&d, &v, 8);
                ap += 8;
            }
            sl = strlen(spec);
            spec[sl] = cv;
            spec[sl + 1] = 0;
            EMIT(spec, d);
            break;
        }
        case '%':
            if (n + 1 < cap)
                out[n] = '%';
            n++;
            break;
        case '@': {
            u32 o = rd(ap, 4);
            ap += 4;
            strcat(spec, "s");
            EMIT(spec, obj_desc(o));
            break;
        }
        case 'n':
            ap += 4;
            break;
        default:
            die("printf: unsupported conversion '%c'", cv);
        }
    }
    if (cap)
        out[n < cap ? n : cap - 1] = 0;
    return (int)n;
}
static int gfmt(u32 fmt, u32 ap, char *out, size_t cap) {
    return gfmt_h(GS(fmt), ap, out, cap);
}
// wide format string (wchar_t = u32): convert to ASCII, format, widen the result back into `dst` (at most n wide chars)
static int gfmt_w(u32 wfmt, u32 ap, u32 dst, u32 n) {
    char f[1024], o[4096];
    size_t k = 0;
    for (; rd(wfmt + 4 * k, 4) && k < 1023; k++) {
        u32 ch = rd(wfmt + 4 * k, 4);
        f[k] = ch < 128 ? (char)ch : '?';
    }
    f[k] = 0;
    int len = gfmt_h(f, ap, o, sizeof o);
    if (len >= (int)sizeof o)
        len = sizeof o - 1;
    if (n) {
        u32 m = (u32)len < n - 1 ? (u32)len : n - 1;
        for (u32 i = 0; i < m; i++)
            wr(dst + 4 * i, (u8)o[i], 4);
        wr(dst + 4 * m, 0, 4);
    }
    return len;
}
static char pbuf[1 << 16];
H(printf) {
    int n = gfmt(A(0), c->r[ESP] + 8, pbuf, sizeof pbuf);
    fwrite(pbuf, 1, n < (int)sizeof pbuf ? n : sizeof pbuf - 1, stdout);
    RET(n);
    return 0;
}
H(fprintf) {
    int n = gfmt(A(1), c->r[ESP] + 12, pbuf, sizeof pbuf);
    fwrite(pbuf, 1, n < (int)sizeof pbuf ? n : sizeof pbuf - 1, file_of(A(0)));
    RET(n);
    return 0;
}
H(sprintf) {
    RET(gfmt(A(1), c->r[ESP] + 12, GS(A(0)), 1u << 30));
    return 0;
}
H(snprintf) {
    RET(gfmt(A(2), c->r[ESP] + 16, GS(A(0)), A(1)));
    return 0;
}
H(vsnprintf) {
    RET(gfmt(A(2), A(3), GS(A(0)), A(1)));
    return 0;
}
H(vsprintf) {
    RET(gfmt(A(1), A(2), GS(A(0)), 1u << 30));
    return 0;
}
H(swprintf) {
    RET(gfmt_w(A(2), c->r[ESP] + 16, A(0), A(1)));
    return 0;
}
H(vswprintf) {
    RET(gfmt_w(A(2), A(3), A(0), A(1)));
    return 0;
}
H(puts) {
    puts(GS(A(0)));
    RET(1);
    return 0;
}
H(perror) {
    fprintf(stderr, "%s: error\n", GS(A(0)));
    return 0;
}
H(asl_log) {
    int n = gfmt(A(3), c->r[ESP] + 20, pbuf, sizeof pbuf);
    fprintf(stderr, "[asl] %s\n", pbuf);
    RET(n);
    return 0;
}
H(asl_open) {
    RET(1);
    return 0;
}
H(asl_close) {
    return 0;
}
H(asl_set_filter) {
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_printf_imports[] = {
    IMPORT(swprintf), IMPORT(vswprintf), IMPORT(printf), IMPORT(fprintf), IMPORT(sprintf), IMPORT(snprintf),
    IMPORT(vsnprintf), IMPORT(vsprintf), IMPORT(puts), IMPORT(perror), IMPORT(asl_log), IMPORT(asl_open),
    IMPORT(asl_close), IMPORT(asl_set_filter),
    {0, 0}};
// clang-format on
