// libc_process.h: process, environment, time, locale and dlopen/dlsym imports.
//

H(exit) {
    fflush(NULL);
    printf("guest called exit(%d)\n", (int)A(0));
    exit((int)A(0));
}
H(_exit) {
    return h_exit(c);
}
H(abort) {
    fflush(NULL);
    printf("guest called abort()\n");
    exit(134);
}
H(__assert_rtn) {
    fflush(NULL);
    printf("assert failed: %s:%d %s (%s)\n", GS(A(1)), (int)A(2), GS(A(0)), GS(A(3)));
    exit(134);
}
H(__stack_chk_fail) {
    die("stack smashing detected");
}
H(__cxa_atexit) {
    RET(0);
    return 0;
} // atexit handlers are never run
H(getenv) {
    RET(0);
    return 0;
}
H(getpid) {
    RET(1234);
    return 0;
}
H(getpagesize) {
    RET(4096);
    return 0;
}
H(time) {
    time_t t = time(NULL);
    if (A(0))
        wr(A(0), (u32)t, 4);
    RET(t);
    return 0;
}
H(gettimeofday) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (A(0)) {
        wr(A(0), (u32)tv.tv_sec, 4);
        wr(A(0) + 4, (u32)tv.tv_usec, 4);
    }
    RET(0);
    return 0;
}
static u32 errno_slot(void);
H(__error) {
    RET(errno_slot());
    return 0;
}
H(setlocale) {
    RET(gstr("C"));
    return 0;
}
static u32 dl_open(CPU *c, const char *path);
static u32 dl_sym(u32 handle, const char *name);
static const char *dl_err; // loader/dyld.h
H(dlopen) {
    RET(dl_open(c, GS(A(0))));
    return 0;
}
H(dlsym) {
    RET(dl_sym(A(0), GS(A(1))));
    return 0;
}
H(dlerror) {
    RET(gstr(dl_err));
    return 0;
}
H(sigaction) {
    RET(0);
    return 0;
}
H(_setjmp) { // jmp_buf: ebx esi edi ebp esp eip
    u32 b = A(0), sp = c->r[ESP] + 4;
    (void)sp;
    wr(b, c->r[EBX], 4);
    wr(b + 4, c->r[ESI], 4);
    wr(b + 8, c->r[EDI], 4);
    wr(b + 12, c->r[EBP], 4);
    wr(b + 16, c->r[ESP] + 4, 4);
    wr(b + 20, rd(c->r[ESP], 4), 4);
    RET(0);
    return 0;
}
H(mach_absolute_time) {
    u64 t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    RET((u32)t);
    c->r[EDX] = (u32)(t >> 32);
    return 0;
}
H(mach_timebase_info) {
    wr(A(0), 1, 4);
    wr(A(0) + 4, 1, 4);
    RET(0);
    return 0;
}
H(mach_error_string) {
    RET(gstr("mach error"));
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_process_imports[] = {
    IMPORT(exit), IMPORT(_exit), IMPORT(abort), IMPORT(__assert_rtn), IMPORT(__stack_chk_fail),
    IMPORT(__cxa_atexit), IMPORT(getenv), IMPORT(getpid), IMPORT(getpagesize), IMPORT(time),
    IMPORT(gettimeofday), IMPORT(__error), IMPORT(setlocale), IMPORT(dlopen), IMPORT(dlsym),
    IMPORT(dlerror), IMPORT(sigaction), IMPORT(_setjmp), IMPORT(mach_absolute_time),
    IMPORT(mach_timebase_info), IMPORT(mach_error_string),
    {0, 0}};
// clang-format on
