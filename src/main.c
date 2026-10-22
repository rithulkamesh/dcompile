// main.c: dcompile entry point.
//
// Loads the i386 Mach-O executable of a game into a flat 4 GiB guest address space, sets up the process the way dyld would
// (argc/argv/envp/apple vectors), runs the static initialisers and then the entry point under the scheduler. When the guest stops
// (exit, fault, unimplemented import, debug limit) the final state is reported.
//
// usage: dcompile <path to the game's main executable>
#include <execinfo.h>
#include <signal.h>

#include "dcompile.h"

// ---------------------------------------------------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------------------------------------------------

static void on_usr1(int sig) {
    (void)sig;
    dump_req = 1; // the scheduler prints every thread's state at the next opportunity
}

static void on_exit_dump(void) {
    void *bt[24];
    int n = backtrace(bt, 24);
    fprintf(stderr, "[host] process exiting; host stack:\n");
    backtrace_symbols_fd(bt, n, 2);
}

static void print_registers(const CPU *c, const char *prefix) {
    printf("%s eip=%08x esp=%08x eax=%08x ecx=%08x edx=%08x ebx=%08x ebp=%08x esi=%08x edi=%08x\n", prefix,
           c->eip, c->r[ESP], c->r[EAX], c->r[ECX], c->r[EDX], c->r[EBX], c->r[EBP], c->r[ESI], c->r[EDI]);
}

// Reads the DC_* switches that configure the run itself (the others are read where they are used).
static void read_debug_options(void) {
    if (DBG_FLAG("EXITDUMP"))
        atexit(on_exit_dump);
    trace = DBG_FLAG("TRACE");
    break_obj = DBG_FLAG("BREAKOBJ");
    break_str = DBG_FLAG("BREAKSTR");
    if (DBG_FLAG("BREAKMAX"))
        break_max = atoi(dbg_env("BREAKMAX"));
    if (DBG_FLAG("BREAK"))
        break_pc = strtoul(dbg_env("BREAK"), 0, 16);
    if (DBG_FLAG("MAXSECS"))
        wall_limit_ns = mono_ns() + 1000000000ull * strtoull(dbg_env("MAXSECS"), 0, 10);
    if (DBG_FLAG("MAXINSN"))
        max_insn = strtoull(dbg_env("MAXINSN"), 0, 10);
    if (DBG_FLAG("PROFILE")) {
        do_prof = 1;
        prof_hist = calloc(1 << 24, 4);
    }
    log_imports = DBG_FLAG("IMPORTS");
    synclog = DBG_FLAG("SYNCLOG");
    heapprof = DBG_FLAG("HEAPPROF");
    gl_log = DBG_FLAG("GLLOG");
    schedlog = DBG_FLAG("SCHEDLOG");
    if (DBG_FLAG("PROGRESS"))
        progress_secs = atoi(dbg_env("PROGRESS"));
}

// ---------------------------------------------------------------------------------------------------------------------
// Process startup
// ---------------------------------------------------------------------------------------------------------------------

// The initial stack dyld hands to an executable's entry point: argc, argv[], 0, envp[], 0, apple[], 0.
typedef struct {
    u32 argc, argv, envp, apple, sp;
} StartupVectors;

static StartupVectors build_startup_stack(const char *program) {
    u32 sp = STACK_TOP - 256;
    u32 pstr = sp;
    memcpy(P(pstr), program, strlen(program) + 1);
    sp = (pstr - 64) & ~15u;
    StartupVectors v = {.argc = 1};
    v.apple = sp;
    wr(v.apple, pstr, 4);
    wr(v.apple + 4, 0, 4);
    v.envp = v.apple - 4;
    wr(v.envp, 0, 4);
    v.argv = v.envp - 8;
    wr(v.argv, pstr, 4);
    wr(v.argv + 4, 0, 4);
    v.sp = (v.argv - 4) & ~15u;
    wr(v.sp, v.argc, 4);
    return v;
}

// Runs the image's static initialisers (each called as `init(argc, argv, envp, apple)`), then its entry point. Returns the
// scheduler's result code (R_DONE when everything ran to completion).
static int run_main_image(CPU *c, const Image *img, const StartupVectors *sv) {
    u32 nin = img->init_size / 4, text_end = img->segs[1].vmaddr + img->segs[1].vmsize;
    int rc = R_DONE;
    for (u32 i = 0; i <= nin && rc == R_DONE; i++) {
        const char *what = i < nin ? "initializer" : "entry";
        u32 fn;
        if (i < nin) {
            fn = rd(img->init_addr + i * 4, 4);
            if (i == 0 && fn >= text_end) {
                fn = recover_first_initializer();
                printf("init[0] was clobbered; using the first static-init stub at %08x\n", fn);
            }
            if (fn < 0x1000 || fn >= text_end) {
                printf("init[%u]=%08x outside __TEXT, skipped\n", i, fn);
                continue;
            }
            c->r[ESP] = sv->sp - 32;
            push(c, sv->apple);
            push(c, sv->envp);
            push(c, sv->argv);
            push(c, sv->argc);
            push(c, RET_MAGIC);
        } else {
            fn = img->entry;
            c->r[ESP] = sv->sp; // start() expects [esp] = argc and no return address
        }
        c->eip = fn;
        rc = sched_run(&threads[0], RET_MAGIC);
        if (DBG_FLAG("EXITDUMP"))
            fprintf(stderr, "[main] sched_run returned rc=%d (i=%u)\n", rc, i);
        if (rc != R_DONE)
            printf("while running %s %u at %08x:\n", what, i, fn);
    }
    return rc;
}

// ---------------------------------------------------------------------------------------------------------------------
// Final report
// ---------------------------------------------------------------------------------------------------------------------

static void print_profile(void) {
    for (int k = 0; k < 14; k++) {
        u32 best = 0;
        for (u32 i = 1; i < (1u << 24); i++)
            if (prof_hist[i] > prof_hist[best])
                best = i;
        printf("  prof: %08x  %u insns\n", best << 8, prof_hist[best]);
        prof_hist[best] = 0;
    }
}

static void print_final_report(CPU *main_cpu, int rc) {
    if (heapprof)
        prof_dump();
    if (do_prof)
        print_profile();
    printf("%llu instructions executed, %d imports served, %llu frames flipped, %llu fork reads, %llu GL "
           "calls\n",
           (unsigned long long)ninsn, nserved, (unsigned long long)nflips, (unsigned long long)nreads,
           (unsigned long long)gl_ncalls);
    CPU *fc = failth ? failth->cpu : main_cpu;
    if (failth)
        printf("  (in guest thread %d of %d)\n", (int)(failth - threads), nthreads);
    if (rc == R_IMPORT) {
        u32 idx = fc->eip - THUNK_BASE, ret = rd(fc->r[ESP], 4);
        printf("UNIMPLEMENTED IMPORT: %s, return=%08x\n  args:", idx < (u32)nthunks ? tnames[idx] : "?", ret);
        for (int k = 1; k <= 6; k++)
            printf(" %08x", rd(fc->r[ESP] + 4 * k, 4));
        printf("\n");
        print_registers(fc, " ");
    } else if (rc == R_BAD) {
        printf("STOPPED: %s\n", why);
        print_registers(fc, " ");
        printf("  backtrace:");
        u32 bp = fc->r[EBP];
        for (int k = 0; k < 14 && bp > 0x1000; k++) {
            printf(" %08x", rd(bp + 4, 4));
            bp = rd(bp, 4);
        }
        printf("\n  last pcs:");
        for (int k = 16; k >= 1; k--)
            printf(" %08x", lastpc[(lastn - k) & 15]);
        printf("\n");
    } else {
        printf("guest returned\n");
    }
    if (nthreads > 1) {
        printf("threads:\n");
        sched_dump();
    }
    if (DBG_FLAG("MISSING")) {
        printf("imports without a host implementation:");
        for (int i = 0; i < nthunks; i++)
            if (!lookup_host(tnames[i]))
                printf(" %s", tnames[i]);
        printf("\n");
    }
}

// ---------------------------------------------------------------------------------------------------------------------

int main(int argc, char **argv) {
    if (argc < 2 && !dbg_env("JITSELFTEST")) { // the JIT self-test needs no game
        fprintf(stderr, "usage: %s <path to the game's main executable>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGUSR1, on_usr1);
    read_debug_options();
    jit_setup();

    G = mmap(NULL, (1ull << 32) + 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1,
             0);
    if (G == MAP_FAILED) {
        perror("mmap 4GB guest space");
        return 1;
    }
    jit_selftest_main();

    Image *img = load_image(argv[1], 0, 1);
    if (!img) {
        fprintf(stderr, "%s: not an i386 Mach-O executable\n", argv[1]);
        return 1;
    }
    set_app_path(argv[1]);
    objc_init();
    apply_binary_patches();

    StartupVectors sv = build_startup_stack(argv[1]);
    g_argc = sv.argc; // handed to dlopen()ed images' initialisers like dyld does
    g_argv = sv.argv;
    g_envp = sv.envp;
    g_apple = sv.apple;

    CPU c = {0};
    c.mxcsr = 0x1f80; // round-to-nearest, all exceptions masked
    c.fcw = 0x37f;
    threads[0].cpu = &c;
    threads[0].state = TS_RUN;

    int rc = run_main_image(&c, img, &sv);
    print_final_report(&c, rc);
    return 0;
}
