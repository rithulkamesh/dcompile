// inject.h: synthetic input for unattended runs and tests.
//
//
// DC_INJECT="sec,type,chars,keycode;..."   one-shot events at the given seconds after start (type: 1 key down, 2 key up,
//                                          3 mouse down, 4 mouse up; chars "R" means Return)
// DC_INJECTSPAM="start,period"             from `start` seconds on, tap Return every `period` seconds (skips menus and movies)
// DC_INJECTHOLD="sec,keycode"              press one key at `sec` seconds and keep it held
//
// Events are returned in place of a host event from the NSApplication event pump (see appkit.h).

enum { INJ_KEY_DOWN = 1, INJ_KEY_UP = 2, KEYCODE_RETURN = 36 };

static double inject_seconds(void) {
    static u64 t0;
    if (!t0)
        t0 = mono_ns();
    return (double)(mono_ns() - t0) / 1e9;
}

static void inject_key(HGEvent *e, int type, int keycode) {
    memset(e, 0, sizeof *e);
    e->type = type;
    e->keycode = keycode;
    if (keycode == KEYCODE_RETURN)
        strcpy(e->chars, "\r");
}

// Parses one "sec,type,chars,keycode" item; returns 0 if it is not due yet or malformed.
static int inject_scheduled(HGEvent *e) {
    static int done[16];
    const char *spec = dbg_env("INJECT");
    if (!spec)
        return 0;
    char buf[256], *save, *item;
    snprintf(buf, sizeof buf, "%s", spec);
    int n = 0;
    for (item = strtok_r(buf, ";", &save); item && n < 16; item = strtok_r(NULL, ";", &save), n++) {
        double sec;
        int type, keycode = 0;
        char chars[8] = "";
        if (sscanf(item, "%lf,%d,%7[^,],%d", &sec, &type, chars, &keycode) < 2 || done[n] ||
            inject_seconds() < sec)
            continue;
        done[n] = 1;
        inject_key(e, type, keycode);
        if (chars[0] == 'R')
            strcpy(e->chars, "\r");
        else if (chars[0])
            snprintf(e->chars, sizeof e->chars, "%s", chars);
        return 1;
    }
    return 0;
}

static int inject_held_key(HGEvent *e) {
    static int sent;
    const char *spec = dbg_env("INJECTHOLD");
    double sec = 0;
    int keycode = 0;
    if (!spec || sent || sscanf(spec, "%lf,%d", &sec, &keycode) < 2 || inject_seconds() < sec)
        return 0;
    sent = 1;
    inject_key(e, INJ_KEY_DOWN, keycode);
    return 1;
}

static int inject_return_taps(HGEvent *e) {
    static double next;
    static int down;
    const char *spec = dbg_env("INJECTSPAM");
    double start = 0, period = 1;
    if (!spec)
        return 0;
    sscanf(spec, "%lf,%lf", &start, &period);
    double now = inject_seconds();
    if (now < start || now < next)
        return 0;
    next = now + period / 2;
    inject_key(e, down ? INJ_KEY_UP : INJ_KEY_DOWN, KEYCODE_RETURN);
    down = !down;
    return 1;
}

// Produces the next synthetic event, if one is due.
static int inject_next_event(HGEvent *e) {
    return inject_held_key(e) || inject_return_taps(e) || inject_scheduled(e);
}
