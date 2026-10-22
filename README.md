# dcompile

Run a 32-bit Intel Mac game on an Apple silicon Mac, with no Rosetta, by emulating the CPU and the operating system around it
in-process.

macOS stopped running 32-bit programs in 10.15, and Rosetta never supported them. dcompile loads the game's i386 Mach-O
executable into a flat 4 GiB address space and executes it with an x86-32 interpreter and an ARM64 just-in-time translator.
Every system call, library call and framework call the game makes is answered by a host implementation (libc, pthreads,
CoreFoundation, an Objective-C runtime and Cocoa shim, OpenGL, CoreAudio, IOKit HID, ...). Graphics are forwarded to the
host's OpenGL (which is layered on Metal), video is decoded with FFmpeg, and the online service is replaced by an offline
stand-in with local savegames.

The supported title is **Assassin's Creed II** (Mac, Steam build). The architecture is not specific to it; see
[docs/architecture.md](docs/architecture.md).

> **You need your own copy of the game.** This repository contains no game code or assets. It is not affiliated with or
> endorsed by Ubisoft. See [Legal notes](#legal-notes).

## Status

| Area | State |
| --- | --- |
| Boot, loader, static initialisers, threads | works |
| Video: intro movies, menus, 3D rendering | works, with known issues below |
| Audio: movies and the in-game mixer | works |
| Keyboard and mouse | works (menus: keyboard; mouse look in game) |
| Saves | works (stored locally) |
| Online features | not available (offline stand-in) |

Known issues are tracked in the [issue list](../../issues). At the time of writing: mouse clicks do not register in menus, and
the screen stays white after the first playable section (the second level load).

## Requirements

- A Mac with Apple silicon, macOS 12 or later
- Xcode command line tools (`xcode-select --install`)
- [Homebrew](https://brew.sh), for FFmpeg and pkg-config
- Assassin's Creed II for Mac, installed through Steam

## Quick start

```sh
scripts/setup.sh      # brew install ffmpeg pkg-config
make                  # builds build/dcompile
./run.sh              # finds the Steam install and starts the game
```

`./run.sh --game "/path/to/Assassin's Creed 2.app/Contents/MacOS/scimitar_sf"` runs a specific executable, and the
`DCOMPILE_GAME` environment variable sets the default. The first minutes are slow: the game boots through the interpreter
and the JIT warms up while it plays the opening movies.

Savegames are written to `~/Library/Application Support/Ubisoft/Assassin's Creed 2/saves/`.

### Controls

The game's own keyboard and mouse bindings apply (`DefaultBindings.map` inside the game bundle): WASD to move, the mouse to
look, Return to confirm in menus.

## Documentation

- [docs/architecture.md](docs/architecture.md): how it works, module by module
- [docs/debugging.md](docs/debugging.md): every diagnostic switch, signals, capturing a frame
- [CONTRIBUTING.md](CONTRIBUTING.md): building, testing, code style, adding an import

## Repository layout

```
src/
  main.c            entry point
  dcompile.h        umbrella header: the modules in dependency order
  core/             types, guest memory, memory map, errors, debug switches
  cpu/              x86-32 interpreter (integer, x87, SSE)
  jit/              x86-32 -> ARM64 translator and its self-test
  loader/           Mach-O loader, dyld-style binding, dlopen, game-specific patches
  runtime/          scheduler, threads, libc, POSIX, C++ library, import dispatch
  cocoa/            Objective-C runtime shim, Foundation, AppKit, CoreFoundation
  video/            OpenGL forwarding, CoreGraphics display, frame capture
  audio/            CoreAudio graph and the host audio sinks
  media/            Bink video on top of FFmpeg
  input/            fake IOHID keyboard/mouse, input injection for tests
  online/           offline stand-in for the Ubisoft Orbit client, savegames
  host/             the only Objective-C: the real window, GL context and audio queue
scripts/            setup, run, smoke test, formatting
docs/
```

## Tests

```sh
make test      # JIT vs interpreter differential self-test; needs no game files
make smoke     # boots the game and checks that it renders (needs the game and a display)
```

## Legal notes

This is an independent compatibility layer. It does not contain, and must never be used to distribute, any part of the game.
It runs the copy you own, on your own machine.

Two features touch the game's protections, so you should know about them:

- The main executable's code section is stored scrambled. dcompile recovers the key from the executable at load time
  (`src/loader/protection.h`) rather than embedding it, and only operates on a file you supply.
- The game expects Ubisoft's online launcher. `src/online/orbit.h` is a from-scratch offline stand-in, and
  `src/loader/patches.h` contains a few binary patches that stop the game from treating the missing connection as fatal.
  The patches only apply to the exact executable they were written for (identified by SHA-256).

Laws on interoperability and circumvention differ between countries, and the above is not legal advice. If you are unsure
whether using or contributing to this project is allowed where you live, ask a lawyer.

"Assassin's Creed" and "Ubisoft" are trademarks of their owners and are used here only to say which software the project is
compatible with.

## License

[MIT](LICENSE). FFmpeg, which dcompile links against dynamically, has its own licensing: see
[docs/third-party.md](docs/third-party.md).
