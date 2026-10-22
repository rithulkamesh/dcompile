// dcompile.h: the umbrella header; the whole program is one translation unit built from these modules, in dependency order.
//
//
// Why one translation unit: the interpreter, the JIT helpers and every import handler share the guest-memory accessors and the
// CPU struct, and the hot paths depend on those being inlined. Module boundaries are enforced by layering (a header only uses
// what is included above it) and documented in docs/architecture.md.

// -- core: types, errors, guest memory, debug switches
#include "core/platform.h"
#include "core/types.h"
#include "core/memory_map.h"
#include "core/debug.h"
#include "core/error.h"
#include "core/memory.h"
#include "core/thunk_table.h"

// -- cpu: the x86-32 interpreter and its JIT
#include "cpu/operands.h"
#include "cpu/sse.h"
#include "cpu/twobyte.h"
#include "cpu/fpu.h"
#include "cpu/interp.h"
#include "cpu/exec_state.h"
#include "jit/jit.h"
#include "cpu/run.h"

// -- runtime: threads and the host side of the imports (libc, pthreads, POSIX, C++ library)
#include "runtime/sched.h"
#include "runtime/hle.h"
#include "runtime/heap.h"
#include "runtime/files.h"
#include "runtime/data_symbols.h"
#include "runtime/libc_string.h"
#include "runtime/libc_wide.h"
#include "runtime/libc_printf.h"
#include "runtime/libc_stdio.h"
#include "runtime/libc_process.h"
#include "runtime/libc_atomic.h"
#include "runtime/libc_math.h"
#include "runtime/threads.h"
#include "runtime/handles.h"

// -- cocoa: Objective-C runtime and Foundation/CoreFoundation
#include "cocoa/objc_classes.h"
#include "cocoa/foundation.h"
#include "video/capture.h"
#include "input/inject.h"
#include "cocoa/appkit.h"
#include "cocoa/objc_dispatch.h"
#include "cocoa/plist.h"
#include "input/hid.h"
#include "cocoa/cf.h"
#include "runtime/posix.h"
#include "runtime/cxx.h"
#include "runtime/stubs.h"
#include "runtime/fs.h"

// -- platform services: online (Orbit), audio, video, media
#include "online/orbit.h"
#include "audio/audio.h"
#include "media/bink.h"
#include "video/gl.h"
#include "video/display.h"

// -- dispatch and loading
#include "runtime/imports.h"
#include "loader/image.h"
#include "loader/dyld_info.h"
#include "loader/build_id.h"
#include "loader/protection.h"
#include "loader/macho.h"
#include "loader/dyld.h"
#include "loader/patches.h"
