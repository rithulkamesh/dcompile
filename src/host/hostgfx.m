// hostgfx.m: AppKit / NSOpenGL glue for the emulator (see hostgfx.h). Compile with -fno-objc-arc.
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#include "hostgfx.h"

// HGEvent.type
enum {
    HGE_NONE,
    HGE_KEY_DOWN,
    HGE_KEY_UP,
    HGE_MOUSE_DOWN,
    HGE_MOUSE_UP,
    HGE_MOUSE_MOVED,
    HGE_SCROLL,
    HGE_KEY_FLAGS,
    HGE_QUIT
};

void hg_init(void) {
    static int done;
    if (done)
        return;
    done = 1;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

HG hg_pixelformat_new(const int *attrs) {
    NSOpenGLPixelFormatAttribute def[] = {NSOpenGLPFAAccelerated,
                                          NSOpenGLPFADoubleBuffer,
                                          NSOpenGLPFAColorSize,
                                          32,
                                          NSOpenGLPFADepthSize,
                                          24,
                                          NSOpenGLPFAStencilSize,
                                          8,
                                          NSOpenGLPFAAllowOfflineRenderers,
                                          0};
    NSOpenGLPixelFormat *pf = nil;
    if (attrs) {
        NSOpenGLPixelFormatAttribute a[64];
        int n = 0;
        for (; attrs[n] && n < 60; n++)
            a[n] = (NSOpenGLPixelFormatAttribute)attrs[n]; // attributes with a value take two slots
        for (int i = 0; i < n; i++) { // re-walk so values are not mistaken for the terminator
            (void)i;
        }
        a[n] = 0;
        pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:a];
    }
    if (!pf)
        pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:def];
    return pf;
}

HG hg_glview_new(float x, float y, float w, float h, HG pf) {
    NSOpenGLView *v = [[NSOpenGLView alloc] initWithFrame:NSMakeRect(x, y, w, h)
                                              pixelFormat:(NSOpenGLPixelFormat *)pf];
    [v setWantsBestResolutionOpenGLSurface:NO]; // the game works in 1x pixels
    return v;
}
HG hg_glview_context(HG view) {
    return [(NSOpenGLView *)view openGLContext];
}
HG hg_context_new(HG pf, HG share) {
    return [[NSOpenGLContext alloc] initWithFormat:(NSOpenGLPixelFormat *)pf
                                      shareContext:(NSOpenGLContext *)share];
}

HG hg_window_new(float x, float y, float w, float h, unsigned style, const char *title) {
    unsigned st =
        (style & NSWindowStyleMaskTitled)
            ? (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
            : 0;
    NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(x, y, w, h)
                                                styleMask:st
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [win setTitle:[NSString stringWithUTF8String:title ? title : "Assassin's Creed II"]];
    [win setReleasedWhenClosed:NO];
    [win setAcceptsMouseMovedEvents:YES];
    return win;
}
void hg_window_set_content(HG win, HG view) {
    [(NSWindow *)win setContentView:(NSView *)view];
    [(NSWindow *)win makeFirstResponder:(NSView *)view];
}
void hg_window_show(HG win) {
    [(NSWindow *)win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
void hg_window_set_content_size(HG win, float w, float h) {
    [(NSWindow *)win setContentSize:NSMakeSize(w, h)];
}
void hg_view_window_resize(HG view, float w, float h) {
    NSWindow *win = [(NSView *)view window];
    if (!win)
        return;
    [win setContentSize:NSMakeSize(w, h)];
    [win center];
}
void hg_window_center(HG win) {
    [(NSWindow *)win center];
}
void hg_window_frame(HG win, float out[4]) {
    NSRect r = [(NSWindow *)win frame];
    out[0] = r.origin.x;
    out[1] = r.origin.y;
    out[2] = r.size.width;
    out[3] = r.size.height;
}
void hg_view_frame(HG view, float out[4]) {
    NSRect r = [(NSView *)view frame];
    out[0] = r.origin.x;
    out[1] = r.origin.y;
    out[2] = r.size.width;
    out[3] = r.size.height;
}

void hg_ctx_make_current(HG ctx) {
    if (ctx)
        [(NSOpenGLContext *)ctx makeCurrentContext];
    else
        [NSOpenGLContext clearCurrentContext];
}
void hg_ctx_flush(HG ctx) {
    [(NSOpenGLContext *)ctx flushBuffer];
}
void hg_ctx_update(HG ctx) {
    [(NSOpenGLContext *)ctx update];
}
void hg_ctx_set_view(HG ctx, HG view) {
    [(NSOpenGLContext *)ctx setView:(NSView *)view];
}
void *hg_ctx_cgl(HG ctx) {
    return [(NSOpenGLContext *)ctx CGLContextObj];
}
void hg_ctx_set_swap_interval(HG ctx, int v) {
    GLint i = v;
    [(NSOpenGLContext *)ctx setValues:&i forParameter:NSOpenGLContextParameterSwapInterval];
}

void hg_screen_frame(float out[4]) {
    NSRect r = [[NSScreen mainScreen] frame];
    out[0] = r.origin.x;
    out[1] = r.origin.y;
    out[2] = r.size.width;
    out[3] = r.size.height;
}
unsigned hg_main_display_id(void) {
    return [[[[NSScreen mainScreen] deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue];
}

int hg_next_event(double timeout_s, HGEvent *ev) {
    @autoreleasepool {
        NSDate *until =
            timeout_s > 0 ? [NSDate dateWithTimeIntervalSinceNow:timeout_s] : [NSDate distantPast];
        for (;;) {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:until
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (!e)
                return 0;
            memset(ev, 0, sizeof *ev);
            NSPoint p = [e locationInWindow];
            ev->x = p.x;
            ev->y = p.y;
            ev->flags = (int)[e modifierFlags];
            switch ([e type]) {
            case NSEventTypeKeyDown:
            case NSEventTypeKeyUp: {
                ev->type = [e type] == NSEventTypeKeyDown ? HGE_KEY_DOWN : HGE_KEY_UP;
                ev->keycode = [e keyCode];
                NSString *s = [e characters];
                if (s)
                    strlcpy(ev->chars, [s UTF8String], sizeof ev->chars);
                return 1;
            }
            case NSEventTypeLeftMouseDown:
            case NSEventTypeRightMouseDown:
            case NSEventTypeOtherMouseDown:
                ev->type = HGE_MOUSE_DOWN;
                ev->button = (int)[e buttonNumber];
                [NSApp sendEvent:e];
                return 1;
            case NSEventTypeLeftMouseUp:
            case NSEventTypeRightMouseUp:
            case NSEventTypeOtherMouseUp:
                ev->type = HGE_MOUSE_UP;
                ev->button = (int)[e buttonNumber];
                [NSApp sendEvent:e];
                return 1;
            case NSEventTypeMouseMoved:
            case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged:
                ev->type = HGE_MOUSE_MOVED;
                ev->dx = [e deltaX];
                ev->dy = [e deltaY];
                return 1;
            case NSEventTypeScrollWheel:
                ev->type = HGE_SCROLL;
                ev->dx = [e deltaX];
                ev->dy = [e deltaY];
                return 1;
            case NSEventTypeFlagsChanged:
                ev->type = HGE_KEY_FLAGS;
                return 1;
            default:
                [NSApp sendEvent:e];
                until = [NSDate distantPast];
                break; // window chrome, activation, etc.
            }
        }
    }
}
void hg_mouse_location(float out[2]) {
    NSPoint p = [NSEvent mouseLocation];
    out[0] = p.x;
    out[1] = p.y;
}
// The game hides the cursor and drives its own pointer from mouse deltas (its CGEventPost re-centring is stubbed), so "hidden" means
// "captured": freeze the cursor in place while deltas keep flowing, and hand it back whenever the app loses focus.
static int cur_locked, cur_hidden;
static void cur_apply(int lock) {
    if (lock && !cur_hidden) {
        [NSCursor hide];
        cur_hidden = 1;
    } else if (!lock && cur_hidden) {
        [NSCursor unhide];
        cur_hidden = 0;
    }
    CGAssociateMouseAndMouseCursorPosition(!lock);
}
void hg_set_cursor_visible(int v) {
    static int observing;
    if (!observing) {
        observing = 1;
        [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification
                                                          object:nil
                                                           queue:nil
                                                      usingBlock:^(NSNotification *n) {
                                                        if (cur_locked)
                                                            cur_apply(0);
                                                      }];
        [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidBecomeActiveNotification
                                                          object:nil
                                                           queue:nil
                                                      usingBlock:^(NSNotification *n) {
                                                        if (cur_locked)
                                                            cur_apply(1);
                                                      }];
    }
    cur_locked = !v;
    cur_apply(cur_locked);
}
void hg_warp_mouse(float x, float y) {
    CGWarpMouseCursorPosition(CGPointMake(x, y));
}
