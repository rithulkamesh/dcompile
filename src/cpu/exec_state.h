// exec_state.h: state shared by the interpreter loop, the JIT and the scheduler.
//
// Instruction budget/slice accounting, the optional BREAK/PROFILE debug hooks and the forward declarations that let the
// execution loop reach the scheduler (cur_tid) and the import dispatcher (serve) without depending on them.

enum { R_DONE, R_IMPORT, R_BAD, R_YIELD };
static int nest, nlive = 1, yield_req; // host->guest callback depth, live guest threads, voluntary-yield flag
static long slice_left = 1 << 30, SLICE = 20000; // instructions per scheduling slice (SLICE=n)
static int trace;
static int cur_tid(void); // sched.h
static u32 break_pc;
static int break_n, break_max = 20, break_obj,
                    break_str; // BREAK=hexaddr logs the first hits (esp, return address, args)
static u32 *prof_hist;
static int do_prof; // PROFILE=1: instruction count per 256-byte code bucket, printed at exit
static u64 ninsn, max_insn = 1ull << 62; // MAXINSN=n overrides
static int serve(CPU *c); // runtime/imports.h: run the host implementation of the import at c->eip; 0 if none
