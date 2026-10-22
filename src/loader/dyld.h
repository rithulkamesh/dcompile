// dyld.h: dlopen / dlsym for guest code.
//
// Loads further i386 dylibs (the Ubisoft libraries) into the same guest address space and runs their initialisers. The Orbit
// client library is replaced by the offline stand-in in online/orbit.h.

static const char *dl_err = "";
static u32 g_argc, g_argv, g_envp, g_apple; // handed to image initializers like dyld does

static u32 dl_open(CPU *c, const char *path) {
    char real[PATH_MAX];
    if (strstr(path, "ubiorbitapi_r2.dylib"))
        return FAKE_ORBIT; // see orbit.h
    if (!realpath(path, real)) {
        dl_err = "image not found";
        return 0;
    }
    for (int i = 0; i < nimages; i++) {
        char r2[PATH_MAX];
        if (realpath(images[i].path, r2) && !strcmp(r2, real))
            return IMAGE_HANDLE_BASE + 16 * i;
    }
    Image *im = load_image(real, DYLIB_BASE + DYLIB_STRIDE * (nimages - 1), 0);
    if (!im) {
        dl_err = "not an i386 Mach-O image";
        return 0;
    }
    objc_image(objc_class_sec, objc_class_size, objc_cls_refs, objc_cls_refs_size);
    for (u32 k = 0; k < im->init_size / 4; k++) {
        u32 args[4] = {g_argc, g_argv, g_envp, g_apple};
        call_guest(c, rd(im->init_addr + 4 * k, 4), 4, args);
    }
    return IMAGE_HANDLE_BASE + 16 * (u32)(im - images);
}
static u32 dl_sym(u32 handle, const char *name) { // `name` has no leading underscore, as in dlsym(3)
    char mangled[512];
    snprintf(mangled, sizeof mangled, "_%s", name);
    if (handle == FAKE_ORBIT) {
        char n[512];
        snprintf(n, sizeof n, "_%s", name);
        return !strncmp(name, "MgOrbitdll", 10) ? thunk_for(n) : 0;
    }
    u32 idx = (handle - IMAGE_HANDLE_BASE) / 16;
    if (handle >= IMAGE_HANDLE_BASE && idx < (u32)nimages) {
        u32 a = st_lookup(&images[idx].syms, mangled);
        if (!a)
            dl_err = "symbol not found";
        return a;
    }
    for (int i = 0; i < nimages; i++) {
        u32 a = st_lookup(&images[i].syms, mangled);
        if (a)
            return a;
    } // RTLD_DEFAULT & friends
    if (lookup_host(mangled))
        return thunk_for(mangled);
    dl_err = "symbol not found";
    return 0;
}
