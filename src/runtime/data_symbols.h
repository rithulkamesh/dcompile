// data_symbols.h: imported *variables* (as opposed to functions).
//
// Symbols such as stdin/stdout/stderr, the stack-protector guard, vtables/typeinfo of imported C++ classes and Darwin's
// _DefaultRuneLocale (ctype macros index its table directly) are resolved to storage in guest memory by the loader.

// Darwin ctype macros index _DefaultRuneLocale.__runetype[] directly, so it must exist in guest memory.
// i386 layout: magic[8] encoding[32] 3 fn/int fields (12) -> __runetype[256] u32 at +52, then __maplower[256], __mapupper[256].
static u32 make_runelocale(void) {
    static const u32 masks[] = {0x100,  0x200,  0x400,   0x800,   0x1000, 0x2000,
                                0x4000, 0x8000, 0x10000, 0x20000, 0x40000};
    u32 a = data_alloc(0x1000);
    memcpy(GS(a), "RuneMagA", 8);
    for (int ch = 0; ch < 128; ch++) {
        u32 t = 0;
        for (int i = 0; i < 11; i++)
            if (__maskrune(ch, masks[i]))
                t |= masks[i];
        if (isdigit(ch))
            t |= ch - '0';
        else if (isxdigit(ch))
            t |= tolower(ch) - 'a' + 10;
        wr(a + 52 + 4 * ch, t, 4);
        wr(a + 52 + 1024 + 4 * ch, tolower(ch), 4);
        wr(a + 52 + 2048 + 4 * ch, toupper(ch), 4);
    }
    for (int ch = 128; ch < 256; ch++) {
        wr(a + 52 + 1024 + 4 * ch, ch, 4);
        wr(a + 52 + 2048 + 4 * ch, ch, 4);
    }
    return a;
}

// data symbols (variables, not functions): returns guest storage address or 0 if `name` is not one
static u32 data_sym(const char *name) {
    static struct {
        const char *n;
        u32 a;
    } done[256];
    static int nd;
    for (int i = 0; i < nd; i++)
        if (!strcmp(done[i].n, name))
            return done[i].a;
    u32 a = 0;
    if (!strcmp(name, "___stdinp") || !strcmp(name, "___stdoutp") || !strcmp(name, "___stderrp")) {
        if (!nfiles) {
            file_new(stdin);
            file_new(stdout);
            file_new(stderr);
        }
        a = data_alloc(4);
        wr(a, FILE_BASE + 0x100 * (name[5] == 'i' ? 0 : name[5] == 'o' ? 1 : 2), 4);
    } else if (!strcmp(name, "___stack_chk_guard")) {
        a = data_alloc(4);
        wr(a, 0x00ff0a0d, 4);
    } else if (!strcmp(name, "_mach_task_self_")) {
        a = data_alloc(4);
        wr(a, 0x103, 4);
    } else if (!strcmp(name, "___mb_cur_max")) {
        a = data_alloc(4);
        wr(a, 1, 4);
    } else if (!strncmp(name, "__ZTV", 5) || !strncmp(name, "__ZTT", 5) || !strncmp(name, "__ZTI", 5) ||
               !strcmp(name, "__ZNSs4_Rep20_S_empty_rep_storageE"))
        a = data_alloc(
            0x200); // vtables / typeinfo / std::string's shared empty rep (all-zero is a valid empty rep)
    else if (!strcmp(name, "_DefaultRuneLocale") || !strcmp(name, "__DefaultRuneLocale"))
        a = make_runelocale();
    else if (!strncmp(name, ".objc_class_name_", 17) || !strncmp(name, "_kCF", 4) ||
             !strncmp(name, "___CF", 5) || !strcmp(name, "_NSApp") ||
             !strcmp(name, "_NSDefaultRunLoopMode") || !strcmp(name, "_NSFullScreenModeSetting") ||
             !strcmp(name, "_kIOMasterPortDefault"))
        a = data_alloc(0x400);
    if (a) {
        done[nd].n = strdup(name);
        done[nd++].a = a;
    }
    return a;
}
