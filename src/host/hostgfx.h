// hostgfx.h: the host (arm64 macOS) side of windowing, OpenGL contexts and input. Implemented in hostgfx.m (Cocoa/AppKit);
// everything is plain C so the emulator core never sees Objective-C. All calls must be made on the main thread.
#ifndef HOSTGFX_H
#define HOSTGFX_H
typedef void *HG; // retained host object (NSWindow, NSOpenGLView, NSOpenGLContext, ...)
typedef struct {
    int type;
    float x, y, dx, dy, dz;
    int button, keycode, flags;
    char chars[16];
} HGEvent; // type: see hostgfx.m

void hg_init(void);
HG hg_pixelformat_new(
    const int *attrs); // NSOpenGLPixelFormat from a 0-terminated NSOpenGLPFA* list (NULL = defaults)
HG hg_glview_new(float x, float y, float w, float h, HG pf);
HG hg_glview_context(HG view);
HG hg_context_new(HG pf, HG share);
HG hg_window_new(float x, float y, float w, float h, unsigned style, const char *title);
void hg_window_set_content(HG win, HG view);
void hg_window_show(HG win);
void hg_window_center(HG win);
void hg_view_window_resize(HG view, float w, float h); // resize (and re-centre) the window that hosts `view`
void hg_window_set_content_size(HG win, float w, float h);
void hg_window_frame(HG win, float out[4]);
void hg_view_frame(HG view, float out[4]);
void hg_ctx_make_current(HG ctx);
void hg_ctx_flush(HG ctx);
void hg_ctx_update(HG ctx);
void hg_ctx_set_view(HG ctx, HG view);
void *hg_ctx_cgl(HG ctx);
void hg_ctx_set_swap_interval(HG ctx, int v);
void hg_screen_frame(float out[4]);
unsigned hg_main_display_id(void);
int hg_next_event(
    double timeout_s,
    HGEvent *ev); // pumps the host event loop (window chrome included); 1 if a key/mouse event is returned
void hg_mouse_location(float out[2]);
void hg_set_cursor_visible(int v);
void hg_warp_mouse(float x, float y);
#endif
