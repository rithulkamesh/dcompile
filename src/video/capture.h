// capture.h: frame capture for debugging.
//
// kill -USR2 <pid> writes the next presented frame to /tmp/dc_shot.ppm, traces every GL call of the following frame to
// /tmp/dc_gl.log and dumps the mid-sized 2D textures after it to /tmp/dc_tex/<id>.ppm (convert with `sips -s format png`).
// DC_SHOTFRAME=n DC_SHOT=path writes frame n of a run to `path` instead. All of it runs on the render thread at buffer-swap time.

static FILE *gl_logf; // one-frame GL trace, written by gl.h while non-NULL
static volatile sig_atomic_t capture_requested;
static void on_usr2(int sig) {
    capture_requested = 1;
}

static void capture_frame(const char *path) { // writes the back buffer as a binary PPM
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    GLint w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return;
    u8 *px = malloc((size_t)w * h * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(vp[0], vp[1], w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++)
            fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
    fclose(f);
    free(px);
    fprintf(stderr, "[capture] wrote %s (%dx%d)\n", path, w, h);
}
static void dump_textures(
    void) { // after a traced frame: every mid-size 2D texture as /tmp/dc_tex/<id>.ppm, to see which pass went wrong
    mkdir("/tmp/dc_tex", 0755);
    GLint act, old;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old);
    int n = 0;
    while (glGetError())
        ;
    for (GLuint id = 1; id < 400 && n < 80; id++) {
        if (!glIsTexture(id))
            continue;
        glBindTexture(GL_TEXTURE_2D, id);
        GLint w = 0, h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        if (w < 128 || h < 64 || w > 2048 || h > 2048)
            continue;
        u8 *px = malloc((size_t)w * h * 4);
        while (glGetError())
            ;
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (!glGetError()) {
            char p[64];
            snprintf(p, sizeof p, "/tmp/dc_tex/%u.ppm", id);
            FILE *f = fopen(p, "wb");
            if (f) {
                fprintf(f, "P6 %d %d 255\n", w, h);
                for (int y = h - 1; y >= 0; y--)
                    for (int x = 0; x < w; x++)
                        fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
                fclose(f);
                n++;
            }
        }
        free(px);
    }
    glBindTexture(GL_TEXTURE_2D, old);
    glActiveTexture(act);
    fprintf(stderr, "[capture] dumped %d textures to /tmp/dc_tex\n", n);
}

// Called for every presented frame, before the buffer swap.
static void capture_on_present(u64 frame) {
    static int armed;
    if (!armed) {
        armed = 1;
        signal(SIGUSR2, on_usr2);
    }
    if (gl_logf) { // the traced frame has ended
        fclose(gl_logf);
        gl_logf = NULL;
        fprintf(stderr, "[capture] GL trace of one frame in /tmp/dc_gl.log\n");
        dump_textures();
    }
    if (capture_requested) {
        capture_requested = 0;
        capture_frame("/tmp/dc_shot.ppm");
        gl_logf = fopen("/tmp/dc_gl.log", "w"); // trace the next frame
    }
    if (dbg_env("SHOTFRAME") && dbg_env("SHOT") && frame == (u64)atoi(dbg_env("SHOTFRAME")))
        capture_frame(dbg_env("SHOT"));
}
