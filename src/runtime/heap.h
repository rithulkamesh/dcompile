// heap.h: guest heap and static data allocation.
//
// hmalloc() is a bump allocator with exact-size free lists (no coalescing) over the guest range [HEAP_BASE, 2 GiB); the block size
// is stored just before each block. data_alloc() hands out never-freed storage in the DATA_BASE region (vtables, FILE objects,
// data symbols). DC_HEAPPROF attributes allocations to their callers.

static u32 data_top = DATA_BASE, heap_top = HEAP_BASE;

static u32 data_alloc(u32 n) {
    u32 a = data_top;
    data_top += (n + 15) & ~15u;
    return a;
}
static u32 freelist[4096]; // size-class free lists (16-byte classes up to 64KB), linked through word 0
static u32 hmalloc(u32 n) {
    u32 sz = (n + 15) & ~15u;
    if (sz < 65536 && freelist[sz >> 4]) {
        u32 a = freelist[sz >> 4];
        freelist[sz >> 4] = rd(a, 4);
        wr(a - 4, n, 4);
        return a;
    }
    u32 a = heap_top + 16;
    heap_top += 16 + sz;
    if (heap_top >= HEAP_LIMIT)
        die("guest heap exhausted");
    wr(a - 4, n, 4);
    return a; // size lives just before the block
}
static u32 gstr(const char *s) {
    u32 a = hmalloc(strlen(s) + 1);
    strcpy(GS(a), s);
    return a;
}
// ---------------- heap & memory/string ----------------
// HEAPPROF=1: attribute guest malloc bytes to the caller of operator new (frame pointer chain), printed on stop
static struct {
    u32 caller;
    u64 bytes, count;
} prof[256];
static int nprof, heapprof;
static void prof_add(CPU *c, u32 n) {
    u32 caller = rd(c->r[EBP] + 4, 4);
    int i;
    for (i = 0; i < nprof; i++)
        if (prof[i].caller == caller)
            break;
    if (i == nprof && nprof < 256)
        prof[nprof++].caller = caller;
    if (i < 256) {
        prof[i].bytes += n;
        prof[i].count++;
    }
}
static void prof_dump(void) {
    for (int k = 0; k < 8; k++) {
        int b = -1;
        for (int i = 0; i < nprof; i++)
            if (b < 0 || prof[i].bytes > prof[b].bytes)
                b = i;
        if (b < 0 || !prof[b].bytes)
            break;
        printf("  heap: caller %08x  %llu bytes in %llu allocs\n", prof[b].caller,
               (unsigned long long)prof[b].bytes, (unsigned long long)prof[b].count);
        prof[b].bytes = 0;
    }
}
H(malloc) {
    if (heapprof)
        prof_add(c, A(0));
    RET(hmalloc(A(0)));
    return 0;
}
H(calloc) {
    u32 n = A(0) * A(1);
    u32 p = hmalloc(n);
    memset(P(p), 0, n);
    RET(p);
    return 0;
}
static void free_guest(u32 a) { // bump allocator + exact-size free lists, no coalescing
    if (!a)
        return;
    u32 sz = (rd(a - 4, 4) + 15) & ~15u;
    if (sz < 65536) {
        wr(a, freelist[sz >> 4], 4);
        freelist[sz >> 4] = a;
    }
}
H(free) {
    free_guest(A(0));
    return 0;
}
H(realloc) {
    u32 old = A(0), n = A(1);
    if (old && n <= rd(old - 4, 4)) {
        RET(old);
        return 0;
    }
    if (!old) {
        RET(hmalloc(n));
        return 0;
    }
    u32 on = rd(old - 4, 4), p = hmalloc(n);
    memcpy(P(p), P(old), on < n ? on : n);
    RET(p);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry heap_imports[] = {
    IMPORT(malloc), IMPORT(calloc), IMPORT(realloc), IMPORT(free),
    {0, 0}};
// clang-format on
