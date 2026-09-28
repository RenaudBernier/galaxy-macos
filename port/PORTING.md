# macOS port: conventions

The host build compiles the decomp sources with clang for arm64 macOS
(LP64, little-endian). Everything port-specific is guarded by `TARGET_PC`
so the original Metrowerks build keeps working. Game data is not part of
this repository; it is read from a user-supplied extracted disc
(`RMGK01`).

## Build flags

The decomp is compiled as `-std=gnu++17 -fshort-wchar -fno-exceptions
-fno-rtti -Wno-register -DTARGET_PC=1`, with `port/include/port/prelude.h`
force-included. `port/include` comes first on the include path and holds
shims for MSL-only headers (`mem.h`, `va_list.h`, `extras.h`, ...).

Syntax-check any set of files with:

    python3 port/tools/trial_compile.py [--show] [--grep REGEX] <dirs/files...>

## Types

* `u32`/`s32` are `uint32_t`/`int32_t` on the host (they were `long` on the
  Wii). Don't write `long`/`unsigned long`/`123l` in decomp code; use
  `s32`/`u32`/`(s32)123`. (`port/tools/fix_long*.py` did the bulk rewrite.)
* `wchar_t` is 16-bit (`-fshort-wchar`), as on the Wii. Host libc wide-string
  functions assume 32-bit `wchar_t`; the port runtime supplies 16-bit versions.
* Integers smuggled through pointers (`(OSMessage)1`) round-trip through
  `uintptr_t` casts unchanged.

## Pointers and the emulated Wii address space

`port/include/port/wii_addr.h`: all game memory (JKRHeap arenas, game thread
stacks, the executable's static data) lives in one window presented to the
game as Wii addresses `0x80000000-0xFFFFFFFF`.

* Pointer to 32-bit integer: `PTR_TO_U32(p)` (never `(u32)p`,
  `reinterpret_cast<u32>(p)` or `(s32)p`).
* 32-bit integer back to pointer: `U32_TO_PTR(T*, a)` (never `(T*)a` or
  `reinterpret_cast<T*>(a)` when `a` is a 32-bit value holding an address).
* Values below `0x80000000` pass through both macros unchanged, so
  `>= 0x80000000` pointer-vs-offset checks keep working.
* Pure pointer differences can stay as pointer arithmetic:
  `(u32)((u8*)a - (u8*)b)`.
* Alignment of real pointers: `U32_TO_PTR(T*, ALIGN_NEXT(PTR_TO_U32(p), n))`
  is fine (the window base is page-aligned).
* Both macros are plain casts on the Wii build, so they are safe to use
  unguarded.

## Game data (big-endian, 32-bit pointers inside files)

Everything read from the disc is big-endian, and structures overlaid on file
data must keep their Wii layout (`port/include/port/endian.h`,
`port/include/port/wii_addr.h`):

* Integer/float/enum fields of file structures: `BE(u32)`, `BE(f32)`, ...
  (same size, converts on access; plain `T` on the Wii build).
* Pointer fields inside file structures (set at runtime or relocated from
  offsets): `PTR32(T)` - a 4-byte Wii address with implicit `T*` conversion.
  Relocation code must read the original *offset* big-endian (e.g.
  `PortReadBE32(&field)`) before storing the pointer.
* Bitfields in file structures: MWCC lays them out from the most significant
  bit; replace with read-only proxies over the raw bytes on `TARGET_PC`
  (see `JKRArchive::SDIFileEntry`).
* Raw reads of file bytes: `PortReadBE16/32/F32(p)` (unaligned-safe).
* Data consumed by the CPU in bulk (vertex arrays, animation keys) may be
  swapped in place once at load time instead; guard against swapping twice.
* Display lists and texture images stay big-endian: Aurora parses them.
* Mark code that relocates offsets into pointers with `// PORT: file-reloc`.

## MWCC leniencies to fix

* Unqualified names from dependent base classes: use `this->`.
* `nullptr` used as an integer or bool (it was `#define nullptr 0`): use `0` /
  `false`.
* Jumping past initialisations in `switch`/`goto`: add a block `{ }` around
  the case body.
* `0f`/`1f` float literals: `0.0f`/`1.0f`.
* Inline PowerPC `asm` blocks: add an `#ifdef TARGET_PC` C/C++ equivalent next
  to the original (keep the original under `#else`).
* Paired-single intrinsics (`__psq_*`) and `__declspec(section ...)`: the same.

## Code that only makes sense on hardware

Hardware register access, the MetroTRK debugger, the Wii's own runtime/MSL,
and the RVL SDK sources are not compiled. The SDK API the game calls is
implemented under `port/src/` on top of SDL3 (window, input, audio) and a GX
graphics backend.
