# Debugging

Every switch is an environment variable with a `DC_` prefix. A flag is on when it is set to anything except `0`.
Diagnostic output goes to stderr; the end-of-run report and thread dumps go to stdout. `./run.sh --log FILE` keeps stderr in a
file so you can read it while the game runs.

## Signals

| Signal | Effect |
| --- | --- |
| `kill -USR1 <pid>` | Print counters and every thread's state: what it is blocked on and its guest backtrace. Locks show their owner. This is the first thing to try when the game hangs |
| `kill -USR2 <pid>` | Capture the next frame to `/tmp/dc_shot.ppm`, trace every GL call of the frame after it to `/tmp/dc_gl.log`, and dump the intermediate textures to `/tmp/dc_tex/<id>.ppm` (`sips -s format png x.ppm --out x.png`) |

## Run control

| Variable | Meaning |
| --- | --- |
| `DC_MAXSECS=n` | Stop after n wall-clock seconds with a full thread dump |
| `DC_MAXINSN=n` | Stop after n guest instructions |
| `DC_PROGRESS=n` | Print instruction/frame/thread counters every n seconds |
| `DC_EXITDUMP` | Print the host stack when the process exits |
| `DC_MISSING` | At exit, list the imports that have no host implementation |
| `DC_NOPATCH` | Do not apply the binary patches (`src/loader/patches.h`) |

## Tracing the guest

| Variable | Meaning |
| --- | --- |
| `DC_TRACE` | Per-instruction trace (forces the interpreter; very slow) |
| `DC_BREAK=hexaddr` | Log the first hits of that guest address (registers, stack arguments). `DC_BREAKMAX=n` limits the hits, `DC_BREAKOBJ` also dumps the object in the first argument, `DC_BREAKSTR` prints eax as a string. Forces the interpreter, so it is only practical early in a run |
| `DC_PROFILE` | Instruction counts per 256-byte code bucket, printed at exit (interpreter) |
| `DC_PEEK="hexaddr[,words][;...]"` | Print guest memory in every `SIGUSR1` dump |
| `DC_IMPORTS` | Log every import served (very verbose) |
| `DC_SYNCLOG` | Trace semaphore and condition-variable traffic |
| `DC_SCHEDLOG` | Trace scheduler decisions |
| `DC_HEAPPROF` | Attribute guest heap bytes to their callers, printed at exit |

## Subsystem logs

| Variable | Subsystem |
| --- | --- |
| `DC_IOLOG` | File opens and stats |
| `DC_WINLOG`, `DC_EVLOG`, `DC_TIMERLOG`, `DC_MSGLOG[=n]` | Window creation, input events, NSTimers, Objective-C messages |
| `DC_HIDLOG` | Fake HID: device matching and every value delivered to the game |
| `DC_ORBITLOG` | Orbit requests and savegame operations |
| `DC_GLLOG` | Every GL call (name only) |
| `DC_GLERR` | Report the GL errors the game queries with `glGetError` |
| `DC_BINKLOG` | Bink movie playback |
| `DC_AUDIOLOG` | Mixer level and format every ~200 render callbacks |
| `DC_NOGAMEAUDIO` | Mute the in-game mixer (movie audio is unaffected) |

## Frames

`DC_SHOTFRAME=n DC_SHOT=file.ppm` writes frame n of the run to a file, for unattended runs.

## Input injection (unattended runs)

| Variable | Meaning |
| --- | --- |
| `DC_INJECTSPAM="start,period"` | From `start` seconds on, tap Return every `period` seconds. Gets through menus and movies |
| `DC_INJECTHOLD="sec,keycode"` | Press one macOS key code at `sec` seconds and hold it |
| `DC_INJECT="sec,type,chars,keycode;..."` | One-shot events (type 1 key down, 2 key up, 3 mouse down, 4 mouse up; `R` in `chars` means Return) |

## The JIT

| Variable | Meaning |
| --- | --- |
| `DC_NOJIT` | Use the interpreter only |
| `DC_JITSELFTEST=n` | Run n random differential tests (JIT vs interpreter) and exit; needs no game. `DC_JITSELFSEED`, `DC_JITSELFONLY=i`, `DC_JITSELFVERBOSE` narrow a failure down |
| `DC_JITCHECK=1` | Run every block in both engines and compare registers, live flags and all written bytes. `=2` with `DC_JITWIN=n` checks about n instructions with chaining enabled; `DC_JITCHECKAT=n` starts checking after n instructions |
| `DC_JITSTAT` | Coverage statistics and a histogram of opcodes that still go through the interpreter |
| `DC_JITPROF` | Sampling profiler: hottest guest blocks and host PCs |
| `DC_JITDUMP=hexaddr\|1` | Dump the guest bytes and generated host code of a block |
| `DC_JITNOCHAIN`, `DC_JITNOIBTC` | Disable block chaining / the indirect-branch table |

## Reading the end-of-run report

A run that stops prints one of:

- `STOPPED: <reason>` with registers, a guest backtrace and the last program counters. Look the addresses up in a disassembly
  of the executable (the unscrambled text is the guest memory at that address).
- `UNIMPLEMENTED IMPORT: _name` with the return address and the first arguments: implement it (see the architecture notes).
- `guest returned` / `guest called exit(n)`: the game ended.

Host crashes (the emulator itself faulting) leave a crash report under `~/Library/Logs/DiagnosticReports/`; the top frames
name the host function.
