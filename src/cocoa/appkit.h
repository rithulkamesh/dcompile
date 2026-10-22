// appkit.h: AppKit: the application object, windows, OpenGL views/contexts, screens, events, timers and the app lifecycle.
//
//
// A guest NSWindow/NSOpenGLView/NSOpenGLContext is a small guest object whose +4 word indexes a host object in handles.h; the
// real window and GL context live in host/hostgfx.m. The shim advertises a virtual screen no larger than 1280x800 and resizes
// the host window when the game asks for "full screen".
//
// Event flow: nextEventMatchingMask:... pulls a host event (or a synthetic one, input/inject.h), mirrors it into the fake HID
// devices (input/hid.h) and returns a guest NSEvent; the game then forwards it with sendEvent:, which is routed to the
// delegate (keyDown:, mouseMoved:, storeEvent:). Launch notifications are delivered on the first nextEvent call, and NSTimers
// fire from the run loop.

// AppKit stand-ins (no real window yet)
static int oh_app_delegate(CPU *c, u32 self) {
    wr(self + 4, OA(0), 4);
    return 0;
}
static int
oh_app_finish(CPU *c,
              u32 self) { // deliver the launch notifications a real NSApplication would send the delegate
    u32 del = rd(self + 4, 4);
    int k = del ? class_of_obj(del) : -1;
    if (k < 0)
        return 0;
    static const char *const N[] = {
        "applicationWillBecomeActive:", "applicationDidFinishLaunching:", "applicationDidBecomeActive:"};
    for (int n = 0; n < 3; n++)
        for (int i = 0; i < classes[k].ngm; i++)
            if (!strcmp(classes[k].gm[i].sel, N[n])) {
                u32 args[3] = {del, gstr(N[n]), 0};
                call_guest(c, classes[k].gm[i].imp, 3, args);
            }
    return 0;
}
static int oh_win_setcontent(CPU *c, u32 self) {
    wr(self + 4, OA(0), 4);
    return 0;
}
static int oh_win_content(CPU *c, u32 self) {
    RET(rd(self + 4, 4));
    return 0;
}
static int oh_screen_main(CPU *c, u32 self) {
    RET(singleton("NSScreen"));
    return 0;
}
static int oh_screens(CPU *c, u32 self) {
    u32 a = arr_new();
    arr_add(a, singleton("NSScreen"));
    RET(a);
    return 0;
}
static int oh_screen_frame(CPU *c, u32 self) {
    u32 s = A(0);
    float r[4] = {0, 0, 1280, 800};
    memcpy(P(s), r, 16);
    RET(s);
    return 0;
}
static int oh_empty_dict(CPU *c, u32 self) {
    RET(dict_new());
    return 0;
}
static int oh_fp_zero_point(CPU *c, u32 self) {
    RET(0);
    c->r[EDX] = 0;
    return 0;
}

// ---- real windowing: NSWindow / NSOpenGLView / NSOpenGLContext / NSOpenGLPixelFormat / NSScreen / event pump, backed by hostgfx.m ----
// guest object layout: +4 = host object index (hg_put); NSWindow +8 = content view, +12 = next responder; NSOpenGLView +8 = its context object
#define HGO(o) (hgt[rd((o) + 4, 4)])
static float fa(CPU *c, int n) {
    return Af(c, oa_base + n);
}
static u32 g_cur_ctx_obj;
static int oh_pf_init(CPU *c, u32 self) {
    wr(self + 4, hg_put(hg_pixelformat_new((const int *)gp(OA(0)))), 4);
    RET(self);
    return 0;
}
static int oh_glview_init(CPU *c, u32 self) {
    u32 pf = OA(4);
    wr(self + 4, hg_put(hg_glview_new(fa(c, 0), fa(c, 1), fa(c, 2), fa(c, 3), pf ? HGO(pf) : NULL)), 4);
    RET(self);
    return 0;
}
static int oh_glview_ctx(CPU *c, u32 self) {
    u32 ctx = rd(self + 8, 4);
    if (!ctx) {
        ctx = obj_of("NSOpenGLContext");
        wr(ctx + 4, hg_put(hg_glview_context(HGO(self))), 4);
        wr(self + 8, ctx, 4);
    }
    RET(ctx);
    return 0;
}
static int oh_ctx_make_current(CPU *c, u32 self) {
    hg_ctx_make_current(HGO(self));
    void *cgl = hg_ctx_cgl(HGO(self));
    host_cgl_current = curth->cgl = cgl;
    g_cur_ctx_obj = self;
    return 0;
}
static int oh_ctx_current(CPU *c, u32 self) {
    RET(g_cur_ctx_obj);
    return 0;
}
static int oh_ctx_flush(CPU *c, u32 self) {
    capture_on_present(nflips);
    nflips++;
    hg_ctx_flush(HGO(self));
    return 0;
}
static int oh_ctx_update(CPU *c, u32 self) {
    hg_ctx_update(HGO(self));
    return 0;
}
static int oh_ctx_cgl(CPU *c, u32 self) {
    u32 s = rd(self + 12, 4);
    if (!s) {
        s = hmalloc(16);
        wr(s, cgl_put(hg_ctx_cgl(HGO(self))), 4);
        wr(self + 12, s, 4);
    }
    RET(s);
    return 0;
}
static int oh_ctx_setvalues(CPU *c, u32 self) {
    if (OA(1) == 222)
        hg_ctx_set_swap_interval(HGO(self), (int)rd(OA(0), 4));
    return 0;
} // NSOpenGLCPSwapInterval
static int oh_win_init(CPU *c, u32 self) {
    if (DBG_FLAG("WINLOG"))
        fprintf(stderr, "[win] initWithContentRect %g %g %g %g style=%x\n", fa(c, 0), fa(c, 1), fa(c, 2),
                fa(c, 3), OA(4));
    wr(self + 4, hg_put(hg_window_new(fa(c, 0), fa(c, 1), fa(c, 2), fa(c, 3), OA(4), "Assassin's Creed II")),
       4);
    RET(self);
    return 0;
}
static int oh_win_frame(CPU *c, u32 self) {
    u32 s = A(0);
    float f[4];
    hg_window_frame(HGO(self), f);
    memcpy(P(s), f, 16);
    RET(s);
    return 0;
}
static int oh_view_frame(CPU *c, u32 self) {
    u32 s = A(0);
    float f[4];
    hg_view_frame(HGO(self), f);
    memcpy(P(s), f, 16);
    RET(s);
    return 0;
}
static int oh_win_setsize(CPU *c, u32 self) {
    if (DBG_FLAG("WINLOG"))
        fprintf(stderr, "[win] setContentSize %g %g\n", fa(c, 0), fa(c, 1));
    hg_window_set_content_size(HGO(self), fa(c, 0), fa(c, 1));
    return 0;
}
static int oh_win_show(CPU *c, u32 self) {
    hg_window_show(HGO(self));
    return 0;
}
static int oh_win_center(CPU *c, u32 self) {
    hg_window_center(HGO(self));
    return 0;
}
static int oh_win_setcontent2(CPU *c, u32 self) {
    u32 v = OA(0);
    wr(self + 8, v, 4);
    if (v)
        hg_window_set_content(HGO(self), HGO(v));
    return 0;
}
static int oh_win_content2(CPU *c, u32 self) {
    RET(rd(self + 8, 4));
    return 0;
}
static int oh_win_setnext(CPU *c, u32 self) {
    wr(self + 12, OA(0), 4);
    return 0;
}
static int oh_win_next(CPU *c, u32 self) {
    RET(rd(self + 12, 4));
    return 0;
}
// The game sizes its render surface from the "screen". We present it in a window, so advertise a screen that fits on any display.
static void virtual_screen(float f[4]) {
    hg_screen_frame(f);
    if (f[2] > 1280)
        f[2] = 1280;
    if (f[3] > 800)
        f[3] = 800;
    f[0] = f[1] = 0;
}
static int oh_screen_frame2(CPU *c, u32 self) {
    u32 s = A(0);
    float f[4];
    virtual_screen(f);
    memcpy(P(s), f, 16);
    RET(s);
    return 0;
}
static int oh_view_fullscreen(CPU *c, u32 self) {
    float f[4];
    virtual_screen(f);
    hg_view_window_resize(HGO(self), f[2], f[3]);
    RET(1);
    return 0;
}
static int oh_screen_desc(CPU *c, u32 self) {
    u32 d = dict_new(), n = obj_of("NSNumber");
    wr(n + 4, hg_main_display_id(), 4);
    dict_set(d, nsstr("NSScreenNumber"), n);
    RET(d);
    return 0;
}
// events: guest NSEvent = { isa, type, characters, buttonNumber, dx, dy, dz (floats), keyCode }
enum {
    NSE_LDOWN = 1,
    NSE_LUP = 2,
    NSE_RDOWN = 3,
    NSE_RUP = 4,
    NSE_MOVED = 5,
    NSE_KDOWN = 10,
    NSE_KUP = 11,
    NSE_FLAGS = 12,
    NSE_SCROLL = 22,
    NSE_ODOWN = 25,
    NSE_OUP = 26
};
static struct {
    u32 obj;
    u64 next_ns;
} timers[16];
static int ntimers;
static void hid_key(int keycode, int down);
static void hid_modifiers(int flags);
static void hid_mouse(int type, int button, float dx, float dy);
// runs due NSTimers whose target is a guest object (the game's heartBeatTimer:)
static void fire_timers(CPU *c) {
    u64 now = mono_ns();
    static int calls;
    if (DBG_FLAG("TIMERLOG") && calls++ < 3)
        fprintf(stderr, "[objc] fire_timers: %d timers, now=%llu\n", ntimers, (unsigned long long)now);
    for (int i = 0; i < ntimers; i++) {
        if (now < timers[i].next_ns)
            continue;
        u32 t = timers[i].obj, target = rd(t + 4, 4), sel = rd(t + 8, 4), repeats = rd(t + 24, 4);
        double iv;
        memcpy(&iv, P(t + 16), 8);
        if (repeats)
            timers[i].next_ns = now + (u64)(iv * 1e9);
        else {
            memmove(&timers[i], &timers[i + 1], (--ntimers - i) * sizeof timers[0]);
            i--;
        }
        int k = target ? class_of_obj(target) : -1, found = 0;
        static int nfired;
        if (DBG_FLAG("TIMERLOG") && nfired++ < 5)
            fprintf(stderr, "[objc] timer fires %s on %08x (class %d)\n", GS(sel), target, k);
        for (int kk = k; kk >= 0 && !found; kk = classes[kk].super)
            for (int j = 0; j < classes[kk].ngm; j++)
                if (!strcmp(classes[kk].gm[j].sel, GS(sel))) {
                    u32 args[3] = {target, sel, t};
                    call_guest(c, classes[kk].gm[j].imp, 3, args);
                    found = 1;
                    break;
                }
    }
}
static void deliver_launch_notifications(
    CPU *c, u32 app) { // what AppKit posts once the app activates: delegate hears about launch + activation
    u32 del = rd(app + 4, 4), k = del ? class_of_obj(del) : (u32)-1;
    if ((int)k < 0 || rd(app + 8, 4))
        return;
    wr(app + 8, 1, 4);
    static const char *const N[] = {
        "applicationWillBecomeActive:", "applicationDidFinishLaunching:", "applicationDidBecomeActive:"};
    for (int n = 0; n < 3; n++)
        for (int i = 0; i < classes[k].ngm; i++)
            if (!strcmp(classes[k].gm[i].sel, N[n])) {
                u32 args[3] = {del, gstr(N[n]), 0};
                call_guest(c, classes[k].gm[i].imp, 3, args);
            }
}
static int oh_app_nextevent(CPU *c, u32 self) {
    deliver_launch_notifications(c, self);
    HGEvent e;
    if (!inject_next_event(&e) && !hg_next_event(0, &e)) {
        RET(0);
        return 0;
    }
    if (DBG_FLAG("EVLOG") && e.type != 5 && e.type != 6)
        fprintf(stderr, "[ev] host event type=%d key=%d button=%d chars='%s' d=%g,%g\n", e.type, e.keycode,
                e.button, e.chars, e.dx, e.dy);
    if (e.type == 1 || e.type == 2)
        hid_key(e.keycode, e.type == 1);
    else if (e.type == 7)
        hid_modifiers(e.flags);
    else
        hid_mouse(e.type, e.button, e.dx, e.dy);
    u32 o = obj_of("NSEvent");
    int ty = 0;
    switch (e.type) {
    case 1:
        ty = NSE_KDOWN;
        break;
    case 2:
        ty = NSE_KUP;
        break;
    case 3:
        ty = e.button == 0 ? NSE_LDOWN : e.button == 1 ? NSE_RDOWN : NSE_ODOWN;
        break;
    case 4:
        ty = e.button == 0 ? NSE_LUP : e.button == 1 ? NSE_RUP : NSE_OUP;
        break;
    case 5:
        ty = NSE_MOVED;
        break;
    case 6:
        ty = NSE_SCROLL;
        break;
    case 7:
        ty = NSE_FLAGS;
        break;
    default:
        RET(0);
        return 0;
    }
    wr(o + 4, ty, 4);
    wr(o + 8, nsstr(e.chars), 4);
    wr(o + 12, e.button, 4);
    memcpy(P(o + 16), &e.dx, 4);
    memcpy(P(o + 20), &e.dy, 4);
    memcpy(P(o + 24), &e.dz, 4);
    wr(o + 28, e.keycode, 4);
    RET(o);
    return 0;
}
// route to the delegate (the game's responder) like the real responder chain
static int oh_app_send(CPU *c, u32 self) {
    u32 ev = OA(0), del = rd(self + 4, 4);
    if (!ev || !del)
        return 0;
    u32 ty = rd(ev + 4, 4);
    if (DBG_FLAG("EVLOG") && ty != NSE_MOVED && ty != NSE_SCROLL)
        fprintf(stderr, "[ev] sendEvent type=%u delegate=%08x\n", ty, del);
    const char *sel = ty == NSE_KDOWN   ? "keyDown:"
                      : ty == NSE_KUP   ? "keyUp:"
                      : ty == NSE_MOVED ? "mouseMoved:"
                                        : "storeEvent:";
    int k = class_of_obj(del);
    for (int j = 0; k >= 0 && j < classes[k].ngm; j++)
        if (!strcmp(classes[k].gm[j].sel, sel)) {
            u32 args[3] = {del, gstr(sel), ev};
            call_guest(c, classes[k].gm[j].imp, 3, args);
            break;
        }
    return 0;
}
static int oh_ev_type(CPU *c, u32 self) {
    RET(rd(self + 4, 4));
    return 0;
}
static int oh_ev_chars(CPU *c, u32 self) {
    RET(rd(self + 8, 4));
    return 0;
}
static int oh_ev_button(CPU *c, u32 self) {
    RET(rd(self + 12, 4));
    return 0;
}
static int oh_ev_dx(CPU *c, u32 self) {
    float f;
    memcpy(&f, P(self + 16), 4);
    fpush(c, f);
    return 0;
}
static int oh_ev_dy(CPU *c, u32 self) {
    float f;
    memcpy(&f, P(self + 20), 4);
    fpush(c, f);
    return 0;
}
static int oh_ev_dz(CPU *c, u32 self) {
    float f;
    memcpy(&f, P(self + 24), 4);
    fpush(c, f);
    return 0;
}
static int oh_ev_mouseloc(CPU *c, u32 self) {
    float p[2];
    hg_mouse_location(p);
    u32 w[2];
    memcpy(w, p, 8);
    RET(w[0]);
    c->r[EDX] = w[1];
    return 0;
}
static int oh_rl_addtimer(CPU *c, u32 self) {
    if (DBG_FLAG("TIMERLOG")) {
        double iv;
        memcpy(&iv, P(OA(0) + 16), 8);
        fprintf(stderr, "[objc] addTimer %08x interval=%g target=%08x sel=%s repeats=%u ntimers=%d\n", OA(0),
                iv, rd(OA(0) + 4, 4), GS(rd(OA(0) + 8, 4)), rd(OA(0) + 24, 4), ntimers);
    }
    if (ntimers < 16) {
        double iv;
        memcpy(&iv, P(OA(0) + 16), 8);
        timers[ntimers].obj = OA(0);
        timers[ntimers].next_ns = mono_ns() + (u64)(iv * 1e9);
        ntimers++;
    }
    return 0;
}
