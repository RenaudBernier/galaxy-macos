# Super Mario Galaxy for macOS (work in progress)

A native macOS (Apple Silicon) build of the Petari decompilation of Super
Mario Galaxy. The decompiled game code is compiled with clang and runs on a
host implementation of the Wii SDK; graphics go through
[Aurora](https://github.com/encounter/aurora) (GX on WebGPU/Metal, SDL3).

**No game data is included.** You need your own copy of the game: a disc
image of the Korean release (`RMGK01`, the version the decompilation targets),
in any format Dolphin/nod read (`.iso`, `.rvz`, `.wbfs`, ...).

## Building

Requirements: Xcode command line tools, CMake >= 3.25, Ninja, SDL3
(`brew install cmake ninja sdl3`).

    cmake -S port -B build/port -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build/port

Aurora is fetched and patched automatically (`port/patches/aurora`). To use a
local checkout instead, pass `-DAURORA_SOURCE_DIR=/path/to/aurora` (with the
patches applied).

## Running

    "build/port/Super Mario Galaxy.app/Contents/MacOS/Super Mario Galaxy" /path/to/RMGK01.rvz

or set `SMG_DISC`. Saves go to `~/Library/Application Support/SuperMarioGalaxy`.
If the game crashes, the report is also saved to
`~/Library/Logs/SuperMarioGalaxy/crash.log`.

At the title screen press A and B together. Menus (file select, dialogs)
use the Wii Remote pointer: aim with the mouse and click.

Environment options:

| Variable | Effect |
|---|---|
| `SMG_DEBUG=1` | verbose logging |
| `SMG_QUIET=1` | hide the game's own debug output |
| `SMG_LANGUAGE` | system language (0-9) |
| `SMG_WINDOW=<width>x<height>` | initial window size (default 1280x720) |
| `SMG_ASPECT=4:3` or `16:9` | which layouts the HUD and menus use (see below) |
| `SMG_NOAUDIO=1`, `SMG_VOLUME=0..1` | disable audio / output gain |
| `SMG_NAND_DIR=<dir>` | use another save directory (e.g. a scratch one) |
| `SMG_STATE_DIR=<dir>` | use another directory for save states |
| `SMG_INPUT_RECORD=<file>` | record the inputs as an `SMG_INPUT_SCRIPT` timeline |
| `SMG_INPUT_SCRIPT="frame:TOKENS;..."` | scripted inputs (see `src/input.cpp`); `AUTOA` answers "press A" prompts |

## Screen shape

The game fills the window at any aspect ratio: resize it or go fullscreen and
the 3D view widens or narrows to match, with the HUD and menus kept in
proportion. Movies keep their 16:9 shape.

The HUD and menus come in the game's 4:3 and 16:9 versions. The 16:9 ones
reach the edges of a 16:9 screen but need one at least that wide, so they are
used on 16:9 and wider displays; on narrower displays (such as a MacBook's
16:10 screen) the 4:3 versions are used, centered on wider windows.
`SMG_ASPECT` overrides the choice.

## Controls (emulated Wii Remote + Nunchuk)

| Wii             | Keyboard / mouse          | Gamepad                  |
|-----------------|---------------------------|--------------------------|
| Pointer         | mouse                     | right stick              |
| A               | left click / Space        | south button             |
| B (Star Bits)   | right click / Left Shift  | east button, R           |
| Z               | Left Ctrl / Q             | L                        |
| C               | E                         | north button             |
| Shake (spin)    | F / middle click          | west button              |
| Nunchuk stick   | W A S D                   | left stick               |
| D-pad           | arrow keys                | D-pad                    |
| - / + (pause)   | - / = (or Enter, Esc)     | Back / Start             |
| 1 / 2           | 1 / 2                     | stick clicks             |
| HOME (no menu)  |                           | Guide                    |
| Tilt remote     | I / K / J / L             |                          |
| Hold upright    | V (toggle)                |                          |

## Save states

The **States** menu in the menu bar saves the whole game into one of five
slots and loads it back: **Save State** (Shift+Cmd+1 to 5) and **Load State**
(Cmd+1 to 5). Each slot shows when it was saved. States are kept in
`~/Library/Application Support/SuperMarioGalaxy/states`, apart from the game's
own save file, and last across launches.

- A state loads only into the build of the game that saved it.
- States are taken and loaded between frames, when the game isn't in the
  middle of something. While it is (loading a stage, for instance), the request
  waits for up to three seconds, then the window title says it can't be done
  right now.
- To load states saved in an earlier session, the game has to sit at the same
  memory addresses every time, so it relaunches itself once at startup with
  address space layout randomization turned off, as debuggers launch programs.

## How it works

See [PORTING.md](PORTING.md) for the conventions used in the decompiled code.
In short:

* **Types and pointers:** the game is ILP32; `u32`/`s32` are fixed-width and
  pointers stored in 32-bit fields go through an emulated Wii address window
  (`include/port/wii_addr.h`).
* **Data:** game files are big-endian; file structures use `BE(T)` fields and
  `PTR32(T)` 4-byte pointers (`include/port/endian.h`).
* **Threads:** the Wii's single-core, priority-based OS scheduler is emulated
  on host threads (`src/os/scheduler.cpp`); hardware interrupts (VI retrace,
  alarms, DVD, audio DMA) are delivered at preemption points.
* **Memory:** game allocations go to the game's own heaps; host libraries use
  the system allocator (`src/memory.cpp`).
* **Graphics:** Aurora implements GX; a few Wii-only calls live in
  `src/gx_rvl.cpp`.

## Status

Playable: boot, title, file select, the prologue (picture book, letter and the
Bowser attack movie) and gameplay run at 60 fps with sound. Every galaxy and
hub stage loads and runs its intro. See [STATUS.md](STATUS.md) for the known
gaps (Miis, HOME Menu, a few GX features).
