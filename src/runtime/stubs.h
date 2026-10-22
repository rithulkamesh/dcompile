// stubs.h: assorted small imports (glob, IOKit service lookups, ObjC exception hooks, ...).
//
// Things the game and the Ubisoft dylibs import that need only a trivial answer.

#include <glob.h>

// glob_t (i386): gl_pathc, gl_matchc, gl_offs, gl_flags, gl_pathv, ...
H(glob) {
    glob_t g;
    int r = glob(GS(A(0)), (int)A(1) & ~0x20, NULL, &g); // GLOB_ERR-style callbacks unsupported
    u32 out = A(3);
    memset(P(out), 0, 48);
    if (r == 0) {
        u32 v = hmalloc(4 * (g.gl_pathc + 1));
        for (size_t i = 0; i < g.gl_pathc; i++)
            wr(v + 4 * i, gstr(g.gl_pathv[i]), 4);
        wr(v + 4 * g.gl_pathc, 0, 4);
        wr(out, g.gl_pathc, 4);
        wr(out + 16, v, 4);
        globfree(&g);
    }
    RET(r);
    return 0;
}
H(globfree) {
    return 0;
}
H(chdir) {
    SYSRET(chdir(GS(A(0))));
    return 0;
}
H(feof) {
    RET(feof(file_of(A(0))));
    return 0;
}
H(rewind) {
    rewind(file_of(A(0)));
    return 0;
}
H(putc) {
    RET(putc((int)A(0), file_of(A(1))));
    return 0;
}
H(strpbrk) {
    char *r = strpbrk(GS(A(0)), GS(A(1)));
    RET(r ? (u32)((u8 *)r - G) : 0);
    return 0;
}
H(strtoull) {
    char *e;
    unsigned long long v = strtoull(GS(A(0)), &e, (int)A(2));
    ptr_out(A(1), A(0), e);
    RET64(v);
    return 0;
}
H(fdopen) {
    FILE *f = fdopen((int)A(0), GS(A(1)));
    RET(f ? file_new(f) : 0);
    return 0;
}
H(_NSGetExecutablePath) {
    u32 n = strlen(app_exe) + 1;
    if (rd(A(1), 4) < n) {
        wr(A(1), n, 4);
        RET((u32)-1);
    } else {
        strcpy(GS(A(0)), app_exe);
        RET(0);
    }
    return 0;
}
H(mach_vm_allocate) { // (task, u64 *addr, u64 size, flags)
    u32 sz = (A(2) + 4095) & ~4095u, p = (hmalloc(sz + 4096) + 4095) & ~4095u;
    memset(P(p), 0, sz);
    wr(A(1), p, 4);
    wr(A(1) + 4, 0, 4);
    RET(0);
    return 0;
}
H(mach_vm_deallocate) {
    RET(0);
    return 0;
}
H(wcslcat) {
    u32 d = A(0), s = A(1), n = A(2), dl = 0;
    while (dl < n && rd(d + 4 * dl, 4))
        dl++;
    if (dl == n) {
        u32 sl = 0;
        while (rd(s + 4 * sl, 4))
            sl++;
        RET(n + sl);
        return 0;
    }
    u32 i = 0;
    for (; rd(s + 4 * i, 4); i++)
        if (dl + i + 1 < n)
            wr(d + 4 * (dl + i), rd(s + 4 * i, 4), 4);
    wr(d + 4 * (dl + i < n ? dl + i : n - 1), 0, 4);
    RET(dl + i);
    return 0;
}
H(wcslcpy) {
    u32 d = A(0), s = A(1), n = A(2), i = 0;
    for (; rd(s + 4 * i, 4); i++)
        if (n && i + 1 < n)
            wr(d + 4 * i, rd(s + 4 * i, 4), 4);
    if (n)
        wr(d + 4 * (i < n ? i : n - 1), 0, 4);
    RET(i);
    return 0;
}
H(wcsncpy) {
    u32 d = A(0), s = A(1), n = A(2);
    int end = 0;
    for (u32 i = 0; i < n; i++) {
        u32 w = end ? 0 : rd(s + 4 * i, 4);
        if (!w)
            end = 1;
        wr(d + 4 * i, w, 4);
    }
    RET(d);
    return 0;
}
H(wcsstr) {
    u32 h = A(0), nd = A(1);
    if (!rd(nd, 4)) {
        RET(h);
        return 0;
    }
    for (u32 i = 0; rd(h + 4 * i, 4); i++) {
        u32 j = 0;
        while (rd(nd + 4 * j, 4) && rd(h + 4 * (i + j), 4) == rd(nd + 4 * j, 4))
            j++;
        if (!rd(nd + 4 * j, 4)) {
            RET(h + 4 * i);
            return 0;
        }
    }
    RET(0);
    return 0;
}
H(inet_ntoa) {
    RET(gstr("0.0.0.0"));
    return 0;
}
H(Debugger) {
    die("Debugger() called by the guest");
}
H(objc_unsupported) {
    die("Objective-C exception machinery not supported (%s)", tnames[c->eip - THUNK_BASE]);
}

// CoreFoundation extras (our CFString is { isa, flags, char *cstr, len })
H(CFStringCreateWithCString) {
    RET(nsstr(GS(A(1))));
    return 0;
}
H(CFStringGetCStringPtr) {
    RET(A(0) ? rd(A(0) + 8, 4) : 0);
    return 0;
}
H(CFArrayCreateMutable) {
    RET(arr_new());
    return 0;
}
H(CFArrayAppendValue) {
    arr_add(A(0), A(1));
    return 0;
}
H(CFUUIDCreate) {
    u32 o = obj_of("NSUUID");
    wr(o + 4, 0x12345678, 4);
    RET(o);
    return 0;
} // fixed UUID, not random
H(CFUUIDCreateString) {
    RET(nsstr("00000000-0000-4000-8000-0000DCOMPILE0"));
    return 0;
}
H(CFUserNotificationDisplayNotice) {
    fprintf(stderr, "[cf] notice: %s / %s\n", cf_cstr(A(6)), cf_cstr(A(7)));
    RET(0);
    return 0;
}
H(LSOpenApplication) {
    RET((u32)-1);
    return 0;
}
static const char *fs_ref_path(u32 ref);
H(CFURLCreateFromFSRef) {
    const char *p = fs_ref_path(A(1));
    RET(p ? url_new(p) : 0);
    return 0;
}
H(GetCurrentProcess) {
    wr(A(0), 0, 4);
    wr(A(0) + 4, 1, 4);
    RET(0);
    return 0;
}
H(GetProcessBundleLocation) {
    RET((u32)-43);
    return 0;
} // fnfErr: callers fall back to the executable path

// IOKit registry (hardware fingerprint): one fake platform device
H(IOServiceMatching) {
    u32 d = dict_new();
    dict_set(d, nsstr("IOProviderClass"), nsstr(GS(A(0))));
    RET(d);
    return 0;
}
H(IOServiceGetMatchingService) {
    RET(0x77);
    return 0;
}
H(IOServiceGetMatchingServices) {
    wr(A(2), 0, 4);
    RET(0);
    return 0;
}
H(IOIteratorNext) {
    RET(0);
    return 0;
}
H(IOObjectRelease) {
    RET(0);
    return 0;
}
H(IORegistryEntryGetParentEntry) {
    RET(0x10000003);
    return 0;
} // kIOReturnError
H(IORegistryEntryCreateCFProperty) {
    const char *k = cf_cstr(A(1));
    RET(!strcmp(k, "IOPlatformSerialNumber") ? nsstr("DCOMPILE0001")
        : !strcmp(k, "IOPlatformUUID")       ? nsstr("00000000-0000-4000-8000-0000DCOMPILE0")
                                             : 0);
    return 0;
}

// IOHID: no gamepads/HID devices are reported (mouse and keyboard arrive through the Cocoa event shim instead)
H(hid_zero) {
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry stubs_imports[] = {
    IMPORT(glob), IMPORT(globfree), IMPORT(chdir), IMPORT(feof), IMPORT(rewind), IMPORT(putc),
    IMPORT(strpbrk), IMPORT(strtoull), IMPORT(fdopen), IMPORT(_NSGetExecutablePath),
    IMPORT(mach_vm_allocate), IMPORT(mach_vm_deallocate), IMPORT(wcslcpy), IMPORT(wcslcat), IMPORT(wcsncpy),
    IMPORT(wcsstr), IMPORT(inet_ntoa), IMPORT(Debugger), IMPORT(CFStringCreateWithCString),
    IMPORT(CFStringGetCStringPtr), IMPORT(CFArrayCreateMutable), IMPORT(CFArrayAppendValue),
    IMPORT(CFUUIDCreate), IMPORT(CFUUIDCreateString), IMPORT(CFUserNotificationDisplayNotice),
    IMPORT(LSOpenApplication), IMPORT(CFURLCreateFromFSRef), IMPORT(GetCurrentProcess),
    IMPORT(GetProcessBundleLocation), IMPORT(IOServiceMatching), IMPORT(IOServiceGetMatchingService),
    IMPORT(IOServiceGetMatchingServices), IMPORT(IOIteratorNext), IMPORT(IOObjectRelease),
    IMPORT(IORegistryEntryGetParentEntry), IMPORT(IORegistryEntryCreateCFProperty),
    {"_objc_exception_extract", h_objc_unsupported}, {"_objc_exception_match", h_objc_unsupported},
    {"_objc_exception_throw", h_objc_unsupported}, {"_objc_exception_try_enter", h_objc_unsupported},
    {"_objc_exception_try_exit", h_objc_unsupported}, {"_objc_enumerationMutation", h_objc_unsupported},
    {"__Unwind_RaiseException", h_objc_unsupported}, {"__Unwind_Resume", h_objc_unsupported},
    {"__Unwind_Resume_or_Rethrow", h_objc_unsupported}, {"___gxx_personality_v0", h_objc_unsupported},
    {0, 0}};
// clang-format on
