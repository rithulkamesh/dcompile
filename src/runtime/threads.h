// threads.h: pthread, Mach semaphore and lock imports on top of the cooperative scheduler (sched.h).
//
// Mutexes, condition variables, semaphores and read/write locks keep their state in host tables keyed by the guest address, so
// statically initialised objects need no init call. Blocking is restart-style (BLOCKED): the thread stays on the import thunk
// and the handler re-evaluates its condition when the thread is woken. DC_SYNCLOG traces semaphore/condvar traffic.
//
// Read/write locks follow macOS semantics: readers share, a writer excludes everyone, a relock by the owner answers EDEADLK
// (the game keeps its own nesting count) and any thread may release a write lock.

static int synclog; // SYNCLOG=1: trace semaphore / condition-variable traffic
#define SL(...)                                                                                              \
    do {                                                                                                     \
        if (synclog) {                                                                                       \
            fprintf(stderr, "[sync] t%d ", (int)(curth - threads));                                          \
            fprintf(stderr, __VA_ARGS__);                                                                    \
            fputc('\n', stderr);                                                                             \
        }                                                                                                    \
    } while (0)
#define BLOCKED 2 // handler result: thread blocked or yielded, import will be re-run

// ---- mutexes ----
typedef struct {
    u32 addr, owner;
    int count, rec, readers;
} Mx;
static Mx mxs[1 << 17];
static int nmx;
static int mx_hash[1 << 18]; // entries are never freed; the table is just large
static Mx *mx_get(u32 a) {
    for (u32 h = (a >> 2) & ((1 << 18) - 1);; h = (h + 1) & ((1 << 18) - 1)) {
        if (!mx_hash[h]) {
            if (nmx == (1 << 17))
                die("too many mutexes");
            mxs[nmx].addr = a;
            mx_hash[h] = ++nmx;
            return &mxs[nmx - 1];
        }
        if (mxs[mx_hash[h] - 1].addr == a)
            return &mxs[mx_hash[h] - 1];
    }
}
static int mx_lock(CPU *c, u32 a, int rec) {
    Mx *m = mx_get(a);
    u32 me = my_id();
    if (rec)
        m->rec = 1;
    if (!m->owner) {
        m->owner = me;
        m->count = 1;
        RET(0);
        return 0;
    }
    if (m->owner == me && m->rec) {
        m->count++;
        RET(0);
        return 0;
    }
    return block_on(c, a, 0); // (a non-recursive relock deadlocks, as it would for real)
}
static int mx_unlock(CPU *c, u32 a) {
    Mx *m = mx_get(a);
    if (m->owner && --m->count <= 0) {
        m->owner = 0;
        m->count = 0;
        wake_obj(a, 1);
    }
    RET(0);
    return 0;
}
H(pthread_mutex_init) {
    Mx *m = mx_get(A(0));
    m->owner = 0;
    m->count = 0;
    m->rec = A(1) && rd(A(1) + 8, 4) == 2;
    RET(0);
    return 0;
}
H(pthread_mutex_lock) {
    return mx_lock(c, A(0), 0);
}
H(pthread_mutex_trylock) {
    Mx *m = mx_get(A(0));
    u32 me = my_id();
    if (!m->owner) {
        m->owner = me;
        m->count = 1;
        RET(0);
    } else if (m->owner == me && m->rec) {
        m->count++;
        RET(0);
    } else
        RET(16);
    return 0;
}
H(pthread_mutex_unlock) {
    return mx_unlock(c, A(0));
}
H(pthread_mutexattr_init) {
    wr(A(0), 0x2DA8B3B4, 4);
    wr(A(0) + 8, 0, 4);
    RET(0);
    return 0;
}
H(pthread_mutexattr_settype) {
    wr(A(0) + 8, A(1), 4);
    RET(0);
    return 0;
}
// Real reader/writer semantics (readers share; a writer excludes everyone, recursive for its owner). Waiters re-run the import when woken.
static int rw_lock(CPU *c, u32 a, int write) {
    Mx *m = mx_get(a);
    u32 me = my_id();
    if (m->owner == me) {
        RET(11);
        return 0;
    } // macOS answers EDEADLK to the owner (the game keeps its own nesting count and unlocks once)
    if (write ? (!m->owner && !m->readers) : !m->owner) {
        if (write) {
            m->owner = me;
            m->count = 1;
        } else
            m->readers++;
        RET(0);
        return 0;
    }
    return block_on(c, a, 0);
}
H(pthread_rwlock_reset) {
    Mx *m = mx_get(A(0));
    m->owner = 0;
    m->count = 0;
    m->readers = 0;
    wake_obj(A(0), 0);
    RET(0);
    return 0;
} // init/destroy: forget state left by a previous object at this address
H(pthread_rwlock_rdlock) {
    return rw_lock(c, A(0), 0);
}
H(pthread_rwlock_wrlock) {
    return rw_lock(c, A(0), 1);
}
H(pthread_rwlock_unlock) {
    Mx *m = mx_get(A(0));
    if (m->owner) {
        if (--m->count <= 0) {
            m->owner = 0;
            m->count = 0;
            wake_obj(A(0), 0);
        }
    } // macOS rwlocks have no owner check: another thread may release a write lock
    else if (m->readers > 0 && --m->readers == 0)
        wake_obj(A(0), 0);
    RET(0);
    return 0;
}

static const char *mx_info(u32 a) {
    static char b[64];
    for (u32 h = (a >> 2) & ((1 << 18) - 1); mx_hash[h]; h = (h + 1) & ((1 << 18) - 1)) {
        Mx *m = &mxs[mx_hash[h] - 1];
        if (m->addr == a) {
            snprintf(b, sizeof b, " [owner=t%d count=%d readers=%d]", m->owner ? (int)m->owner - 1 : -1,
                     m->count, m->readers);
            return b;
        }
    }
    return "";
}
// ---- condition variables ----
static u64 deadline_from_abs(u32 ts) { // struct timespec {sec, nsec} on the wall clock
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long delta = ((long long)rd(ts, 4) * 1000000000LL + (i32)rd(ts + 4, 4)) -
                      ((long long)tv.tv_sec * 1000000000LL + tv.tv_usec * 1000LL);
    return mono_ns() + (delta > 0 ? (u64)delta : 0);
}
static int cond_wait_impl(CPU *c, u32 cond, u32 mutex, int timed, u64 dl) {
    Thread *t = curth;
    Mx *m = mx_get(mutex);
    if (t->phase == 0) { // first run: release the mutex and sleep on the condition
        SL("cond_wait %08x mutex %08x", cond, mutex);
        t->saved_count = m->count;
        m->owner = 0;
        m->count = 0;
        wake_obj(mutex, 1);
        t->phase = 1;
        t->timed_out = 0;
        return block_on(c, cond, timed ? dl : 0);
    }
    if (t->phase == 1) {
        t->result = t->timed_out ? 60 : 0;
        t->phase = 2;
    } // woken: signal or timeout
    if (m->owner)
        return block_on(c, mutex, 0); // reacquire the mutex
    m->owner = my_id();
    m->count = t->saved_count ? t->saved_count : 1;
    t->phase = 0;
    RET(t->result);
    return 0;
}
H(pthread_cond_wait) {
    return cond_wait_impl(c, A(0), A(1), 0, 0);
}
H(pthread_cond_timedwait) {
    return cond_wait_impl(c, A(0), A(1), 1, deadline_from_abs(A(2)));
}
H(pthread_cond_timedwait_relative_np) {
    return cond_wait_impl(c, A(0), A(1), 1, mono_ns() + (u64)rd(A(2), 4) * 1000000000ull + rd(A(2) + 4, 4));
}
H(pthread_cond_signal) {
    SL("cond_signal %08x", A(0));
    wake_obj(A(0), 1);
    RET(0);
    return 0;
}
H(pthread_cond_broadcast) {
    SL("cond_broadcast %08x", A(0));
    wake_obj(A(0), 0);
    RET(0);
    return 0;
}

// ---- mach semaphores ----
static int sem_count[256];
static int nsem = 1;
#define SEMOBJ(id) (0xFE000000u + ((id) & 255))
H(semaphore_create) {
    SL("sem_create %d value %d", nsem, (int)A(3));
    sem_count[nsem & 255] = (int)A(3);
    wr(A(1), nsem++, 4);
    RET(0);
    return 0;
}
H(semaphore_destroy) {
    RET(0);
    return 0;
}
H(semaphore_signal) {
    SL("sem_signal %u", A(0));
    sem_count[A(0) & 255]++;
    wake_obj(SEMOBJ(A(0)), 1);
    RET(0);
    return 0;
}
H(semaphore_signal_all) {
    SL("sem_signal_all %u", A(0));
    sem_count[A(0) & 255]++;
    wake_obj(SEMOBJ(A(0)), 0);
    RET(0);
    return 0;
}
static int sem_wait_impl(CPU *c, u32 id, int timed, u64 dl) {
    Thread *t = curth;
    int *n = &sem_count[id & 255];
    if (t->phase == 0)
        SL("sem_wait %u (count %d)", id, *n);
    if (*n > 0) {
        (*n)--;
        t->phase = 0;
        t->timed_out = 0;
        RET(0);
        return 0;
    }
    if (timed && t->phase && t->timed_out) {
        t->phase = 0;
        t->timed_out = 0;
        RET(49);
        return 0;
    } // KERN_OPERATION_TIMED_OUT
    t->phase = 1;
    t->timed_out = 0;
    return block_on(c, SEMOBJ(id), timed ? dl : 0);
}
H(semaphore_wait) {
    return sem_wait_impl(c, A(0), 0, 0);
}
H(semaphore_timedwait) {
    return sem_wait_impl(c, A(0), 1, mono_ns() + (u64)A(1) * 1000000000ull + A(2));
}

// ---- sleeping & spinning ----
static int sleep_ns(CPU *c, u64 ns) {
    Thread *t = curth;
    if (t->phase == 0) {
        t->phase = 1;
        t->timed_out = 0;
        return block_on(c, 0, mono_ns() + ns);
    }
    t->phase = 0;
    RET(0);
    return 0;
}
H(usleep) {
    return sleep_ns(c, (u64)A(0) * 1000);
}
H(pthread_yield_np) {
    RET(0);
    yield_req = 1;
    return 0;
}
H(OSSpinLockLock) {
    if (rd(A(0), 4))
        return yield_retry();
    wr(A(0), 0xffffffffu, 4);
    return 0;
}
H(OSSpinLockTry) {
    if (rd(A(0), 4))
        RET(0);
    else {
        wr(A(0), 0xffffffffu, 4);
        RET(1);
    }
    return 0;
}
H(OSSpinLockUnlock) {
    wr(A(0), 0, 4);
    return 0;
}

// ---- threads ----
#define MACH_BASE 0x2000u // mach thread port = MACH_BASE + index
static int thread_create_impl(CPU *c, u32 out, u32 attr, u32 start, u32 arg, int suspended) {
    (void)attr; // stack size requests are ignored: every stack gets a 4MB slot
    int i = thread_spawn(c, start, arg, suspended, 0);
    SL("thread_create -> t%d start=%08x arg=%08x %s", i, start, arg, suspended ? "(suspended)" : "");
    if (i < 0) {
        RET(35);
        return 0;
    } // EAGAIN
    wr(out, PTHREAD_BASE + 0x100 * i, 4);
    RET(0);
    return 0;
}
H(pthread_create) {
    return thread_create_impl(c, A(0), A(1), A(2), A(3), 0);
}
H(pthread_create_suspended_np) {
    return thread_create_impl(c, A(0), A(1), A(2), A(3), 1);
}
H(pthread_self) {
    RET(PTHREAD_BASE + 0x100 * (u32)(curth - threads));
    return 0;
}
H(pthread_mach_thread_np) {
    RET(MACH_BASE + (A(0) - PTHREAD_BASE) / 0x100);
    return 0;
}
H(pthread_from_mach_thread_np) {
    RET(PTHREAD_BASE + 0x100 * (A(0) - MACH_BASE));
    return 0;
}
H(thread_resume) {
    u32 i = A(0) - MACH_BASE;
    SL("thread_resume t%u", i);
    if (i < (u32)nthreads && threads[i].state == TS_SUSP)
        threads[i].state = TS_RUN;
    RET(0);
    return 0;
}
H(thread_suspend) {
    u32 i = A(0) - MACH_BASE;
    if (i < (u32)nthreads && &threads[i] != curth && threads[i].state == TS_RUN)
        threads[i].state = TS_SUSP;
    RET(0);
    return 0;
}
H(pthread_join) {
    u32 i = (A(0) - PTHREAD_BASE) / 0x100;
    if (A(0) < PTHREAD_BASE || i >= (u32)nthreads) {
        RET(3);
        return 0;
    } // ESRCH
    if (threads[i].state != TS_DEAD)
        return block_on(c, 0xFD000000u + i, 0);
    if (A(1))
        wr(A(1), threads[i].retval, 4);
    RET(0);
    return 0;
}
H(pthread_exit) {
    curth->retval = A(0);
    curth->state = TS_DEAD;
    nlive--;
    wake_obj(0xFD000000u + (u32)(curth - threads), 0);
    return BLOCKED;
}
H(pthread_key_create) {
    static int nkeys;
    wr(A(0), nkeys++, 4);
    RET(0);
    return 0;
}
H(pthread_key_delete) {
    RET(0);
    return 0;
}
H(pthread_getspecific) {
    RET(A(0) < 256 ? curth->tls[A(0)] : 0);
    return 0;
}
H(pthread_setspecific) {
    if (A(0) < 256)
        curth->tls[A(0)] = A(1);
    RET(0);
    return 0;
}
H(pthread_attr_init) {
    wr(A(0), 0x54484a41, 4);
    wr(A(0) + 8, 512 * 1024, 4);
    RET(0);
    return 0;
}
H(pthread_attr_setstacksize) {
    wr(A(0) + 8, A(1), 4);
    RET(0);
    return 0;
}

H(pthread_once) { // once_t = { long sig; char state[] }
    u32 ctl = A(0), fn = A(1);
    if (rd(ctl + 4, 4) == 0) {
        wr(ctl + 4, 1, 4);
        wr(ctl, 0x30B1BCC8, 4);
        call_guest(c, fn, 0, NULL);
    }
    RET(0);
    return 0;
}
H(ret0) {
    RET(0);
    return 0;
} // mutex/attr/cond init+lock+unlock, etc.
H(sched_get_priority_max) {
    RET(63);
    return 0;
}
H(sched_get_priority_min) {
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry threads_imports[] = {
    IMPORT(pthread_once), IMPORT(sched_get_priority_max), IMPORT(sched_get_priority_min),
    IMPORT(pthread_mutex_init), IMPORT(pthread_mutex_lock), IMPORT(pthread_mutex_trylock),
    IMPORT(pthread_mutex_unlock), IMPORT(pthread_mutexattr_init), IMPORT(pthread_mutexattr_settype),
    IMPORT(pthread_cond_wait), IMPORT(pthread_cond_timedwait), IMPORT(pthread_cond_timedwait_relative_np),
    IMPORT(pthread_cond_signal), IMPORT(pthread_cond_broadcast), IMPORT(semaphore_create),
    IMPORT(semaphore_destroy), IMPORT(semaphore_signal), IMPORT(semaphore_signal_all),
    IMPORT(semaphore_wait), IMPORT(semaphore_timedwait), IMPORT(usleep), IMPORT(pthread_yield_np),
    IMPORT(OSSpinLockLock), IMPORT(OSSpinLockTry), IMPORT(OSSpinLockUnlock), IMPORT(pthread_create),
    IMPORT(pthread_create_suspended_np), IMPORT(pthread_self), IMPORT(pthread_mach_thread_np),
    IMPORT(pthread_from_mach_thread_np), IMPORT(thread_resume), IMPORT(thread_suspend),
    IMPORT(pthread_join), IMPORT(pthread_exit), IMPORT(pthread_key_create), IMPORT(pthread_key_delete),
    IMPORT(pthread_getspecific), IMPORT(pthread_setspecific), IMPORT(pthread_attr_init),
    IMPORT(pthread_attr_setstacksize), {"_gethostbyname", h_ret0}, {"_pthread_mutex_destroy", h_ret0},
    {"_pthread_mutexattr_destroy", h_ret0}, {"_pthread_mutexattr_setprotocol", h_ret0},
    {"_pthread_cond_init", h_ret0}, {"_pthread_cond_destroy", h_ret0}, {"_pthread_cond_wait", h_ret0},
    {"_pthread_condattr_init", h_ret0}, {"_pthread_condattr_destroy", h_ret0},
    {"_pthread_attr_destroy", h_ret0}, {"_pthread_attr_setdetachstate", h_ret0},
    {"_pthread_attr_setschedparam", h_ret0}, {"_pthread_attr_setschedpolicy", h_ret0},
    {"_pthread_attr_getschedparam", h_ret0}, {"_pthread_getschedparam", h_ret0},
    {"_pthread_setschedparam", h_ret0}, {"_pthread_from_mach_thread_np", h_ret0},
    {"_pthread_rwlock_init", h_pthread_rwlock_reset}, {"_pthread_rwlock_destroy", h_pthread_rwlock_reset},
    {"_pthread_rwlock_rdlock", h_pthread_rwlock_rdlock},
    {"_pthread_rwlock_wrlock", h_pthread_rwlock_wrlock},
    {"_pthread_rwlock_unlock", h_pthread_rwlock_unlock},
    {0, 0}};
// clang-format on
