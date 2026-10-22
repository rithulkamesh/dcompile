// memory.h: guest memory access.
//
// G is the base of the 4 GiB guest mapping, so guest address a lives at G + a. rd()/wr() are the checked accessors used by the
// interpreter and by every host-side import implementation; instruction fetch and stack helpers build on them.

static u8 *G; // guest memory, 4GB flat; guest addr a lives at G+a
#define P(a) (G + (u32)(a))

static void wlog_note(u32 a, u32 sz); // jit.h: memory-write log used by JITCHECK
static inline u32 rd(u32 a, int sz) {
    if (a < 0x1000)
        die("null read %08x", a);
    u32 v = 0;
    memcpy(&v, P(a), sz);
    return v;
}
static inline void wr(u32 a, u32 v, int sz) {
    if (a < 0x1000)
        die("null write %08x", a);
    if (__builtin_expect(wlog_on_, 0))
        wlog_note(a, sz);
    memcpy(P(a), &v, sz);
}
static inline u8 f8(CPU *c) {
    return *P(c->eip++);
}
static inline u32 f16(CPU *c) {
    u32 v = rd(c->eip, 2);
    c->eip += 2;
    return v;
}
static inline u32 f32(CPU *c) {
    u32 v = rd(c->eip, 4);
    c->eip += 4;
    return v;
}
static inline void push(CPU *c, u32 v) {
    c->r[ESP] -= 4;
    wr(c->r[ESP], v, 4);
}
static inline u32 pop(CPU *c) {
    u32 v = rd(c->r[ESP], 4);
    c->r[ESP] += 4;
    return v;
}
