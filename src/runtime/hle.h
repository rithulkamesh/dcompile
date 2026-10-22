// hle.h: the high-level-emulation (HLE) ABI shared by every host import.
//
// A guest call to an imported symbol lands on a thunk address (>= THUNK_BASE). serve() (imports.h) runs the matching host
// handler: `H(name)` defines `h_name(CPU *c)`. Handlers use the cdecl convention of the i386 guest:
//   A(n)      n-th 32-bit argument on the guest stack          RET(v)  result in eax (RET64 also sets edx)
//   fpush()   floating-point results go to st(0)               Ad/Af   double / float arguments
//   GS(a)     guest C string -> host pointer (guest memory is one flat mapping, so guest pointers are host pointers + G)
// A handler returns 0 normally (serve() then pops the return address), 1 if it changed c->eip itself, and BLOCKED (2) if the
// calling thread must wait: the import is restarted when the thread is next woken (see sched.h).
//
// call_guest() is the reverse direction: host code calling a guest function (callbacks, ObjC method imps, initialisers).

#define A(n) rd(c->r[ESP] + 4 + 4 * (n), 4)
#define RET(v) (c->r[EAX] = (u32)(v))
#define H(n) static int h_##n(CPU *c)
#define GS(a) ((char *)P(a)) // guest C string -> host pointer

static int log_imports;

typedef int (*HostFn)(CPU *);
typedef struct {
    const char *name; // linker symbol, e.g. "_malloc"; a trailing '*' matches any symbol with that prefix
    HostFn fn;
} ImportEntry;
#define IMPORT(n) {"_" #n, h_##n} // table entry for the handler h_<n> of symbol _<n>

static void fpush(CPU *c, double v) {
    c->ftop = (c->ftop - 1) & 7;
    c->st[c->ftop] = v;
}
static double Ad(CPU *c, int n) {
    double d;
    u64 v = (u64)rd(c->r[ESP] + 4 + 4 * n, 4) | ((u64)rd(c->r[ESP] + 8 + 4 * n, 4) << 32);
    memcpy(&d, &v, 8);
    return d;
}
static float Af(CPU *c, int n) {
    u32 v = A(n);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
// ---------------- call back into guest code ----------------
static u32 call_guest(CPU *c, u32 fn, int n, const u32 *args) {
    u32 sp = c->r[ESP], eip = c->eip;
    for (int i = n - 1; i >= 0; i--)
        push(c, args[i]);
    push(c, RET_MAGIC);
    c->eip = fn;
    nest++;
    int rc = runs(c, RET_MAGIC);
    nest--;
    if (rc == R_YIELD) {
        u32 idx = c->eip - THUNK_BASE;
        die("guest callback %08x blocked in %s (threads cannot switch inside a host callback)", fn,
            idx < (u32)nthunks ? tnames[idx] : "?");
    }
    if (rc == R_BAD) {
        snprintf(whybuf, sizeof whybuf, "%s", why);
        longjmp(jb, 1);
    } // keep the inner message (and pcs)
    if (rc == R_IMPORT) {
        u32 idx = c->eip - THUNK_BASE;
        die("callback %08x hit unimplemented import %s", fn, idx < (u32)nthunks ? tnames[idx] : "?");
    }
    u32 ret = c->r[EAX];
    c->r[ESP] = sp;
    c->eip = eip;
    return ret;
}
