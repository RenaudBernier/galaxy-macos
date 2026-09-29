// Force-included into every translation unit of the host (PC/macOS) build.
// Supplies stand-ins for Metrowerks/PowerPC-specific compiler features.
#pragma once

#ifndef TARGET_PC
#define TARGET_PC 1
#endif

#ifdef __cplusplus
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#else
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

// All decompiled game code goes into its own section. The port's global
// operator new uses the caller's address to send game allocations to JKRHeap
// and everything else to the host allocator (port/src/memory.cpp).
#pragma clang section text = "__TEXT,__game,regular,pure_instructions"

// Its global variables go into sections of their own too, so save states
// (port/src/savestate.cpp) can capture the game's state and nothing else.
#pragma clang section bss = "__DATA,__game_bss" data = "__DATA,__game_data"

// Metrowerks pragmas that clang doesn't know are harmless; silence the noise.
#pragma clang diagnostic ignored "-Wunknown-pragmas"

// MWCC: `__declspec(section ".init")`, `__declspec(weak)` etc.
#define __declspec(x) PORT_DECLSPEC_##x
#define PORT_DECLSPEC_weak __attribute__((weak))
#define PORT_DECLSPEC_force_export
#define PORT_DECLSPEC_section

// PowerPC intrinsics used by game code.
static inline float __frsqrte(float x) { return 1.0f / sqrtf(x); }
static inline float __fres(float x) { return 1.0f / x; }
static inline double __fabs(double x) { return fabs(x); }
static inline float __fabsf(float x) { return fabsf(x); }
static inline double __fnabs(double x) { return -fabs(x); }
static inline int __abs(int x) { return x < 0 ? -x : x; }
static inline unsigned int __cntlzw(unsigned int x) { return x == 0 ? 32 : (unsigned int)__builtin_clz(x); }
static inline void __sync(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void __isync(void) {}

#ifdef __cplusplus
#include "port/msl_compat.hpp"
#endif

#ifdef __cplusplus
// JSystem heap-aware operator new overloads. MWCC resolves these at template
// instantiation time; clang needs them declared before the first use.
class JKRHeap;
void* operator new(size_t, int);
void* operator new(size_t, JKRHeap*, int);
void* operator new[](size_t, int);
void* operator new[](size_t, JKRHeap*, int);
#endif

#ifdef PORT_AURORA
// The decomp refers to GX types by their RVL struct/enum tags (_GXColor...);
// Aurora's headers name them GXColor or declare them anonymously.
#define _GXRenderModeObj GXRenderModeObj
#define _GXTlut GXTlut
#define _GXTlutFmt GXTlutFmt
#define _GXTexObj GXTexObj
#define _GXTexMapID GXTexMapID
#define _GXColor GXColor
#define _GXAttr GXAttr
#define _GXTlutObj GXTlutObj
#define _GXTexMtxType GXTexMtxType
#define _GXCompType GXCompType
#define _GXColorS10 GXColorS10
#define _GXVtxAttrFmtList GXVtxAttrFmtList
#define _GXTlutSize GXTlutSize
#define _GXTexWrapMode GXTexWrapMode
#define _GXTexFmt GXTexFmt
#define _GXFogAdjTable GXFogAdjTable
#define _GXCullMode GXCullMode
#define _GXLightObj GXLightObj
#define _GXFifoObj GXFifoObj
#define _GXTexFilter GXTexFilter
#define _GXCompCnt GXCompCnt
#define _GXVtxFmt GXVtxFmt
#define _GXPrimitive GXPrimitive
#define _GXTevStageID GXTevStageID
#define _GXChannelID GXChannelID
#endif

// The game's wchar_t is 16 bits (-fshort-wchar); the host C library's wide
// string functions assume 32 bits. Route game calls to 16-bit versions
// (port/src/runtime/wchar16.c) without affecting host code.
#ifdef __cplusplus
extern "C" {
#endif
size_t PortWcslen16(const wchar_t*);
wchar_t* PortWcsncpy16(wchar_t*, const wchar_t*, size_t);
wchar_t* PortWcscpy16(wchar_t*, const wchar_t*);
int PortWcscmp16(const wchar_t*, const wchar_t*);
int PortWcsncmp16(const wchar_t*, const wchar_t*, size_t);
wchar_t* PortWcscat16(wchar_t*, const wchar_t*);
wchar_t* PortWcschr16(const wchar_t*, wchar_t);
int PortVswprintf16(wchar_t*, size_t, const wchar_t*, va_list);
int PortSwprintf16(wchar_t*, size_t, const wchar_t*, ...);
#ifdef __cplusplus
}
#endif
#define wcslen PortWcslen16
#define wcsncpy PortWcsncpy16
#define wcscpy PortWcscpy16
#define wcscmp PortWcscmp16
#define wcsncmp PortWcsncmp16
#define wcscat PortWcscat16
#define wcschr PortWcschr16
#define vswprintf PortVswprintf16
#define swprintf PortSwprintf16

// MSL-compatible sprintf family (port/src/runtime/msl_printf.c): the game
// relies on MSL behaviour such as NULL "%s" printing as "".
#ifdef __cplusplus
extern "C" {
#endif
int PortVsnprintfMsl(char*, size_t, const char*, va_list);
int PortSnprintfMsl(char*, size_t, const char*, ...);
int PortVsprintfMsl(char*, const char*, va_list);
int PortSprintfMsl(char*, const char*, ...);
#ifdef __cplusplus
}
#endif
#define sprintf PortSprintfMsl
#define snprintf PortSnprintfMsl
#define vsprintf PortVsprintfMsl
#define vsnprintf PortVsnprintfMsl
