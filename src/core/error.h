// error.h: fatal guest errors.
//
// die() records a message and unwinds to the innermost runs() frame (cpu/run.h) with longjmp, which reports the guest state
// (registers, backtrace, last program counters) instead of aborting the host process.

static jmp_buf jb;
static const char *why;
static char whybuf[160];
static CPU *cpu;
static u32 lastpc[16];
static int lastn;

static void die(const char *fmt, ...) __attribute__((noreturn));
#include <stdarg.h>
static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(whybuf, sizeof whybuf, fmt, ap);
    va_end(ap);
    why = whybuf;
    longjmp(jb, 1);
}

static int wlog_on_;
