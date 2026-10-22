// orbit.h: an offline stand-in for Ubisoft's Orbit client library (ubiorbitapi_r2.dylib).
//
// The real library talks to the Ubisoft Game Launcher over a pipe. dlopen() of the dylib returns FAKE_ORBIT instead and dlsym()
// hands out the host functions below, which answer every request locally: the launcher is "already running", the user is
// logged in as an offline account, and savegames are stored as files under ~/Library/Application Support/Ubisoft/.
//
// Requests are answered from MgOrbitdllUpdate() (the game registers listeners after the request call returns). The game keeps
// a connection object it checks every frame; the binary patches in loader/patches.h keep it from concluding the connection is lost.

#define FAKE_ORBIT (IMAGE_HANDLE_BASE + 0xF00u) // dlopen handle of the stand-in library
static u32 orbit_session(void) {
    static u32 s;
    if (!s) {
        s = hmalloc(64);
        memset(P(s), 0, 64);
    }
    return s;
}
// Requests are answered from MgOrbitdllUpdate(), like the real library: callers register their listener *after* the request call
// returns, so a synchronous callback would find nobody waiting.
static struct {
    int kind;
    u32 listener, id, x, y;
} orbit_q[64];
static int orbit_qn;
static void orbit_post2(int kind, u32 l, u32 id, u32 x, u32 y) {
    if (l && orbit_qn < 64) {
        orbit_q[orbit_qn].kind = kind;
        orbit_q[orbit_qn].listener = l;
        orbit_q[orbit_qn].id = id;
        orbit_q[orbit_qn].x = x;
        orbit_q[orbit_qn++].y = y;
    }
}
static void orbit_post(int kind, u32 l, u32 id) {
    orbit_post2(kind, l, id, 0, 0);
}
H(MgOrbitdllGetSession) {
    RET(orbit_session());
    return 0;
}
// (session, unsigned, unsigned, const char *, const char *) -> bool. true means "the launcher was spawned, quit so it can restart us";
// false means "we are already running under the launcher", which is what we pretend.
H(MgOrbitdllStartLauncher) {
    RET(0);
    return 0;
}

// ---- savegames: stored as <productId>_<savegameId>.sav (+ .nam with the UTF-8 name) under Application Support ----
static const char *save_dir(void) {
    static char d[1100];
    if (d[0])
        return d;
    const char *h = getenv("HOME");
    snprintf(d, sizeof d, "%s/Library/Application Support/Ubisoft/Assassin's Creed 2/saves", h ? h : "/tmp");
    char t[1100];
    snprintf(t, sizeof t, "%s", d);
    for (char *p = t + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(t, 0755);
            *p = '/';
        }
    mkdir(t, 0755);
    return d;
}
static u32 save_hash(const char *n) {
    u32 h = 2166136261u;
    for (; *n; n++)
        h = (h ^ (u8)*n) * 16777619u;
    return (h & 0x7fffffff) | 1;
} // savegame id derived from the name: the game finds saves by name
static void save_path(char *out, size_t n, u32 p, u32 sid, const char *ext) {
    snprintf(out, n, "%s/%u_%u.%s", save_dir(), p, sid, ext);
}
static void wide_to_utf8(u32 w, char *o, size_t n) {
    size_t k = 0;
    for (u32 i = 0; w && rd(w + 4 * i, 4) && k + 1 < n; i++) {
        u32 ch = rd(w + 4 * i, 4);
        o[k++] = ch < 128 ? (char)ch : '?';
    }
    o[k] = 0;
}
static u32 utf8_to_wide(const char *s) {
    u32 n = strlen(s), w = hmalloc(4 * (n + 1));
    for (u32 i = 0; i <= n; i++)
        wr(w + 4 * i, (u8)s[i], 4);
    return w;
}
typedef struct {
    u32 obj, prod, sid;
    char name[256];
    u8 *buf;
    size_t len, cap;
} SaveW;
typedef struct {
    u32 obj;
    u8 *buf;
    size_t len;
} SaveR;
static SaveW sw[16];
static int nsw;
static SaveR sr[16];
static int nsr;
static SaveW *sw_of(u32 o) {
    for (int i = 0; i < nsw; i++)
        if (sw[i].obj == o)
            return &sw[i];
    return 0;
}
static SaveR *sr_of(u32 o) {
    for (int i = 0; i < nsr; i++)
        if (sr[i].obj == o)
            return &sr[i];
    return 0;
}
H(MgOrbitdllGetSavegameWriter) { // (session, reqId, IGetSavegameWriterListener *, productId, savegameId, overwrite)
    int i = nsw < 16 ? nsw++ : 0;
    free(sw[i].buf);
    memset(&sw[i], 0, sizeof sw[i]);
    sw[i].obj = hmalloc(16);
    sw[i].prod = A(3);
    sw[i].sid = A(4);
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] GetSavegameWriter(req %u prod %u sid %u)\n", A(1), A(3), A(4));
    orbit_post2(3, A(2), A(1), 0, sw[i].obj);
    RET(0);
    return 0;
}
H(MgOrbitdllSaveGameWriterSetName) {
    SaveW *w = sw_of(A(0));
    if (w)
        wide_to_utf8(A(1), w->name, sizeof w->name);
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] SetName '%s'\n", w ? w->name : "?");
    RET(1);
    return 0;
}
H(MgOrbitdllSaveGameWriterWrite) { // (writer, reqId, ISavegameWriteListener *, void *data, size)
    SaveW *w = sw_of(A(0));
    u32 n = A(4);
    if (w && n) {
        if (w->len + n > w->cap) {
            w->cap = (w->len + n) * 2;
            w->buf = realloc(w->buf, w->cap);
        }
        memcpy(w->buf + w->len, P(A(3)), n);
        w->len += n;
    }
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] Write req %u %u bytes\n", A(1), n);
    orbit_post2(4, A(2), A(1), w ? n : 0, 0);
    RET(0);
    return 0;
}
H(MgOrbitdllSaveGameWriterClose) {
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] WriterClose\n");
    SaveW *w = sw_of(A(0));
    if (w) {
        char p[1200];
        if (w->name[0])
            w->sid = save_hash(w->name);
        save_path(p, sizeof p, w->prod, w->sid, "sav");
        FILE *f = fopen(p, "wb");
        if (f) {
            fwrite(w->buf, 1, w->len, f);
            fclose(f);
        }
        save_path(p, sizeof p, w->prod, w->sid, "nam");
        f = fopen(p, "wb");
        if (f) {
            fputs(w->name, f);
            fclose(f);
        }
        if (DBG_FLAG("ORBITLOG"))
            fprintf(stderr, "[orbit] saved %u_%u '%s' %zu bytes\n", w->prod, w->sid, w->name, w->len);
        free(w->buf);
        w->buf = 0;
        w->len = 0;
    }
    RET(0);
    return 0;
}
H(MgOrbitdllGetSavegameReader) { // (session, reqId, IGetSavegameReaderListener *, productId, savegameId)
    char p[1200];
    save_path(p, sizeof p, A(3), A(4), "sav");
    FILE *f = fopen(p, "rb");
    u32 st = 1, o = 0;
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        int i = nsr < 16 ? nsr++ : 0;
        free(sr[i].buf);
        sr[i].buf = malloc(n ? n : 1);
        sr[i].len = fread(sr[i].buf, 1, n, f);
        fclose(f);
        sr[i].obj = o = hmalloc(16);
        st = 0;
    }
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] GetSavegameReader(prod %u sid %u) -> %s\n", A(3), A(4),
                st ? "missing" : "ok");
    orbit_post2(5, A(2), A(1), st, o);
    RET(0);
    return 0;
}
H(MgOrbitdllSaveGameReaderRead) { // (reader, reqId, ISavegameReadListener *, offset, void *buf, size)
    SaveR *r = sr_of(A(0));
    u32 off = A(3), n = A(5), got = 0;
    if (r && off < r->len) {
        got = r->len - off < n ? (u32)(r->len - off) : n;
        memcpy(P(A(4)), r->buf + off, got);
    }
    orbit_post2(6, A(2), A(1), got, 0);
    RET(0);
    return 0;
}
H(MgOrbitdllSaveGameReaderClose) {
    RET(0);
    return 0;
}
H(MgOrbitdllRemoveSavegame) { // (session, reqId, IRemoveSavegameListener *, productId, savegameId)
    char p[1200];
    save_path(p, sizeof p, A(3), A(4), "sav");
    unlink(p);
    save_path(p, sizeof p, A(3), A(4), "nam");
    unlink(p);
    orbit_post2(7, A(2), A(1), 0, 0);
    RET(0);
    return 0;
}
// SavegameInfo: the game hands us the address of the array slot (OrbitClient forwards &infos[i]), so the slot holds a pointer to { wchar *name, productId, savegameId, size }
H(MgOrbitdllSaveGameInfoGetName) {
    RET(rd(rd(A(0), 4), 4));
    return 0;
}
H(MgOrbitdllSaveGameInfoGetProductId) {
    RET(rd(rd(A(0), 4) + 4, 4));
    return 0;
}
H(MgOrbitdllSaveGameInfoGetSavegameId) {
    RET(rd(rd(A(0), 4) + 8, 4));
    return 0;
}
H(MgOrbitdllSaveGameInfoGetSize) {
    RET(rd(rd(A(0), 4) + 12, 4));
    return 0;
}
static void orbit_deliver_list(CPU *c, u32 fn, u32 l, u32 id) {
    u32 arr = hmalloc(4 * 64), n = 0;
    DIR *d = opendir(save_dir());
    for (struct dirent *e; d && (e = readdir(d)) && n < 64;) {
        u32 p, s;
        char ext[8];
        if (sscanf(e->d_name, "%u_%u.%3s", &p, &s, ext) != 3 || strcmp(ext, "sav"))
            continue;
        char path[1200], nm[256] = "savegame";
        struct stat st;
        save_path(path, sizeof path, p, s, "sav");
        if (stat(path, &st))
            continue;
        save_path(path, sizeof path, p, s, "nam");
        FILE *f = fopen(path, "rb");
        if (f) {
            size_t k = fread(nm, 1, sizeof nm - 1, f);
            nm[k] = 0;
            fclose(f);
        }
        if (DBG_FLAG("ORBITLOG"))
            fprintf(stderr, "[orbit] list entry prod %u sid %u '%s' %lld bytes\n", p, s, nm,
                    (long long)st.st_size);
        u32 info = hmalloc(16);
        wr(info, utf8_to_wide(nm), 4);
        wr(info + 4, p, 4);
        wr(info + 8, s, 4);
        wr(info + 12, (u32)st.st_size, 4);
        wr(arr + 4 * n++, info, 4);
    }
    if (d)
        closedir(d);
    u32 args[4] = {l, id, arr, n};
    call_guest(c, fn, 4, args);
}
H(MgOrbitdllUpdate) {
    while (orbit_qn) {
        int kind = orbit_q[0].kind;
        u32 l = orbit_q[0].listener, id = orbit_q[0].id, orbit_q_x = orbit_q[0].x, orbit_q_y = orbit_q[0].y;
        memmove(orbit_q, orbit_q + 1, --orbit_qn * sizeof orbit_q[0]);
        u32 fn = rd(rd(l, 4), 4);
        if (kind == 0) {
            u32 args[3] = {l, id, 1}; // any non-zero server id: the game only checks that one was returned
            call_guest(c, fn, 3, args);
        } // OnGetOrbitServer(id, serverId)
        else if (kind == 1) { // OnGetLoginDetails(id, user, wide name, token)
            u32 s1 = gstr("dcompile"), s3 = gstr("offline"), s2 = hmalloc(4 * 9);
            for (int i = 0; i <= 8; i++)
                wr(s2 + 4 * i, "dcompile"[i], 4);
            u32 args[5] = {l, id, s1, s2, s3};
            call_guest(c, fn, 5, args);
        } else if (kind == 2)
            orbit_deliver_list(c, fn, l, id); // OnGetSavegameList(id, infos, count)
        else if (kind == 3 || kind == 5) {
            u32 args[4] = {l, id, kind == 3 ? 0 : orbit_q_x, orbit_q_y};
            call_guest(c, fn, 4, args);
        } // OnGet(Savegame Writer|Reader)Complete(id, status, object)
        else if (kind == 4) {
            u32 args[3] = {l, id, orbit_q_x};
            call_guest(c, fn, 3, args);
        } // OnSavegameWriteComplete(id, bytesWritten)
        else if (kind == 7) {
            u32 args[3] = {l, id, 0};
            call_guest(c, fn, 3, args);
        } // OnRemoveSavegameComplete(id, status)
        else if (kind == 6) {
            u32 args[3] = {l, id, orbit_q_x};
            call_guest(c, fn, 3, args);
        } // OnReadComplete(id, bytesRead)
    }
    RET(1);
    return 0; // Update() -> bool: the launcher connection is alive
}
H(MgOrbitdllGetRequestUniqueId) {
    static u32 id = 1;
    RET(id++);
    return 0;
}
// GetLoginDetails(session, requestId, IGetLoginDetailsListener *): the real library answers asynchronously from the launcher; we
// answer on the spot by calling listener slot 0: OnLoginDetails(this, requestId, const char *a, const wchar_t *b, const char *c).
H(MgOrbitdllGetLoginDetails) {
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] GetLoginDetails(%08x %08x %08x)\n", A(0), A(1), A(2));
    orbit_post(1, A(2), A(1));
    RET(0);
    return 0;
}
H(MgOrbitdllGetOrbitServer) {
    if (DBG_FLAG("ORBITLOG")) {
        fprintf(stderr, "[orbit] t%d GetOrbitServer(%08x %08x %08x) bt:", cur_tid(), A(0), A(1), A(2));
        u32 bp = c->r[EBP];
        fprintf(stderr, " %08x", rd(c->r[ESP], 4));
        for (int k = 0; k < 8 && bp > 0x1000; k++) {
            fprintf(stderr, " %08x", rd(bp + 4, 4));
            bp = rd(bp, 4);
        }
        fprintf(stderr, "\n");
    }
    orbit_post(0, A(2), A(1));
    {
        u32 l = A(2), args[3] = {l, A(1), 1};
        call_guest(c, rd(rd(l, 4), 4), 3, args);
    } // the game resets its result slot, then calls us: answer in-line (the queued answer from Update() follows for callers that register late)
    RET(0);
    return 0;
}
H(MgOrbitdllGetSavegameList) {
    if (DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] GetSavegameList(%08x %08x %08x)\n", A(0), A(1), A(2));
    orbit_post(2, A(2), A(1));
    RET(0);
    return 0;
}
// GetLocText(session, const wchar_t *key, const char *lang) -> const wchar_t *: hand the key back so the game always has text to show
H(MgOrbitdllGetLocText) {
    RET(A(1));
    return 0;
}
H(MgOrbitdllNull) {
    if (log_imports || DBG_FLAG("ORBITLOG"))
        fprintf(stderr, "[orbit] %s(%08x %08x %08x %08x %08x)\n", tnames[c->eip - THUNK_BASE], A(0), A(1),
                A(2), A(3), A(4));
    RET(0);
    return 0;
} // everything else: nothing available offline

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry orbit_imports[] = {
    {"_MgOrbitdllGetSession", h_MgOrbitdllGetSession},
    {"_MgOrbitdllStartLauncher", h_MgOrbitdllStartLauncher},
    {"_MgOrbitdllGetLoginDetails", h_MgOrbitdllGetLoginDetails},
    {"_MgOrbitdllGetOrbitServer", h_MgOrbitdllGetOrbitServer},
    {"_MgOrbitdllGetSavegameList", h_MgOrbitdllGetSavegameList},
    {"_MgOrbitdllGetLocText", h_MgOrbitdllGetLocText},
    {"_MgOrbitdllGetSavegameWriter", h_MgOrbitdllGetSavegameWriter},
    {"_MgOrbitdllSaveGameWriterSetName", h_MgOrbitdllSaveGameWriterSetName},
    {"_MgOrbitdllSaveGameWriterWrite", h_MgOrbitdllSaveGameWriterWrite},
    {"_MgOrbitdllSaveGameWriterClose", h_MgOrbitdllSaveGameWriterClose},
    {"_MgOrbitdllGetSavegameReader", h_MgOrbitdllGetSavegameReader},
    {"_MgOrbitdllSaveGameReaderRead", h_MgOrbitdllSaveGameReaderRead},
    {"_MgOrbitdllSaveGameReaderClose", h_MgOrbitdllSaveGameReaderClose},
    {"_MgOrbitdllRemoveSavegame", h_MgOrbitdllRemoveSavegame},
    {"_MgOrbitdllSaveGameInfoGetName", h_MgOrbitdllSaveGameInfoGetName},
    {"_MgOrbitdllSaveGameInfoGetProductId", h_MgOrbitdllSaveGameInfoGetProductId},
    {"_MgOrbitdllSaveGameInfoGetSavegameId", h_MgOrbitdllSaveGameInfoGetSavegameId},
    {"_MgOrbitdllSaveGameInfoGetSize", h_MgOrbitdllSaveGameInfoGetSize},
    {"_MgOrbitdllUpdate", h_MgOrbitdllUpdate},
    {"_MgOrbitdllGetRequestUniqueId", h_MgOrbitdllGetRequestUniqueId},
    {"_MgOrbitdll*", h_MgOrbitdllNull}, // every other entry point: nothing is available offline
    {0, 0}};
// clang-format on
