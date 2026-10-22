// prof.h: sampling profiler (DC_JITPROF).
//
// A SIGPROF sampler attributing host PCs to guest blocks (or to host C code).

// ---- JITPROF=1: SIGPROF sampler attributing host PCs to guest blocks (or to host C code) ----
#include <sys/time.h>
#include <dlfcn.h>
static u32 *prof_cnt;
static u64 prof_host, prof_total;
static void *hostpc[64];
static u32 hostpcn[64];
static int nhostpc;
static void jit_prof_sig(int sig, siginfo_t *si, void *ucv) {
    (void)sig;
    (void)si;
    ucontext_t *uc = ucv;
    u64 pc = uc->uc_mcontext->__ss.__pc;
    prof_total++;
    if (pc >= (u64)cache_base && pc < (u64)cache_top) {
        u32 lo = 0, hi = nblocks; // blocks are allocated in increasing code address order
        while (lo + 1 < hi) {
            u32 mid = (lo + hi) / 2;
            if ((u64)bpool[mid].code <= pc)
                lo = mid;
            else
                hi = mid;
        }
        if (nblocks)
            prof_cnt[lo]++;
    } else {
        prof_host++;
        int i;
        for (i = 0; i < nhostpc; i++)
            if (hostpc[i] == (void *)pc)
                break;
        if (i == nhostpc && nhostpc < 64)
            hostpc[nhostpc++] = (void *)pc;
        if (i < nhostpc)
            hostpcn[i]++;
    }
}
static void jit_prof_report(void) {
    fprintf(stderr, "[jitprof] %llu samples, %llu in host code (%.1f%%)\n", (unsigned long long)prof_total,
            (unsigned long long)prof_host, prof_total ? 100.0 * prof_host / prof_total : 0.0);
    for (int k = 0; k < 8; k++) { // hottest host PCs, resolved to the nearest symbol
        int bi = 0;
        for (int i = 1; i < nhostpc; i++)
            if (hostpcn[i] > hostpcn[bi])
                bi = i;
        if (!nhostpc || !hostpcn[bi])
            break;
        Dl_info di;
        const char *nm = dladdr(hostpc[bi], &di) && di.dli_sname ? di.dli_sname : "?";
        fprintf(stderr, "  host pc %p (%s+%ld): %u samples\n", hostpc[bi], nm,
                di.dli_saddr ? (long)((char *)hostpc[bi] - (char *)di.dli_saddr) : 0, hostpcn[bi]);
        hostpcn[bi] = 0;
    }
    for (int k = 0; k < 25; k++) {
        u32 bi = 0;
        for (u32 i = 1; i < nblocks; i++)
            if (prof_cnt[i] > prof_cnt[bi])
                bi = i;
        if (!nblocks || !prof_cnt[bi])
            break;
        fprintf(stderr, "  guest block %08x..%08x (%d insns): %u samples\n", bpool[bi].guest, bpool[bi].gend,
                bpool[bi].nins, prof_cnt[bi]);
        prof_cnt[bi] = 0;
    }
}
static void jit_prof_start(void) {
    prof_cnt = calloc(MAXBLK, 4);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = jit_prof_sig;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = {{0, 1000}, {0, 1000}};
    setitimer(ITIMER_PROF, &it, NULL);
    atexit(jit_prof_report);
}
static void jit_setup(void) {
    jstat = dbg_env("JITSTAT") != NULL;
    jcheck = dbg_env("JITCHECK") != NULL;
    jnochain = dbg_env("JITNOCHAIN") != NULL;
    jnoibtc = dbg_env("JITNOIBTC") != NULL;
    jdump = dbg_env("JITDUMP") ? (u32)strtoul(dbg_env("JITDUMP"), 0, 16) : 0;
    if (jcheck) { // JITCHECKAT=n: run unchecked until n guest instructions, then flush and start checking
        jwin = atoi(dbg_env("JITCHECK")) >= 2 ? (dbg_env("JITWIN") ? atol(dbg_env("JITWIN")) : 4000) : 0;
        jcheck_nochain = !jwin;
        jnochain = jnoibtc = jcheck_nochain;
        if (DBG_FLAG("JITCHECKAT")) {
            jcheck_at = strtoull(dbg_env("JITCHECKAT"), 0, 10);
            jcheck_pending = 1;
            jcheck = 0;
            jnochain = jnoibtc = 0;
        }
    }
    jit_on = !DBG_FLAG("NOJIT") && !trace && !do_prof && !break_pc;
    if (jit_on && (jstat || jcheck || jcheck_pending))
        atexit(jit_report);
    if (jcheck || jcheck_pending)
        atexit(jit_check_summary);
}
