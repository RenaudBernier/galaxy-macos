Super Mario Galaxy for macOS
============================

An unofficial native macOS port of Super Mario Galaxy for Apple Silicon Macs,
built on the [Petari](https://github.com/SMGCommunity/Petari) decompilation.

This fork compiles Petari's decompiled game code (Game, JSystem, nw4r) with
clang and runs it on a host implementation of the Wii SDK. Graphics go through
[Aurora](https://github.com/encounter/aurora), which reimplements the Wii's GX
graphics API on Metal; audio, input, saves and disc access are handled by the
port layer in [`port/`](port). The game code is not emulated: it runs as native
arm64 code.

> [!IMPORTANT]
> This is not the Petari project. Petari is a matching decompilation and is
> not meant to be a PC port, so please don't ask about this port in the Petari
> repository or its Discord server.

**No game data is included.** You need your own copy of the game: a disc image
of the Korean release, `RMGK01` (the version Petari decompiles). The game reads
everything from it at runtime.

Requirements
============

- A Mac with Apple Silicon.
- Xcode command line tools (`xcode-select --install`), which also provide
  Python 3.
- CMake 3.25 or later, Ninja and SDL3:

  ```sh
  brew install cmake ninja sdl3
  ```

- A disc image of Super Mario Galaxy (Korea, `RMGK01`), in any format Dolphin
  reads: `.iso`, `.rvz`, `.wbfs`, ...

Building
========

```sh
git clone https://github.com/RenaudBernier/galaxy-macos.git
cd galaxy-macos
cmake -S port -B build/port -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/port
```

The first configure downloads Aurora and its dependencies, so it needs an
internet connection. Aurora is patched automatically with
[`port/patches/aurora`](port/patches/aurora). The build produces
`build/port/Super Mario Galaxy.app`.

Running
=======

```sh
"build/port/Super Mario Galaxy.app/Contents/MacOS/Super Mario Galaxy" /path/to/RMGK01.rvz
```

or set `SMG_DISC` to the image path. Saves are stored in
`~/Library/Application Support/SuperMarioGalaxy`.

At the title screen, press A and B together (left click and right click, or
Space and Left Shift). Menus use the Wii Remote pointer: aim with the mouse and
click. Controllers are supported too. See [port/README.md](port/README.md) for
the full controls and the environment options.

Status
======

Boot, the title screen, file select, the prologue and gameplay run at 60 fps
with sound, and every galaxy loads. On displays of 100 Hz or more, the port
shows 120 fps by rendering an interpolated frame between game frames (see
[port/README.md](port/README.md#frame-rate)). Miis and the HOME Menu are
disabled, and later-game content has not been fully tested yet. See
[port/STATUS.md](port/STATUS.md) for details and known gaps.

The decompilation
=================

This fork keeps Petari's decompilation and its matching build (`configure.py`
and `ninja`, which rebuild the original Wii executable). For that build, and to
contribute to the decompilation itself, see the
[Petari repository](https://github.com/SMGCommunity/Petari). Most changes made
for the port are guarded by `TARGET_PC`; the matching build has not been
re-checked against this fork.

Credits
=======

- [Petari](https://github.com/SMGCommunity/Petari) and its contributors, for the
  decompilation this port is built on, and the projects Petari credits:
  [doldecomp](https://github.com/doldecomp/sdk_2009-12-11),
  [tp](https://github.com/zeldaret/tp) and
  [ogws](https://github.com/doldecomp/ogws).
- [Aurora](https://github.com/encounter/aurora), for GX on modern graphics APIs,
  window management and disc image access.
