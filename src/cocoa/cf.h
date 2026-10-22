// cf.h: CoreFoundation / Foundation C functions on top of the object model in objc_classes.h/foundation.h (strings, arrays, dictionaries, numbers, URLs).
//

static const char *cf_cstr(u32 s) {
    return s ? cstr_of(s) : "";
}

static u32 url_new(const char *path) {
    u32 u = obj_of("NSURL");
    wr(u + 4, nsstr(path), 4);
    return u;
}
static const char *url_path(u32 u) {
    return cf_cstr(rd(u + 4, 4));
}

H(CFRelease) {
    return 0;
}
H(CFShow) {
    return 0;
}
H(CFGetTypeID) {
    u32 o = A(0);
    if (hid_is_dev(o)) {
        RET(0x77);
        return 0;
    }
    int k = o ? class_of_obj(o) : -1;
    RET(k >= 0 && is_kind(k, "NSString")       ? 7
        : k >= 0 && is_kind(k, "NSArray")      ? 19
        : k >= 0 && is_kind(k, "NSDictionary") ? 18
                                               : 99);
    return 0;
}
H(CFStringGetTypeID) {
    RET(7);
    return 0;
}
H(CFArrayGetTypeID) {
    RET(19);
    return 0;
}
H(CFStringCreateWithBytes) {
    char *t = malloc(A(2) + 1);
    memcpy(t, P(A(1)), A(2));
    t[A(2)] = 0;
    RET(nsstr(t));
    free(t);
    return 0;
}
H(CFStringCreateWithFileSystemRepresentation) {
    RET(nsstr(GS(A(1))));
    return 0;
}
H(CFStringCreateWithFormat) {
    gfmt(rd(A(2) + 8, 4), c->r[ESP] + 16, pbuf, sizeof pbuf);
    RET(nsstr(pbuf));
    return 0;
}
H(CFStringCreateWithFormatAndArguments) {
    gfmt(rd(A(2) + 8, 4), A(3), pbuf, sizeof pbuf);
    RET(nsstr(pbuf));
    return 0;
}
H(CFStringGetLength) {
    RET(strlen(cf_cstr(A(0))));
    return 0;
}
H(CFStringGetCString) { // (str, buf, bufferSize, encoding) -> Boolean
    const char *s = cf_cstr(A(0));
    if (strlen(s) + 1 > A(2)) {
        RET(0);
        return 0;
    }
    strcpy(GS(A(1)), s);
    RET(1);
    return 0;
}
H(CFStringGetBytes) { // (str, CFRange{loc,len}, enc, lossByte, isExt, buf, maxBuf, usedBuf) -> CFIndex
    const char *s = cf_cstr(A(0));
    u32 loc = A(1), len = A(2), buf = A(6), max = A(7), used = A(8);
    u32 n = len < max || !buf ? len : max;
    if (buf)
        memcpy(P(buf), s + loc, n);
    if (used)
        wr(used, n, 4);
    RET(len);
    return 0;
}
H(CFStringGetCharacters) {
    const char *s = cf_cstr(A(0));
    u32 loc = A(1), len = A(2), buf = A(3);
    for (u32 i = 0; i < len; i++)
        wr(buf + 2 * i, (u8)s[loc + i], 2);
    return 0;
}
H(CFStringGetFileSystemRepresentation) {
    const char *s = cf_cstr(A(0));
    if (strlen(s) + 1 > A(2)) {
        RET(0);
        return 0;
    }
    strcpy(GS(A(1)), s);
    RET(1);
    return 0;
}
H(CFStringGetMaximumSizeOfFileSystemRepresentation) {
    RET(strlen(cf_cstr(A(0))) * 3 + 1);
    return 0;
}
H(CFStringCreateExternalRepresentation) {
    u32 d = obj_of("NSData");
    const char *s = cf_cstr(A(1));
    wr(d + 4, gstr(s), 4);
    wr(d + 8, strlen(s), 4);
    RET(d);
    return 0;
}
H(CFDataCreate) {
    u32 d = obj_of("NSData");
    u32 n = A(2), p = hmalloc(n + 1);
    memcpy(P(p), P(A(1)), n);
    wr(d + 4, p, 4);
    wr(d + 8, n, 4);
    RET(d);
    return 0;
}
H(CFDataGetLength) {
    RET(rd(A(0) + 8, 4));
    return 0;
}
H(CFDataGetBytePtr) {
    RET(rd(A(0) + 4, 4));
    return 0;
}
H(CFDataGetBytes) {
    u32 loc = A(1), len = A(2);
    memcpy(P(A(3)), P(rd(A(0) + 4, 4) + loc), len);
    return 0;
}
H(CFArrayGetCount) {
    RET(rd(A(0) + 4, 4));
    return 0;
}
H(CFArrayGetValueAtIndex) {
    RET(rd(rd(A(0) + 12, 4) + 4 * A(1), 4));
    return 0;
}
H(CFSetGetCount) {
    RET(hid_is_set(A(0)) ? 1 : 0);
    return 0;
}
H(CFSetGetValues) {
    if (hid_is_set(A(0)))
        wr(A(1), rd(A(0) + 4, 4), 4);
    return 0;
}
H(CFDictionaryCreateMutable) {
    RET(dict_new());
    return 0;
}
H(CFDictionarySetValue) {
    dict_set(A(0), A(1), A(2));
    return 0;
}
H(CFDictionaryGetValue) {
    RET(dict_get(A(0), A(1)));
    return 0;
}
H(CFDictionaryContainsValue) {
    u32 d = A(0), v = A(1);
    for (u32 i = 0; i < rd(d + 4, 4); i++)
        if (rd(rd(d + 16, 4) + 4 * i, 4) == v) {
            RET(1);
            return 0;
        }
    RET(0);
    return 0;
}
H(CFNumberCreate) { // (alloc, type, ptr)
    u32 n = obj_of("NSNumber");
    u32 t = A(1), p = A(2);
    double d;
    float f;
    if (t == 5 || t == 12) {
        memcpy(&f, P(p), 4);
        d = f;
        wr(n + 12, 1, 4);
        wr(n + 4, (u32)(i32)d, 4);
        memcpy(P(n + 8), &d, 8);
    } else if (t == 6 || t == 13) {
        memcpy(&d, P(p), 8);
        wr(n + 12, 1, 4);
        wr(n + 4, (u32)(i32)d, 4);
        memcpy(P(n + 8), &d, 8);
    } else {
        i32 v = (t == 1 || t == 7) ? (i8)*P(p) : (t == 2 || t == 8) ? (i16)rd(p, 2) : (i32)rd(p, 4);
        d = v;
        wr(n + 4, (u32)v, 4);
        memcpy(P(n + 8), &d, 8);
    }
    RET(n);
    return 0;
}
H(CFNumberGetValue) { // (num, type, ptr)
    u32 n = A(0), t = A(1), p = A(2);
    double d;
    memcpy(&d, P(n + 8), 8);
    i32 iv = (i32)rd(n + 4, 4);
    if (t == 5 || t == 12) {
        float f = (float)d;
        memcpy(P(p), &f, 4);
    } else if (t == 6 || t == 13)
        memcpy(P(p), &d, 8);
    else if (t == 1 || t == 7)
        *P(p) = (u8)iv;
    else if (t == 2 || t == 8)
        wr(p, (u16)iv, 2);
    else if (t == 4 || t == 11) {
        i64 q = rd(n + 12, 4) ? (i64)d : (i64)iv;
        wr(p, (u32)q, 4);
        wr(p + 4, (u32)((u64)q >> 32), 4);
    } else
        wr(p, (u32)(rd(n + 12, 4) ? (i32)d : iv), 4);
    RET(1);
    return 0;
}
H(CFBooleanGetValue) {
    RET(rd(A(0) + 4, 4) != 0);
    return 0;
}
H(CFPreferencesCopyAppValue) {
    if (log_imports)
        fprintf(stderr, "[cf] CFPreferencesCopyAppValue(%s, %s)\n", cf_cstr(A(0)), cf_cstr(A(1)));
    if (!strcmp(cf_cstr(A(0)), "LauncherBundlePath")) { // Steam ships the launcher next to the game
        char p[1200];
        snprintf(p, sizeof p, "%s/../Ubisoft/Ubisoft Game Launcher.app", app_bundle);
        char r[PATH_MAX];
        if (realpath(p, r)) {
            RET(nsstr(r));
            return 0;
        }
    }
    RET(0);
    return 0;
}
H(CFLocaleCopyCurrent) {
    RET(obj_of("NSLocale"));
    return 0;
}
H(CFLocaleGetValue) {
    RET(nsstr("US"));
    return 0;
}
H(CFPropertyListCreateFromXMLData) { // (alloc, CFDataRef xml, mutability, CFStringRef *errorString)
    u32 d = A(1), g = d ? plist_from_bytes(P(rd(d + 4, 4)), rd(d + 8, 4)) : 0;
    if (A(3))
        wr(A(3), 0, 4);
    RET(g);
    return 0;
}
H(CFAllocatorAllocate) {
    RET(hmalloc(A(1)));
    return 0;
}
H(CFAllocatorDeallocate) {
    return 0;
}
H(CFUserNotificationDisplayAlert) {
    fflush(NULL);
    fprintf(stderr, "[cf] alert: %s / %s\n", cf_cstr(A(3)), cf_cstr(A(4)));
    RET(0);
    return 0;
}
// bundles & URLs
H(CFBundleGetMainBundle) {
    u32 b = singleton("NSBundle");
    if (!rd(b + 4, 4))
        wr(b + 4, nsstr(app_bundle), 4);
    RET(b);
    return 0;
}
H(CFBundleCreate) {
    if (!A(1)) {
        RET(0);
        return 0;
    }
    u32 b = obj_of("NSBundle");
    wr(b + 4, rd(A(1) + 4, 4), 4);
    RET(b);
    return 0;
}
H(CFBundleCopyResourcesDirectoryURL) {
    char t[1100];
    snprintf(t, sizeof t, "%s/Contents/Resources", cf_cstr(rd(A(0) + 4, 4)));
    RET(url_new(t));
    return 0;
}
H(CFBundleCopyPrivateFrameworksURL) {
    char t[1100];
    snprintf(t, sizeof t, "%s/Contents/Frameworks", cf_cstr(rd(A(0) + 4, 4)));
    RET(url_new(t));
    return 0;
}
H(CFBundleGetValueForInfoDictionaryKey) { // main bundle's Info.plist, loaded lazily with the host's CF
    static u32 info;
    if (!info) {
        char p[1100];
        snprintf(p, sizeof p, "%s/Contents/Info.plist", app_bundle);
        info = load_plist(p);
    }
    RET(dict_get(info, A(1)));
    return 0;
}
H(CFURLCreateWithFileSystemPath) {
    RET(url_new(cf_cstr(A(1))));
    return 0;
}
H(CFURLCopyFileSystemPath) {
    RET(nsstr(url_path(A(0))));
    return 0;
}
H(CFURLCopyAbsoluteURL) {
    RET(A(0));
    return 0;
}
H(CFURLGetFileSystemRepresentation) {
    const char *p = url_path(A(0));
    if (strlen(p) + 1 > A(3)) {
        RET(0);
        return 0;
    }
    strcpy(GS(A(2)), p);
    RET(1);
    return 0;
}
H(CFURLCreateCopyAppendingPathComponent) {
    char t[2200];
    snprintf(t, sizeof t, "%s/%s", url_path(A(1)), cf_cstr(A(2)));
    RET(url_new(t));
    return 0;
}

// Foundation
H(NSLog) {
    gfmt(rd(A(0) + 8, 4), c->r[ESP] + 8, pbuf, sizeof pbuf);
    fprintf(stderr, "[NSLog] %s\n", pbuf);
    return 0;
}
H(NSMouseInRect) {
    RET(0);
    return 0;
}
H(NSSearchPathForDirectoriesInDomains) {
    const char *home = getenv("HOME");
    char t[1100];
    u32 dir = A(0);
    snprintf(t, sizeof t, "%s/Library%s", home ? home : "",
             dir == 14   ? "/Application Support"
             : dir == 9  ? "/Documents"
             : dir == 13 ? "/Caches"
                         : "");
    u32 a = arr_new();
    arr_add(a, nsstr(t));
    RET(a);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry cf_imports[] = {
    IMPORT(CFRelease), IMPORT(CFShow), IMPORT(CFGetTypeID), IMPORT(CFStringGetTypeID),
    IMPORT(CFArrayGetTypeID), IMPORT(CFStringCreateWithBytes),
    IMPORT(CFStringCreateWithFileSystemRepresentation), IMPORT(CFStringCreateWithFormat),
    IMPORT(CFStringCreateWithFormatAndArguments), IMPORT(CFStringGetLength), IMPORT(CFStringGetCString),
    IMPORT(CFStringGetBytes), IMPORT(CFStringGetCharacters), IMPORT(CFStringGetFileSystemRepresentation),
    IMPORT(CFStringGetMaximumSizeOfFileSystemRepresentation), IMPORT(CFStringCreateExternalRepresentation),
    IMPORT(CFDataCreate), IMPORT(CFDataGetLength), IMPORT(CFDataGetBytePtr), IMPORT(CFDataGetBytes),
    IMPORT(CFArrayGetCount), IMPORT(CFArrayGetValueAtIndex), IMPORT(CFSetGetCount), IMPORT(CFSetGetValues),
    IMPORT(CFDictionaryCreateMutable), IMPORT(CFDictionarySetValue), IMPORT(CFDictionaryGetValue),
    IMPORT(CFDictionaryContainsValue), IMPORT(CFNumberCreate), IMPORT(CFNumberGetValue),
    IMPORT(CFBooleanGetValue), IMPORT(CFPreferencesCopyAppValue), IMPORT(CFLocaleCopyCurrent),
    IMPORT(CFLocaleGetValue), IMPORT(CFPropertyListCreateFromXMLData), IMPORT(CFAllocatorAllocate),
    IMPORT(CFAllocatorDeallocate), IMPORT(CFUserNotificationDisplayAlert), IMPORT(CFBundleGetMainBundle),
    IMPORT(CFBundleCreate), IMPORT(CFBundleCopyResourcesDirectoryURL),
    IMPORT(CFBundleCopyPrivateFrameworksURL), IMPORT(CFBundleGetValueForInfoDictionaryKey),
    IMPORT(CFURLCreateWithFileSystemPath), IMPORT(CFURLCopyFileSystemPath), IMPORT(CFURLCopyAbsoluteURL),
    IMPORT(CFURLGetFileSystemRepresentation), IMPORT(CFURLCreateCopyAppendingPathComponent), IMPORT(NSLog),
    IMPORT(NSMouseInRect), IMPORT(NSSearchPathForDirectoriesInDomains),
    {0, 0}};
// clang-format on
