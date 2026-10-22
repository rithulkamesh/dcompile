// plist.h: property lists parsed by the host's CoreFoundation and converted into guest objects.
//
// Used for Info.plist, dictionaryWithContentsOfFile: and CFPropertyListCreateFromXMLData.

#include <CoreFoundation/CoreFoundation.h>

static u32 cf_to_guest(CFTypeRef v);
static void plist_entry(const void *k, const void *v, void *ctx) {
    u32 d = *(u32 *)ctx;
    char kb[512];
    if (CFGetTypeID(k) != CFStringGetTypeID() || !CFStringGetCString(k, kb, sizeof kb, kCFStringEncodingUTF8))
        return;
    u32 g = cf_to_guest(v);
    if (g)
        dict_set(d, nsstr(kb), g);
}
static u32 cf_to_guest(CFTypeRef v) {
    CFTypeID t = CFGetTypeID(v);
    if (t == CFStringGetTypeID()) {
        CFIndex n = CFStringGetMaximumSizeForEncoding(CFStringGetLength(v), kCFStringEncodingUTF8) + 1;
        char *b = malloc(n);
        u32 r = CFStringGetCString(v, b, n, kCFStringEncodingUTF8) ? nsstr(b) : nsstr("");
        free(b);
        return r;
    }
    if (t == CFBooleanGetTypeID()) {
        u32 o = obj_of("NSNumber");
        wr(o + 4, CFBooleanGetValue(v), 4);
        return o;
    }
    if (t == CFNumberGetTypeID()) {
        u32 o = obj_of("NSNumber");
        double d = 0;
        long long i = 0;
        if (CFNumberIsFloatType(v)) {
            CFNumberGetValue(v, kCFNumberDoubleType, &d);
            i = (long long)d;
            wr(o + 12, 1, 4);
        } else {
            CFNumberGetValue(v, kCFNumberLongLongType, &i);
            d = (double)i;
        }
        wr(o + 4, (u32)i, 4);
        memcpy(P(o + 8), &d, 8);
        return o;
    }
    if (t == CFArrayGetTypeID()) {
        u32 a = arr_new();
        for (CFIndex i = 0; i < CFArrayGetCount(v); i++) {
            u32 g = cf_to_guest(CFArrayGetValueAtIndex(v, i));
            if (g)
                arr_add(a, g);
        }
        return a;
    }
    if (t == CFDictionaryGetTypeID()) {
        u32 d = dict_new();
        CFDictionaryApplyFunction(v, plist_entry, &d);
        return d;
    }
    if (t == CFDataGetTypeID()) {
        u32 o = obj_of("NSData"), n = (u32)CFDataGetLength(v), p = hmalloc(n + 1);
        memcpy(P(p), CFDataGetBytePtr(v), n);
        wr(o + 4, p, 4);
        wr(o + 8, n, 4);
        return o;
    }
    return 0; // dates etc.: skipped
}
static u32 plist_from_bytes(const void *bytes, size_t n) {
    CFDataRef data = CFDataCreate(NULL, bytes, n);
    u32 g = 0;
    CFPropertyListRef pl = CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, NULL, NULL);
    if (pl) {
        g = cf_to_guest(pl);
        CFRelease(pl);
    }
    CFRelease(data);
    return g;
}
static u32 load_plist(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[plist] cannot read %s\n", path);
        return dict_new();
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc(n);
    size_t got = fread(b, 1, n, f);
    fclose(f);
    u32 g = plist_from_bytes(b, got);
    free(b);
    return g ? g : dict_new();
}
