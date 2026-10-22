// imports.h: import registry and dispatch.
//
// Each module exposes a table of ImportEntry {symbol, handler}. lookup_host() searches the tables (a symbol ending in `*` matches
// by prefix; `$UNIX2003`-style suffixes are ignored) and serve() runs the handler for the thunk the guest just reached.
// Resolution is cached per thunk. Unresolved imports stop the run with an "UNIMPLEMENTED IMPORT" report.

static const ImportEntry *const import_tables[] = {
    libc_string_imports, libc_wide_imports, libc_printf_imports, libc_stdio_imports, libc_process_imports,
    libc_atomic_imports, libc_math_imports, heap_imports,        threads_imports,    objc_imports,
    cf_imports,          posix_imports,     cxx_imports,         stubs_imports,      fs_imports,
    orbit_imports,       audio_imports,     bink_imports,        gl_imports,         display_imports,
    hid_imports,
};

static HostFn lookup_host(const char *name) {
    char base[128];
    snprintf(base, sizeof base, "%s", name);
    char *dollar = strchr(base, '$');
    if (dollar)
        *dollar = 0; // foo$UNIX2003 -> foo
    size_t len = strlen(base);
    for (size_t t = 0; t < sizeof import_tables / sizeof *import_tables; t++)
        for (const ImportEntry *e = import_tables[t]; e->name; e++) {
            size_t n = strlen(e->name);
            if (e->name[n - 1] == '*' ? !strncmp(base, e->name, n - 1)
                                      : (n == len && !memcmp(base, e->name, n)))
                return e->fn;
        }
    return NULL;
}

static HostFn host_fn[4096];
static char host_resolved[4096];

// Runs the host implementation of the import at c->eip. Returns 0 if there is none, 2 if the thread blocked (the import is
// restarted later), 1 otherwise.
static int serve(CPU *c) {
    u32 idx = c->eip - THUNK_BASE;
    if (idx >= (u32)nthunks)
        return 0;
    if (!host_resolved[idx]) {
        host_fn[idx] = lookup_host(tnames[idx]);
        host_resolved[idx] = 1;
    }
    if (!host_fn[idx])
        return 0;
    if (log_imports)
        fprintf(stderr, "[import] %s ret=%08x\n", tnames[idx], rd(c->r[ESP], 4));
    nserved++;
    int r = host_fn[idx](c);
    if (r == 2)
        return 2;
    if (!r)
        c->eip = pop(c);
    return 1;
}
