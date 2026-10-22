// foundation.h: Foundation classes: NSString, NSArray, NSDictionary, NSNumber, NSDate, NSBundle, NSFileManager, NSRunLoop,
//
// NSTimer, NSScanner, ... as far as the game and the Ubisoft dylibs use them.
//
// Layouts (guest memory): strings are { isa, flags, char *cstr, len } like a CFString constant; arrays and dictionaries are
// growable pointer vectors (+4 count, +8 capacity, +12 items / keys, +16 values). Only what is actually called is implemented;
// unknown messages log a warning and return 0 (DC_MSGLOG shows every message).

// ---- strings: same layout as a CFString constant: { isa, flags, char *cstr, len } ----
static u32 nsstr(const char *s) {
    u32 o = obj_new(cls_add("NSCFString", 0), 64);
    wr(o + 4, 0x7c8, 4);
    wr(o + 8, gstr(s), 4);
    wr(o + 12, strlen(s), 4);
    return o;
}
static const char *cstr_of(u32 o) {
    return o ? GS(rd(o + 8, 4)) : "";
}
static int is_string(u32 o) {
    int k = o ? class_of_obj(o) : -1;
    return k >= 0 && is_kind(k, "NSString");
}

static const char *obj_desc(u32 o) {
    if (!o)
        return "(null)";
    if (is_string(o))
        return cstr_of(o);
    return "<object>";
}

// ---- arrays: +4 count +8 cap +12 items ----
static u32 arr_new(void) {
    u32 o = obj_of("NSMutableArray");
    wr(o + 8, 8, 4);
    wr(o + 12, hmalloc(32), 4);
    return o;
}
static void arr_add(u32 a, u32 v) {
    u32 n = rd(a + 4, 4), cap = rd(a + 8, 4), items = rd(a + 12, 4);
    if (n == cap) {
        u32 ni = hmalloc(cap * 8);
        memcpy(P(ni), P(items), cap * 4);
        wr(a + 12, ni, 4);
        wr(a + 8, cap * 2, 4);
        items = ni;
    }
    wr(items + 4 * n, v, 4);
    wr(a + 4, n + 1, 4);
}
// ---- dictionaries: +4 count +8 cap +12 keys +16 vals ----
static u32 dict_new(void) {
    u32 o = obj_of("NSMutableDictionary");
    wr(o + 8, 16, 4);
    wr(o + 12, hmalloc(64), 4);
    wr(o + 16, hmalloc(64), 4);
    return o;
}
static int key_eq(u32 a, u32 b) {
    return a == b || (is_string(a) && is_string(b) && !strcmp(cstr_of(a), cstr_of(b)));
}
static int dict_find(u32 d, u32 k) {
    for (u32 i = 0; i < rd(d + 4, 4); i++)
        if (key_eq(rd(rd(d + 12, 4) + 4 * i, 4), k))
            return i;
    return -1;
}
static void dict_set(u32 d, u32 k, u32 v) {
    int i = dict_find(d, k);
    if (i < 0) {
        u32 n = rd(d + 4, 4), cap = rd(d + 8, 4);
        if (n == cap) {
            u32 nk = hmalloc(cap * 8), nv = hmalloc(cap * 8);
            memcpy(P(nk), P(rd(d + 12, 4)), cap * 4);
            memcpy(P(nv), P(rd(d + 16, 4)), cap * 4);
            wr(d + 12, nk, 4);
            wr(d + 16, nv, 4);
            wr(d + 8, cap * 2, 4);
        }
        i = n;
        wr(d + 4, n + 1, 4);
        wr(rd(d + 12, 4) + 4 * i, k, 4);
    }
    wr(rd(d + 16, 4) + 4 * i, v, 4);
}
static u32 dict_get(u32 d, u32 k) {
    int i = dict_find(d, k);
    return i < 0 ? 0 : rd(rd(d + 16, 4) + 4 * i, 4);
}

// ---- singletons & app paths ----
static char app_exe[1024], app_bundle[1024];
static u32 singleton(const char *cls) {
    static struct {
        const char *n;
        u32 o;
    } S[32];
    static int ns;
    for (int i = 0; i < ns; i++)
        if (!strcmp(S[i].n, cls))
            return S[i].o;
    S[ns].n = cls;
    S[ns].o = obj_of(cls);
    return S[ns++].o;
}
static void set_app_path(const char *exe) {
    char tmp[1024];
    strncpy(tmp, exe, 1023);
    tmp[1023] = 0;
    snprintf(app_exe, sizeof app_exe, "%s", tmp);
    char *d = dirname(tmp);
    char t2[1024];
    strcpy(t2, d);
    d = dirname(t2);
    strcpy(tmp, d);
    d = dirname(tmp); // MacOS -> Contents -> .app
    snprintf(app_bundle, sizeof app_bundle, "%s", d);
}

// NSString
static int oh_str_with_c(CPU *c, u32 self) {
    RET(nsstr(GS(OA(0))));
    return 0;
}
static int oh_str_utf8(CPU *c, u32 self) {
    RET(rd(self + 8, 4));
    return 0;
}
static int oh_str_getc(CPU *c, u32 self) {
    u32 buf = OA(0), max = OA(1);
    strncpy(GS(buf), cstr_of(self), max);
    *P(buf + max - 1) = 0;
    RET(1);
    return 0;
}
static int oh_str_eq(CPU *c, u32 self) {
    RET(is_string(OA(0)) && !strcmp(cstr_of(self), cstr_of(OA(0))));
    return 0;
}
static int oh_str_prefix(CPU *c, u32 self) {
    RET(!strncmp(cstr_of(self), cstr_of(OA(0)), strlen(cstr_of(OA(0)))));
    return 0;
}
static int oh_str_int(CPU *c, u32 self) {
    RET(atoi(cstr_of(self)));
    return 0;
}
static int oh_str_dirname(CPU *c, u32 self) {
    char t[1024];
    snprintf(t, sizeof t, "%s", cstr_of(self));
    RET(nsstr(dirname(t)));
    return 0;
}
static int oh_str_copy(CPU *c, u32 self) {
    RET(nsstr(cstr_of(self)));
    return 0;
}
static int oh_str_getchars(CPU *c, u32 self) { // getCharacters:(unichar*) range:(NSRange)
    u32 buf = OA(0), loc = OA(1), len = OA(2);
    const char *s = cstr_of(self);
    for (u32 i = 0; i < len; i++)
        wr(buf + 2 * i, (u8)s[loc + i], 2);
    return 0;
}
// NSBundle
static int oh_bundle_main(CPU *c, u32 self) {
    u32 b = singleton("NSBundle");
    if (!rd(b + 4, 4))
        wr(b + 4, nsstr(app_bundle), 4);
    RET(b);
    return 0;
}
static int oh_bundle_with(CPU *c, u32 self) {
    u32 b = obj_of("NSBundle");
    wr(b + 4, OA(0), 4);
    RET(b);
    return 0;
}
static int oh_bundle_path(CPU *c, u32 self) {
    RET(rd(self + 4, 4));
    return 0;
}
static int oh_bundle_res(CPU *c, u32 self) {
    char t[1100];
    snprintf(t, sizeof t, "%s/Contents/Resources", cstr_of(rd(self + 4, 4)));
    RET(nsstr(t));
    return 0;
}
static int oh_bundle_exe(CPU *c, u32 self) {
    RET(nsstr(app_exe));
    return 0;
}
// NSFileManager
static int oh_fm_exists(CPU *c, u32 self) {
    struct stat st;
    RET(stat(cstr_of(OA(0)), &st) == 0);
    return 0;
}
static int oh_fm_chdir(CPU *c, u32 self) {
    RET(chdir(cstr_of(OA(0))) == 0);
    return 0;
}
// NSProcessInfo / arrays / dicts / numbers / dates
static int oh_pi_args(CPU *c, u32 self) {
    u32 a = arr_new();
    arr_add(a, nsstr(app_exe));
    RET(a);
    return 0;
}
static int oh_arr_new(CPU *c, u32 self) {
    RET(arr_new());
    return 0;
}
static int oh_arr_add(CPU *c, u32 self) {
    arr_add(self, OA(0));
    return 0;
}
static int oh_arr_count(CPU *c, u32 self) {
    RET(rd(self + 4, 4));
    return 0;
}
static int oh_arr_at(CPU *c, u32 self) {
    u32 i = OA(0);
    if (i >= rd(self + 4, 4))
        die("NSArray index %u out of range", i);
    RET(rd(rd(self + 12, 4) + 4 * i, 4));
    return 0;
}
static int oh_arr_clear(CPU *c, u32 self) {
    wr(self + 4, 0, 4);
    return 0;
}
static int oh_arr_rmrange(CPU *c, u32 self) {
    u32 loc = OA(0), len = OA(1), n = rd(self + 4, 4), it = rd(self + 12, 4);
    if (loc + len > n)
        die("removeObjectsInRange out of range");
    memmove(P(it + 4 * loc), P(it + 4 * (loc + len)), 4 * (n - loc - len));
    wr(self + 4, n - len, 4);
    return 0;
}
static int oh_dict_obj_key(CPU *c, u32 self) {
    u32 d = dict_new();
    dict_set(d, OA(1), OA(0));
    RET(d);
    return 0;
}
static int oh_dict_file(CPU *c, u32 self) {
    RET(load_plist(cstr_of(OA(0))));
    return 0;
}
static int oh_dict_get(CPU *c, u32 self) {
    RET(dict_get(self, OA(0)));
    return 0;
}
static int oh_dict_set(CPU *c, u32 self) {
    dict_set(self, OA(1), OA(0));
    return 0;
}
static int oh_num_bool(CPU *c, u32 self) {
    u32 n = obj_of("NSNumber");
    wr(n + 4, OA(0) & 0xff, 4);
    RET(n);
    return 0;
}
static int oh_num_int(CPU *c, u32 self) {
    RET(rd(self + 4, 4));
    return 0;
}
static double now_s(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}
static u32 date_new(double t) {
    u32 d = obj_of("NSDate");
    memcpy(P(d + 8), &t, 8);
    return d;
}
static double date_t(u32 d) {
    double t;
    memcpy(&t, P(d + 8), 8);
    return t;
}
static double dbl_arg(CPU *c, int n) {
    return Ad(c, oa_base + n);
}
static int oh_date_now(CPU *c, u32 self) {
    RET(date_new(now_s()));
    return 0;
}
static int oh_date_since_now(CPU *c, u32 self) {
    RET(date_new(now_s() + dbl_arg(c, 0)));
    return 0;
}
static int oh_date_diff(CPU *c, u32 self) {
    fpush(c, date_t(self) - date_t(OA(0)));
    return 0;
}
static void fire_timers(CPU *c);
static int oh_runloop_limit(CPU *c, u32 self) {
    fire_timers(c);
    RET(date_new(now_s() + 1e9));
    return 0;
}
static int oh_timer_make(CPU *c,
                         u32 self) { // timerWithTimeInterval:(double) target: selector: userInfo: repeats:
    u32 t = obj_of("NSTimer");
    wr(t + 4, OA(2), 4);
    wr(t + 8, OA(3), 4);
    wr(t + 12, OA(4), 4);
    wr(t + 24, OA(5) & 0xff, 4);
    double iv = dbl_arg(c, 0);
    memcpy(P(t + 16), &iv, 8);
    RET(t);
    return 0;
}
static int oh_sleep(CPU *c, u32 self) {
    return sleep_ns(c, (u64)(dbl_arg(c, 0) * 1e9));
} // blocks only this guest thread
// ---- Foundation extras used by the Ubisoft orbit dylib: path/array/string helpers, NSScanner, NSWorkspace ----
static u32 arr_at(u32 a, u32 i) {
    return rd(rd(a + 12, 4) + 4 * i, 4);
}
static u32 str_split(const char *s, const char *sep) { // componentsSeparatedByString:
    u32 a = arr_new();
    size_t sl = strlen(sep);
    const char *p = s;
    if (!sl) {
        arr_add(a, nsstr(s));
        return a;
    }
    for (const char *q; (q = strstr(p, sep)); p = q + sl) {
        char *t = strndup(p, q - p);
        arr_add(a, nsstr(t));
        free(t);
    }
    arr_add(a, nsstr(p));
    return a;
}
static int oh_str_length(CPU *c, u32 self) {
    RET(strlen(cstr_of(self)));
    return 0;
}
static int oh_str_format(CPU *c, u32 self) { // +stringWithFormat: (NSString *format, ...)
    gfmt(rd(OA(0) + 8, 4), c->r[ESP] + 4 + 4 * (oa_base + 1), pbuf, sizeof pbuf);
    RET(nsstr(pbuf));
    return 0;
}
static int oh_str_components(CPU *c, u32 self) {
    RET(str_split(cstr_of(self), cstr_of(OA(0))));
    return 0;
}
static int oh_str_pathcomps(CPU *c, u32 self) {
    const char *s = cstr_of(self);
    u32 a = arr_new();
    if (*s == '/') {
        arr_add(a, nsstr("/"));
        s++;
    }
    u32 parts = str_split(s, "/");
    for (u32 i = 0; i < rd(parts + 4, 4); i++)
        if (*cstr_of(arr_at(parts, i)))
            arr_add(a, arr_at(parts, i));
    RET(a);
    return 0;
}
static int oh_str_pathwith(CPU *c, u32 self) { // +pathWithComponents:
    char t[2048] = "";
    u32 a = OA(0);
    for (u32 i = 0; i < rd(a + 4, 4); i++) {
        const char *p = cstr_of(arr_at(a, i));
        if (i && t[strlen(t) - 1] != '/')
            strcat(t, "/");
        strcat(t, p);
    }
    RET(nsstr(t));
    return 0;
}
static int oh_str_trim(CPU *c, u32 self) {
    const char *s = cstr_of(self);
    while (isspace((u8)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((u8)s[n - 1]))
        n--;
    char *t = strndup(s, n);
    RET(nsstr(t));
    free(t);
    return 0;
}
static int oh_arr_copyfrom(CPU *c, u32 self) {
    u32 a = arr_new();
    for (u32 i = 0; i < rd(OA(0) + 4, 4); i++)
        arr_add(a, arr_at(OA(0), i));
    RET(a);
    return 0;
}
static int oh_arr_addall(CPU *c, u32 self) {
    for (u32 i = 0; i < rd(OA(0) + 4, 4); i++)
        arr_add(self, arr_at(OA(0), i));
    return 0;
}
static int oh_arr_rmat(CPU *c, u32 self) {
    u32 i = OA(0), n = rd(self + 4, 4), it = rd(self + 12, 4);
    if (i >= n)
        die("removeObjectAtIndex: %u out of range", i);
    memmove(P(it + 4 * i), P(it + 4 * (i + 1)), 4 * (n - i - 1));
    wr(self + 4, n - 1, 4);
    return 0;
}
static int oh_arr_rmobj(CPU *c, u32 self) {
    for (u32 i = 0; i < rd(self + 4, 4);)
        if (key_eq(arr_at(self, i), OA(0))) {
            u32 n = rd(self + 4, 4), it = rd(self + 12, 4);
            memmove(P(it + 4 * i), P(it + 4 * (i + 1)), 4 * (n - i - 1));
            wr(self + 4, n - 1, 4);
        } else
            i++;
    return 0;
}
static int oh_arr_sub(CPU *c, u32 self) {
    u32 loc = OA(0), len = OA(1);
    if (loc + len > rd(self + 4, 4))
        die("subarrayWithRange: out of range");
    u32 a = arr_new();
    for (u32 i = 0; i < len; i++)
        arr_add(a, arr_at(self, loc + i));
    RET(a);
    return 0;
}
static int oh_arr_join(CPU *c, u32 self) {
    char t[4096] = "";
    const char *sep = cstr_of(OA(0));
    for (u32 i = 0; i < rd(self + 4, 4); i++) {
        if (i)
            strcat(t, sep);
        strcat(t, cstr_of(arr_at(self, i)));
    }
    RET(nsstr(t));
    return 0;
}
// countByEnumeratingWithState:objects:count: -- one batch with everything
static int oh_arr_fastenum(CPU *c, u32 self) {
    u32 st = OA(0);
    if (rd(st, 4)) {
        RET(0);
        return 0;
    }
    wr(st, 1, 4);
    wr(st + 4, rd(self + 12, 4), 4);
    wr(st + 8, st + 20, 4);
    RET(rd(self + 4, 4));
    return 0;
}
static int oh_charset(CPU *c, u32 self) {
    RET(singleton("NSCharacterSet"));
    return 0;
}
static int oh_scanner_new(CPU *c, u32 self) {
    u32 s = obj_of("NSScanner");
    wr(s + 4, OA(0), 4);
    wr(s + 8, 0, 4);
    RET(s);
    return 0;
}
static int oh_scan_str(CPU *c, u32 self) { // scanString:intoString:
    const char *s = cstr_of(rd(self + 4, 4));
    u32 pos = rd(self + 8, 4);
    while (isspace((u8)s[pos]))
        pos++;
    const char *t = cstr_of(OA(0));
    size_t n = strlen(t);
    if (strncmp(s + pos, t, n)) {
        RET(0);
        return 0;
    }
    wr(self + 8, pos + n, 4);
    if (OA(1))
        wr(OA(1), OA(0), 4);
    RET(1);
    return 0;
}
static int oh_scan_upto(CPU *c, u32 self) { // scanUpToString:intoString:
    const char *s = cstr_of(rd(self + 4, 4));
    u32 pos = rd(self + 8, 4);
    while (isspace((u8)s[pos]))
        pos++;
    const char *q = strstr(s + pos, cstr_of(OA(0)));
    size_t n = q ? (size_t)(q - (s + pos)) : strlen(s + pos);
    if (!n) {
        RET(0);
        return 0;
    }
    char *t = strndup(s + pos, n);
    if (OA(1))
        wr(OA(1), nsstr(t), 4);
    free(t);
    wr(self + 8, pos + n, 4);
    RET(1);
    return 0;
}
static int oh_scan_end(CPU *c, u32 self) {
    const char *s = cstr_of(rd(self + 4, 4));
    u32 pos = rd(self + 8, 4);
    while (isspace((u8)s[pos]))
        pos++;
    RET(!s[pos]);
    return 0;
}
static int oh_ws_running(CPU *c, u32 self) {
    RET(arr_new());
    return 0;
} // no other applications are "running"
static int oh_bundle_id(CPU *c, u32 self) {
    RET(nsstr("com.Ubisoft.AssassinsCreed2"));
    return 0;
}
static int oh_url_path(CPU *c, u32 self) {
    RET(rd(self + 4, 4));
    return 0;
}
