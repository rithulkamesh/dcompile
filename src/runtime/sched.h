// sched.h: cooperative guest threads and the main scheduler loop.
//
// Every guest thread has its own CPU context; the host runs one at a time and switches only at top level (never inside a
// host->guest callback): when a thread blocks, exits, yields, or has used up its instruction slice. Also hosts the helper
// threads that run asynchronous work on guest time: the audio render pump and the file-I/O completion queue.

#include <time.h>
#include <unistd.h>

enum { TS_FREE, TS_RUN, TS_BLOCK, TS_SUSP, TS_DEAD };
typedef struct {
    CPU *cpu;
    int state;
    u64 wake_ns;
    u32 wait_obj; // wait_obj: what a BLOCKed thread waits for (0 = only a deadline)
    void *cgl;    // this thread's current OpenGL context
    u64 insns;    // instructions executed by this thread
    int phase, timed_out, result, reusable;
    u32 retval, saved_count;
    u32 tls[256];
} Thread;
#define MAXTHREADS 64
static Thread threads[MAXTHREADS];
static int nthreads = 1; // threads[0] is the main thread
static Thread *curth = &threads[0], *failth;
static void *host_cgl_current; // the context currently bound on the host thread
static volatile int dump_req;  // kill -USR1 <pid>: print the thread table and counters
static int schedlog;
static int sched_rr;
static int progress_secs;
static u64 next_progress_ns, nflips, nreads; // PROGRESS=n: report every n seconds
static const char *mx_info(u32 a); // threads.h: " [owner=tN count=n readers=n]" when `a` is a known lock
static void audio_zero(void),
    audio_collect(
        void); // audio.h: clear the render buffers before a callback / send them to the host afterwards
static u64 wall_limit_ns; // MAXSECS=n: stop (with a thread dump) after n wall-clock seconds
static int nserved;

static int cur_tid(void) {
    return (int)(curth - threads);
}
static u64 mono_ns(void) {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
static u32 my_id(void) {
    return (u32)(curth - threads) + 1;
}

// wake threads blocked on `obj` (max = 0: all). They re-run their import and re-check the condition.
static int wake_obj(u32 obj, int max) {
    int n = 0;
    for (int i = 0; i < nthreads && (!max || n < max); i++)
        if (threads[i].state == TS_BLOCK && threads[i].wait_obj == obj) {
            threads[i].state = TS_RUN;
            threads[i].timed_out = 0;
            threads[i].wait_obj = 0;
            n++;
        }
    return n;
}
static int block_on(CPU *c, u32 obj, u64 deadline_ns) {
    (void)c;
    curth->state = TS_BLOCK;
    curth->wait_obj = obj;
    curth->wake_ns = deadline_ns;
    return 2;
}
static int yield_retry(void) {
    return 2;
} // stay runnable, re-run the import after the others had a turn

static void sched_dump(void) {
    static const char *const N[] = {"free", "run", "block", "susp", "dead"};
    for (int i = 0; i < nthreads; i++) {
        Thread *t = &threads[i];
        if (t->state == TS_FREE)
            continue;
        u32 ip = t->cpu->eip;
        const char *at = ip >= THUNK_BASE && ip - THUNK_BASE < (u32)nthunks ? tnames[ip - THUNK_BASE] : "";
        printf("  thread %d: %-5s eip=%08x %s%s wait_obj=%08x%s insns=%llu bt:", i, N[t->state], ip, at,
               t->wake_ns ? " (timed)" : "", t->wait_obj, t->wait_obj > 0x1000 ? mx_info(t->wait_obj) : "",
               (unsigned long long)t->insns);
        {
            u32 ra = t->state == TS_DEAD ? 0 : rd(t->cpu->r[ESP], 4), bp = t->cpu->r[EBP];
            if (ra)
                printf(" %08x", ra);
            for (int k = 0; k < 9 && bp > 0x1000 && bp < 0xC0000000u; k++) {
                printf(" %08x", rd(bp + 4, 4));
                bp = rd(bp, 4);
            }
        }
        printf("\n");
    }
}

// Creates a runnable (or suspended) thread starting at `start(arg)`; returns its index or -1. Dead `reusable` threads are recycled.
static int thread_spawn_n(const CPU *proto, u32 start, int nargs, const u32 *args, int suspended,
                          int reusable) {
    int i = -1;
    for (int k = 1; k < nthreads; k++)
        if (threads[k].state == TS_DEAD && threads[k].reusable) {
            i = k;
            break;
        }
    if (i < 0 && nthreads < MAXTHREADS)
        i = nthreads++;
    if (i < 0)
        for (int k = 1; k < nthreads; k++)
            if (threads[k].state == TS_DEAD) {
                i = k;
                break;
            } // out of slots: recycle any finished thread
    if (i < 0)
        return -1;
    Thread *t = &threads[i];
    CPU *cpu = t->cpu ? t->cpu : calloc(1, sizeof(CPU));
    memset(t, 0, sizeof *t);
    memset(cpu, 0, sizeof *cpu);
    t->cpu = cpu;
    t->reusable = reusable;
    cpu->mxcsr = proto->mxcsr;
    cpu->fcw = proto->fcw;
    cpu->r[ESP] = THREAD_STACK_BASE + (u32)i * THREAD_STACK_SIZE + THREAD_STACK_SIZE -
                  64; // every thread gets a 4MB stack slot
    for (int k = nargs - 1; k >= 0; k--)
        push(cpu, args[k]);
    push(cpu, THREAD_RET);
    cpu->eip = start;
    t->state = suspended ? TS_SUSP : TS_RUN;
    nlive++;
    return i;
}

static int thread_spawn(const CPU *proto, u32 start, u32 arg, int suspended, int reusable) {
    return thread_spawn_n(proto, start, 1, &arg, suspended, reusable);
}

// Completion callbacks queued by async I/O (PBReadForkAsync); each runs on a short-lived guest thread, one at a time.
static struct {
    u32 fn, arg;
} iocb_q[256];
static int iocb_n, iocb_active;

// Audio render pump (audio.h): while running, a helper guest thread calls the game's render callback every audio_period_ns.
static int audio_running, audio_active;
static u64 audio_next_ns, audio_period_ns;
static u32 audio_cb_fn, audio_cb_args[6];

// Runs the guest until thread `t0` returns to `stop0` (R_DONE), or something fatal happens (R_BAD / unhandled R_IMPORT).
static int sched_run(Thread *t0, u32 stop0) {
    for (;;) {
        if (iocb_active && threads[iocb_active].state == TS_DEAD)
            iocb_active = 0;
        if (iocb_n && !iocb_active) {
            int i = thread_spawn(t0->cpu, iocb_q[0].fn, iocb_q[0].arg, 0, 1);
            if (i > 0) {
                iocb_active = i;
                memmove(iocb_q, iocb_q + 1, --iocb_n * sizeof iocb_q[0]);
            }
        }
        if (audio_active && threads[audio_active].state == TS_DEAD) {
            audio_active = 0;
            audio_collect();
        }
        if (audio_running && !audio_active && mono_ns() >= audio_next_ns) {
            audio_zero();
            int i = thread_spawn_n(t0->cpu, audio_cb_fn, 6, audio_cb_args, 0, 1);
            if (i > 0) {
                audio_active = i;
                audio_next_ns += audio_period_ns;
                if (audio_next_ns < mono_ns())
                    audio_next_ns = mono_ns() + audio_period_ns / 2;
            }
        }
        u64 now = mono_ns(), earliest = 0;
        Thread *pick = NULL;
        if (progress_secs && now >= next_progress_ns) {
            next_progress_ns = now + 1000000000ull * progress_secs;
            fprintf(stderr,
                    "[progress] insns=%llu imports=%d frames=%llu forkreads=%llu threads=%d main-eip=%08x\n",
                    (unsigned long long)ninsn, nserved, (unsigned long long)nflips,
                    (unsigned long long)nreads, nthreads, threads[0].cpu->eip);
        }
        if (dump_req) {
            dump_req = 0;
            if (DBG_FLAG("PEEK")) { // PEEK=hexaddr[,words][;hexaddr[,words]...]: print guest memory
                char list[512];
                snprintf(list, sizeof list, "%s", dbg_env("PEEK"));
                for (char *tok = strtok(list, ";"); tok; tok = strtok(NULL, ";")) {
                    char *e;
                    u32 a = (u32)strtoul(tok, &e, 16), n = *e == ',' ? atoi(e + 1) : 16;
                    for (u32 k = 0; k < n; k++) {
                        if (k % 8 == 0)
                            fprintf(stderr, "\n[peek %08x]", a + 4 * k);
                        fprintf(stderr, " %08x", rd(a + 4 * k, 4));
                    }
                    fprintf(stderr, "\n");
                }
            }
            fprintf(stderr, "[signal] insns=%llu imports=%d frames=%llu forkreads=%llu\n",
                    (unsigned long long)ninsn, nserved, (unsigned long long)nflips,
                    (unsigned long long)nreads);
            fflush(stdout);
            sched_dump();
            fflush(stdout);
        }
        if (wall_limit_ns && now > wall_limit_ns) {
            snprintf(whybuf, sizeof whybuf, "wall-clock limit reached");
            why = whybuf;
            failth = t0;
            return R_BAD;
        }
        for (int k = 1; k <= nthreads; k++) {
            int i = (sched_rr + k) % nthreads;
            Thread *t = &threads[i];
            if (t->state == TS_BLOCK && t->wake_ns) {
                if (now >= t->wake_ns) {
                    t->state = TS_RUN;
                    t->timed_out = 1;
                    t->wait_obj = 0;
                } else if (!earliest || t->wake_ns < earliest)
                    earliest = t->wake_ns;
            }
            if (t->state == TS_RUN && !pick) {
                pick = t;
                sched_rr = i;
            }
        }
        if (!pick) {
            if (audio_running && !audio_active && (!earliest || audio_next_ns < earliest))
                earliest = audio_next_ns > now ? audio_next_ns : now + 1;
            if (earliest) {
                u64 d = earliest - now;
                if (d > 20000000)
                    d = 20000000;
                usleep((useconds_t)(d / 1000) + 1);
                continue;
            }
            snprintf(whybuf, sizeof whybuf, "deadlock: every guest thread is blocked");
            why = whybuf;
            failth = t0;
            return R_BAD;
        }
        curth = pick;
        slice_left = SLICE;
        if (pick->cgl != host_cgl_current) {
            CGLSetCurrentContext(pick->cgl);
            host_cgl_current = pick->cgl;
        }
        u64 before = ninsn;
        int rc = runs(pick->cpu, pick == t0 ? stop0 : THREAD_RET);
        pick->insns += ninsn - before;
        if (schedlog)
            fprintf(stderr, "[sched] t%d ran %llu insns -> rc=%d state=%d eip=%08x\n", (int)(pick - threads),
                    (unsigned long long)(ninsn - before), rc, pick->state, pick->cpu->eip);
        if (rc == R_YIELD)
            continue;
        if (rc == R_DONE) {
            if (pick == t0)
                return R_DONE;
            pick->state = TS_DEAD;
            pick->retval = pick->cpu->r[EAX];
            nlive--; // start routine returned
            wake_obj(0xFD000000u + (u32)(pick - threads), 0);
            continue;
        }
        failth = pick;
        return rc;
    }
}
