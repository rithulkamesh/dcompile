// handles.h: tables mapping 32-bit guest handles to host objects (windows, views, GL contexts).
//
// The guest only ever sees small integers; the host objects behind them are never freed.

static HG hgt[512];
static u32 nhg = 1; // host objects the guest holds by index (never freed)
static u32 hg_put(HG h) {
    hgt[nhg] = h;
    return nhg++;
}
static void *cglt[64];
static u32 ncgl = 1; // CGLContextObj / CGLPixelFormatObj handles
static u32 cgl_put(void *p) {
    for (u32 i = 1; i < ncgl; i++)
        if (cglt[i] == p)
            return i;
    cglt[ncgl] = p;
    return ncgl++;
}
static void set_current_cgl(void *ctx) {
    curth->cgl = ctx;
    if (ctx != host_cgl_current) {
        CGLSetCurrentContext(ctx);
        host_cgl_current = ctx;
    }
}

static const void *gp(u32 a) {
    return a ? (const void *)P(a) : NULL;
} // guest pointer -> host pointer
