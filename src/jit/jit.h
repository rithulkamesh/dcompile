// jit.h: x86-32 -> ARM64 dynamic binary translator.
//
// Guest basic blocks are translated on demand into a MAP_JIT code cache keyed by guest eip and chained with direct branches.
// Indirect branches (ret, call/jmp r/m) go through a direct-mapped lookup table (IBTC) probed from generated code.
//
// Register mapping: the eight guest registers live in host x19..x26, x27 = CPU*, x28 = G (guest memory base). Guest memory is
// reached with `ldr w, [x28, wEA, uxtw]`, so the 32-bit guest address wraps exactly like the interpreter's.
//
// Flags: inside a block, ARM NZCV carries ZF/SF/CF/OF lazily; PF and DF always live in the CPU struct. At block exits and before
// helper calls the *live* flags are materialised. Liveness is computed per block (backwards) with a bounded look-ahead into the
// successors; ret/indirect exits assume flags are dead (ABI).
//
// Anything not translated natively is executed by calling exec(c) for exactly that instruction (the "fallback").
//
// Budget: every block starts with a check on CPU.jb (instructions left); a block that finds it exhausted exits to C with eip =
// its own start. Imports (eip >= THUNK_BASE) and the stop/return magics have no block, so control returns to jit_run().
//
// Debug switches (see docs/debugging.md): DC_NOJIT, DC_JITCHECK, DC_JITSELFTEST, DC_JITSTAT, DC_JITPROF, DC_JITDUMP,
// DC_JITNOCHAIN, DC_JITNOIBTC.

#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <stddef.h>
#include <signal.h>
#include <dlfcn.h>
#include <sys/time.h>

#include "jit/encoders.h"
#include "jit/decoder.h"
#include "jit/cache.h"
#include "jit/regstate.h"
#include "jit/codegen.h"
#include "jit/translate.h"
#include "jit/helpers.h"
#include "jit/difftest.h"
#include "jit/jit_run.h"
#include "jit/selftest.h"
#include "jit/prof.h"
