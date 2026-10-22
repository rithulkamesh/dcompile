// run.h: the execution loop.
//
// run() executes guest code (interpreting, or through the JIT) until it reaches a stop address, an import thunk, a scheduling
// slice boundary or an error. runs() wraps it: it serves host imports and is re-entrant, so host code can call back into the
// guest (call_guest in runtime/hle.h).

static int run(CPU *c, u32 stop) {
    cpu = c;
    if (jit_on)
        return jit_run(c, stop);
    for (;;) {
        if (c->eip == stop)
            return R_DONE;
        if (c->eip >= THUNK_BASE)
            return R_IMPORT;
        if (nlive > 1 && !nest && --slice_left <= 0)
            return R_YIELD;
        if (do_prof && c->eip < DYLIB_BASE)
            prof_hist[c->eip >> 8]++;
        lastpc[lastn++ & 15] = c->eip;
        if (++ninsn > max_insn)
            die("instruction limit (%llu) reached", (unsigned long long)max_insn);
        if (c->eip == break_pc && break_n++ < break_max)
            fprintf(stderr, "[break] t%d %08x eax=%08x esp=%08x ret=%08x args=%08x %08x %08x insn=%llu\n",
                    cur_tid(), c->eip, c->r[EAX], c->r[ESP], rd(c->r[ESP], 4), rd(c->r[ESP] + 4, 4),
                    rd(c->r[ESP] + 8, 4), rd(c->r[ESP] + 12, 4), (unsigned long long)ninsn);
        if (c->eip == break_pc && break_obj && break_n <= break_max) {
            u32 o = rd(c->r[ESP], 4);
            fprintf(stderr, "   obj %08x:", o);
            for (int k = 0; k < 40; k++)
                fprintf(stderr, " %08x", rd(o + 4 * k, 4));
            {
                u32 fp = rd(o + 0x78, 4);
                fprintf(stderr, "\n   functor@%08x:", fp);
                if (fp > 0x1000)
                    for (int k = 0; k < 10; k++)
                        fprintf(stderr, " %08x", rd(fp + 4 * k, 4));
            }
            fprintf(stderr, "\n   strs:");
            for (int k = 0; k < 40; k++) {
                u32 p = rd(o + 4 * k, 4);
                if (p > 0x1000 && p < 0x03000000) {
                    char *s = (char *)P(p);
                    int ok = 1, n = 0;
                    for (; n < 30 && s[n]; n++)
                        if (s[n] < 32 || s[n] > 126)
                            ok = 0;
                    if (ok && n >= 3)
                        fprintf(stderr, " [+%x]%.30s", 4 * k, s);
                }
            }
            fprintf(stderr, "\n");
        }
        if (c->eip == break_pc && break_str && break_n <= break_max && c->r[EAX] > 0x1000)
            fprintf(stderr, "   eax string: %.60s\n", (char *)P(c->r[EAX]));
        if (trace)
            fprintf(stderr,
                    "%08x: %02x %02x %02x %02x  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x\n",
                    c->eip, *P(c->eip), *P(c->eip + 1), *P(c->eip + 2), *P(c->eip + 3), c->r[0], c->r[1],
                    c->r[2], c->r[3], c->r[4], c->r[5]);
        exec(c);
    }
}

// run guest code until it returns to `stop`, serving imports on the way. Re-entrant (host -> guest callbacks).
static int runs(CPU *c, u32 stop) {
    jmp_buf saved;
    memcpy(saved, jb, sizeof jb);
    volatile int rc;
    if (setjmp(jb))
        rc = R_BAD;
    else
        for (;;) {
            rc = run(c, stop);
            if (rc != R_IMPORT)
                break;
            int s = serve(c);
            if (s == 0)
                break;
            if (s == 2) {
                rc = R_YIELD;
                break;
            }
            if (yield_req) {
                yield_req = 0;
                if (!nest) {
                    rc = R_YIELD;
                    break;
                }
            }
        }
    memcpy(jb, saved, sizeof jb);
    return rc;
}
