// libc_string.h: <string.h> and the strto*/ato* family. Guest pointers are translated with GS()/P().
//

H(memcpy) {
    memmove(P(A(0)), P(A(1)), A(2));
    RET(A(0));
    return 0;
}
H(memset) {
    memset(P(A(0)), (int)A(1), A(2));
    RET(A(0));
    return 0;
}
H(memcmp) {
    RET(memcmp(P(A(0)), P(A(1)), A(2)));
    return 0;
}
H(memchr) {
    u8 *r = memchr(P(A(0)), (int)A(1), A(2));
    RET(r ? (u32)(r - G) : 0);
    return 0;
}
H(strlen) {
    RET(strlen(GS(A(0))));
    return 0;
}
H(strcmp) {
    RET(strcmp(GS(A(0)), GS(A(1))));
    return 0;
}
H(strncmp) {
    RET(strncmp(GS(A(0)), GS(A(1)), A(2)));
    return 0;
}
H(strcasecmp) {
    RET(strcasecmp(GS(A(0)), GS(A(1))));
    return 0;
}
H(strncasecmp) {
    RET(strncasecmp(GS(A(0)), GS(A(1)), A(2)));
    return 0;
}
H(strcpy) {
    strcpy(GS(A(0)), GS(A(1)));
    RET(A(0));
    return 0;
}
H(strncpy) {
    strncpy(GS(A(0)), GS(A(1)), A(2));
    RET(A(0));
    return 0;
}
H(strcat) {
    strcat(GS(A(0)), GS(A(1)));
    RET(A(0));
    return 0;
}
H(strncat) {
    strncat(GS(A(0)), GS(A(1)), A(2));
    RET(A(0));
    return 0;
}
H(strlcpy) {
    RET(strlcpy(GS(A(0)), GS(A(1)), A(2)));
    return 0;
}
H(strlcat) {
    RET(strlcat(GS(A(0)), GS(A(1)), A(2)));
    return 0;
}
H(strchr) {
    char *r = strchr(GS(A(0)), (int)A(1));
    RET(r ? (u32)((u8 *)r - G) : 0);
    return 0;
}
H(strrchr) {
    char *r = strrchr(GS(A(0)), (int)A(1));
    RET(r ? (u32)((u8 *)r - G) : 0);
    return 0;
}
H(strstr) {
    char *r = strstr(GS(A(0)), GS(A(1)));
    RET(r ? (u32)((u8 *)r - G) : 0);
    return 0;
}
H(strdup) {
    RET(gstr(GS(A(0))));
    return 0;
}
H(strspn) {
    RET(strspn(GS(A(0)), GS(A(1))));
    return 0;
}
H(strcspn) {
    RET(strcspn(GS(A(0)), GS(A(1))));
    return 0;
}
H(__tolower) {
    RET(tolower((int)A(0)));
    return 0;
}
H(__toupper) {
    RET(toupper((int)A(0)));
    return 0;
}
H(atoi) {
    RET(atoi(GS(A(0))));
    return 0;
}
H(atof) {
    fpush(c, atof(GS(A(0))));
    return 0;
}

static void ptr_out(u32 endp, u32 s, const char *e) {
    if (endp)
        wr(endp, s + (u32)(e - GS(s)), 4);
}
H(strtol) {
    char *e;
    long v = strtol(GS(A(0)), &e, (int)A(2));
    ptr_out(A(1), A(0), e);
    RET(v);
    return 0;
}
H(strtoul) {
    char *e;
    unsigned long v = strtoul(GS(A(0)), &e, (int)A(2));
    ptr_out(A(1), A(0), e);
    RET(v);
    return 0;
}
H(strtoll) {
    char *e;
    long long v = strtoll(GS(A(0)), &e, (int)A(2));
    ptr_out(A(1), A(0), e);
    RET((u32)v);
    c->r[EDX] = (u32)((u64)v >> 32);
    return 0;
}
H(strtod) {
    char *e;
    double v = strtod(GS(A(0)), &e);
    ptr_out(A(1), A(0), e);
    fpush(c, v);
    return 0;
}
H(strtof) {
    char *e;
    float v = strtof(GS(A(0)), &e);
    ptr_out(A(1), A(0), e);
    fpush(c, v);
    return 0;
}

H(memmove_) {
    return h_memcpy(c); // memcpy already has memmove semantics
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_string_imports[] = {
    IMPORT(memcpy), IMPORT(memmove_), IMPORT(memset), IMPORT(memcmp), IMPORT(memchr), IMPORT(strlen),
    IMPORT(strcmp), IMPORT(strncmp), IMPORT(strcasecmp), IMPORT(strncasecmp), IMPORT(strcpy),
    IMPORT(strncpy), IMPORT(strcat), IMPORT(strncat), IMPORT(strlcpy), IMPORT(strlcat), IMPORT(strchr),
    IMPORT(strrchr), IMPORT(strstr), IMPORT(strdup), IMPORT(strspn), IMPORT(strcspn), IMPORT(__tolower),
    IMPORT(__toupper), IMPORT(atoi), IMPORT(atof), IMPORT(strtol), IMPORT(strtoul), IMPORT(strtoll),
    IMPORT(strtod), IMPORT(strtof), {"_memmove", h_memmove_}, {"_wmemmove", h_memmove_},
    {0, 0}};
// clang-format on
