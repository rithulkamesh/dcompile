// display.h: CoreGraphics display and CGL entry points.
//
// The game enumerates and "switches" display modes; the window stays windowed, so the mode list is synthetic. CGLContextObj values
// are accepted in both shapes the game holds them in: the handle itself, or a pointer to a struct whose first word is the handle.

static void *cgl_get(u32 a) {
    if (!a)
        return NULL;
    if (a < ncgl)
        return cglt[a];
    u32 h = rd(a, 4);
    return h && h < ncgl ? cglt[h] : NULL;
}
H(CGLCreateContext) {
    CGLContextObj ctx = NULL;
    CGLError e = CGLCreateContext(cgl_get(A(0)), cgl_get(A(1)), &ctx);
    if (!e)
        wr(A(2), cgl_put(ctx), 4);
    RET(e);
    return 0;
}
H(CGLGetPixelFormat) {
    RET(cgl_put(CGLGetPixelFormat(cgl_get(A(0)))));
    return 0;
}
H(CGLReleaseContext) {
    return 0;
}
H(CGLSetCurrentContext) {
    set_current_cgl(cgl_get(A(0)));
    RET(0);
    return 0;
}

// ---- displays: the game enumerates and switches modes; we stay windowed, so the mode list is synthetic ----
static const struct {
    int w, h;
} MODES[] = {{800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1440, 900}, {1680, 1050}, {1920, 1080}};
static u32 mode_dict(int w, int h) {
    u32 d = dict_new();
    static const struct {
        const char *k;
        int v;
    } kv[] = {{"BitsPerPixel", 32},
              {"RefreshRate", 60},
              {"kCGDisplayModeIsSafeForHardware", 1},
              {"kCGDisplayModeIsStretched", 0}};
    u32 n;
    n = obj_of("NSNumber");
    wr(n + 4, w, 4);
    {
        double v = w;
        memcpy(P(n + 8), &v, 8);
    }
    dict_set(d, nsstr("Width"), n);
    n = obj_of("NSNumber");
    wr(n + 4, h, 4);
    {
        double v = h;
        memcpy(P(n + 8), &v, 8);
    }
    dict_set(d, nsstr("Height"), n);
    for (int i = 0; i < 4; i++) {
        n = obj_of("NSNumber");
        wr(n + 4, kv[i].v, 4);
        double v = kv[i].v;
        memcpy(P(n + 8), &v, 8);
        dict_set(d, nsstr(kv[i].k), n);
    }
    return d;
}
H(CGGetActiveDisplayList) {
    u32 id = hg_main_display_id();
    if (A(1))
        wr(A(1), id, 4);
    if (A(2))
        wr(A(2), 1, 4);
    RET(0);
    return 0;
}
H(CGDisplayAvailableModes) {
    u32 a = arr_new();
    for (size_t i = 0; i < sizeof MODES / sizeof *MODES; i++)
        arr_add(a, mode_dict(MODES[i].w, MODES[i].h));
    RET(a);
    return 0;
}
H(CGDisplayCurrentMode) {
    float f[4];
    hg_screen_frame(f);
    RET(mode_dict(f[2] > 1280 ? 1280 : (int)f[2], f[3] > 800 ? 800 : (int)f[3]));
    return 0;
}
H(CGDisplaySwitchToMode) {
    RET(0);
    return 0;
}
H(CGCaptureAllDisplays) {
    RET(0);
    return 0;
}
H(CGReleaseAllDisplays) {
    RET(0);
    return 0;
}
H(CGDisplayIsCaptured) {
    RET(0);
    return 0;
}
H(CGDisplayHideCursor) {
    hg_set_cursor_visible(0);
    RET(0);
    return 0;
}
H(CGDisplayShowCursor) {
    hg_set_cursor_visible(1);
    RET(0);
    return 0;
}
H(CGEventCreateMouseEvent) {
    RET(0x55000);
    return 0;
}
H(CGEventCreateSourceFromEvent) {
    RET(0x55001);
    return 0;
}
H(CGEventPost) {
    return 0;
}
H(CGRectGetMidX) {
    float x = Af(c, 0), w = Af(c, 2);
    fpush(c, x + w / 2);
    return 0;
} // CGRect {origin.x, origin.y, size.w, size.h} floats
H(CGRectGetMidY) {
    float y = Af(c, 1), h = Af(c, 3);
    fpush(c, y + h / 2);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry display_imports[] = {
    IMPORT(CGLCreateContext), IMPORT(CGLGetPixelFormat), IMPORT(CGLReleaseContext),
    IMPORT(CGLSetCurrentContext), IMPORT(CGGetActiveDisplayList), IMPORT(CGDisplayAvailableModes),
    IMPORT(CGDisplayCurrentMode), IMPORT(CGDisplaySwitchToMode), IMPORT(CGCaptureAllDisplays),
    IMPORT(CGReleaseAllDisplays), IMPORT(CGDisplayIsCaptured), IMPORT(CGDisplayHideCursor),
    IMPORT(CGDisplayShowCursor), IMPORT(CGEventCreateMouseEvent), IMPORT(CGEventCreateSourceFromEvent),
    IMPORT(CGEventPost), IMPORT(CGRectGetMidX), IMPORT(CGRectGetMidY),
    {0, 0}};
// clang-format on
