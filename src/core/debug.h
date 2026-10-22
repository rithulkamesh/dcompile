// debug.h: diagnostic switches.
//
// Every switch is an environment variable with a DC_ prefix; docs/debugging.md lists them all. dbg_env("X") reads DC_X.
// DBG_FLAG("X") is the cached boolean form (set and not "0") that is cheap enough for hot paths.

static const char *dbg_env(const char *name) {
    char key[64];
    snprintf(key, sizeof key, "DC_%s", name);
    return getenv(key);
}

#define DBG_FLAG(name)                                                                                       \
    ({                                                                                                       \
        static int cached_ = -1;                                                                             \
        if (cached_ < 0) {                                                                                   \
            const char *v_ = dbg_env(name);                                                                  \
            cached_ = v_ && *v_ != '0';                                                                      \
        }                                                                                                    \
        cached_;                                                                                             \
    })
