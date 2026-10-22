// jit_run.h: the JIT-backed execution loop (replaces run() when the JIT is enabled).
//

static void jit_prof_start(void);
static int jit_run(CPU *c, u32 stop) {
    static int inited;
    if (!inited) {
        inited = 1;
        jit_init();
        jit_install_segv();
        if (DBG_FLAG("JITPROF"))
            jit_prof_start();
    }
    for (;;) {
        if (c->eip == stop)
            return R_DONE;
        if (c->eip >= THUNK_BASE)
            return R_IMPORT;
        int sliced = nlive > 1 && !nest;
        if (sliced && slice_left <= 0)
            return R_YIELD;
        if (ninsn > max_insn)
            die("instruction limit (%llu) reached", (unsigned long long)max_insn);
        if (jcheck_pending && ninsn >= jcheck_at) {
            jcheck_pending = 0;
            jcheck = 1;
            jnochain = jnoibtc = jcheck_nochain;
            jit_flush();
            fprintf(stderr, "[jitcheck] checking from %llu instructions\n", (unsigned long long)ninsn);
        }
        if (smc_flag)
            jit_flush();
        Block *b = blk_find(c->eip);
        if (!b)
            b = jit_translate(c->eip);
        if (jcheck) {
            long d = jit_check_block(c, b);
            ninsn += d;
            if (sliced)
                slice_left -= d;
            continue;
        }
        i64 budget = sliced ? slice_left : (1 << 24);
        i64 cap = (i64)(max_insn - ninsn) + 1;
        if (budget > cap)
            budget = cap;
        smc_hit = 0;
        c->jb = budget;
        ((void (*)(CPU *, void *))t_enter)(c, b->code);
        i64 done = budget - (smc_hit ? smc_jb : c->jb);
        smc_hit = 0;
        ninsn += done;
        if (sliced)
            slice_left -= done;
        st_runs++;
        if (c->eip < THUNK_BASE && c->eip != stop && !blk_find(c->eip))
            st_ibtc_miss++;
    }
}
