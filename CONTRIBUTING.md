# Contributing

## Build and test

```sh
scripts/setup.sh    # Homebrew: ffmpeg, pkg-config
make                # build/dcompile
make test           # JIT differential self-test, no game needed
make smoke          # boots the game for ~45 s and checks it renders (needs the game and a display)
```

Read [docs/architecture.md](docs/architecture.md) first; [docs/debugging.md](docs/debugging.md) lists the diagnostic switches.

## Code style

- C11, plus Objective-C only under `src/host/`. Format with `make format` (clang-format, `.clang-format`); the style is
  4 spaces and 110 columns.
- Every header opens with a comment saying what the module is, what it assumes and what it provides. Keep it true.
- Layering: a header may only use what `src/dcompile.h` includes above it. If you need something from further down, forward
  declare it with a comment naming the header that defines it.
- Guest structures have the i386 Darwin layout; never overlay host structs on guest memory. Use `rd()`/`wr()` and explicit
  offsets.
- No game data, decrypted binaries, screenshots, logs or disassembly in the repository.
- Diagnostics are `DC_*` switches read with `DBG_FLAG()`/`dbg_env()` and documented in `docs/debugging.md`.

## Adding support for an import

See "Adding an import" in the architecture notes. A change that adds one should say which game code needed it.

## Reporting bugs

Include the output of the run (`./run.sh --log run.log`), the end-of-run report, and for hangs the thread dump
(`kill -USR1 <pid>`). For rendering problems attach a frame capture (`kill -USR2 <pid>`).
