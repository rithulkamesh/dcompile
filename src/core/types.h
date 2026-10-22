// types.h: fixed-width aliases, the guest address-space layout and the guest CPU state.
//
// The guest is a 32-bit x86 process living in a flat 4 GiB host mapping (see memory.h). The top of that address space is
// reserved for host-implemented "thunks": a guest call/jump to an address >= THUNK_BASE traps into the host import table.

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

// A 128-bit SSE register viewed at every element width.
typedef union {
    u8 b[16];
    u16 w[8];
    i16 sw[8];
    u32 d[4];
    i32 sd[4];
    u64 q[2];
    float f[4];
    double df[2];
} X128;
// The architectural state of one guest thread.
typedef struct {
    u32 r[8], eip;              // general registers (indexed by EAX..EDI) and the instruction pointer
    int cf, pf, zf, sf, of, df; // flags, one int each (the interpreter keeps them materialised)
    double st[8];               // x87 stack registers (host doubles)
    int ftop;                   // x87 top-of-stack index
    u32 fsw, fcw;               // x87 status and control words
    X128 xmm[8];                // SSE registers
    u32 mxcsr;                  // SSE control/status (rounding mode lives here)
    long jb;                    // JIT: instruction budget left in the current slice
    int flo;                    // JIT: flags live at the last block exit (used by DC_JITCHECK)
} CPU;
enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
