// image.h: loaded Mach-O images and their symbol tables.
//
// An Image records where a Mach-O was mapped (slide, address range, segments), its defined symbols (for dlsym and for
// self/weak binds), its static initialisers and entry point. thunk_for() turns an imported symbol into either guest storage
// (imported variables) or a host thunk address.

static int nbound, nself;

static u32 thunk_for(const char *name) {
    u32 d = data_sym(name);
    if (d)
        return d;
    for (int i = 0; i < nthunks; i++)
        if (!strcmp(tnames[i], name))
            return THUNK_BASE + i;
    if (nthunks == 4096) {
        fprintf(stderr, "thunk table full\n");
        exit(1);
    }
    tnames[nthunks] = strdup(name);
    return THUNK_BASE + nthunks++;
}

static u64 uleb(const u8 *p, size_t *i) {
    u64 v = 0;
    int s = 0;
    u8 b;
    do {
        b = p[(*i)++];
        v |= (u64)(b & 0x7f) << s;
        s += 7;
    } while (b & 0x80);
    return v;
}
static i64 sleb(const u8 *p, size_t *i) {
    i64 v = 0;
    int s = 0;
    u8 b;
    do {
        b = p[(*i)++];
        v |= (i64)(b & 0x7f) << s;
        s += 7;
    } while (b & 0x80);
    if (b & 0x40)
        v |= -((i64)1 << s);
    return v;
}

// Symbols an image defines (from LC_SYMTAB): used for self/weak/flat binds and for dlsym.
typedef struct {
    u32 *idx;
    u32 mask;
    struct {
        const char *name;
        u32 addr;
    } *e;
    u32 n;
} SymTab;
static u32 hash_str(const char *s) {
    u32 h = 5381;
    while (*s)
        h = h * 33 + (u8)*s++;
    return h;
}
static void st_init(SymTab *t, u32 count) {
    u32 cap = 16;
    while (cap < count * 2)
        cap <<= 1;
    t->idx = calloc(cap, 4);
    t->mask = cap - 1;
    t->e = calloc(count + 1, sizeof *t->e);
    t->n = 0;
}
static void st_add(SymTab *t, const char *name, u32 addr) {
    t->e[t->n].name = name;
    t->e[t->n].addr = addr;
    u32 h = hash_str(name) & t->mask;
    while (t->idx[h])
        h = (h + 1) & t->mask;
    t->idx[h] = ++t->n;
}
static u32 st_lookup(const SymTab *t, const char *name) {
    for (u32 h = hash_str(name) & t->mask; t->idx[h]; h = (h + 1) & t->mask)
        if (!strcmp(t->e[t->idx[h] - 1].name, name))
            return t->e[t->idx[h] - 1].addr;
    return 0;
}

typedef struct {
    char path[1024];
    u32 slide, lo, hi;
    struct {
        u32 vmaddr, vmsize;
    } segs[16];
    int nsegs;
    SymTab syms;
    u32 init_addr, init_size, entry;
    int is_main;
} Image;
static Image images[8];
static int nimages;
static Image *cur; // image currently being rebased/bound
