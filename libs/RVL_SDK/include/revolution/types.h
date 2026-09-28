#ifndef TYPES_H
#define TYPES_H

#ifdef TARGET_PC
// Host build: the Wii is ILP32, so `long` is 32 bits there. On LP64 hosts it is
// 64 bits, so fixed-width types are required to keep struct layouts intact.
#include <stdint.h>
#include <stddef.h>
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
#endif

// Pointer <-> 32-bit address conversions. Plain casts on the Wii; on 64-bit
// hosts they translate through the emulated Wii address window.
#ifdef TARGET_PC
#include <port/wii_addr.h>
#include <port/endian.h>
#else
#define PTR_TO_U32(p) ((u32)(p))
#define U32_TO_PTR(T, a) ((T)(a))
// File-data field annotations; plain types on the Wii (see port/include/port).
#define BE(T) T
#define PTR32(T) T*
#endif

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float f32;
typedef double f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;

typedef int BOOL;

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

#if !defined(TARGET_PC) && !defined(nullptr)
#ifdef __cplusplus
#define nullptr 0
#endif
#endif

#if !defined(TARGET_PC) && !defined(override)
#ifdef __cplusplus
#define override
#endif
#endif

#ifdef __MWERKS__
#define __REGISTER register
#else
#define __REGISTER
#endif

#if !defined(AT_ADDRESS)
#if defined(__MWERKS__)
#define AT_ADDRESS(x)							: x
#else
#define AT_ADDRESS(x)
#endif
#endif

#if __MWERKS__
#if !defined(METRO_TRK) && __MWERKS__ >= 0x3000
#define ALWAYS_INLINE __attribute__((always_inline))
#define NO_INLINE __attribute__((noinline))
#else
#define ALWAYS_INLINE
#define NO_INLINE
#endif
#else
#define ALWAYS_INLINE
#define NO_INLINE
#endif

#if __MWERKS__
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#elif defined(TARGET_PC)
// Host builds need real alignment (GPU buffers, 32-byte DMA-style copies).
#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#endif
#else
#define ATTRIBUTE_ALIGN(num)
#endif

#if __MWERKS__
#define ATTRIBUTE_PACKED __attribute__((packed))
#else
#define ATTRIBUTE_PACKED
#endif

#if __MWERKS__ || defined(TARGET_PC)
#define ATTRIBUTE_WEAK __attribute__((weak))
#else
#define ATTRIBUTE_WEAK
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define ROUND_UP(x, align) (((x) + (align) - 1) & (-(align)))
#define ROUND_UP_PTR(x, align) ((void*)((((u32)(x)) + (align) - 1) & (~((align) - 1))))

#define ALIGN_PREV(X, N) ((X) & ~((N) - 1))
#define ALIGN_NEXT(X, N) ALIGN_PREV(((X) + (N) - 1), N)

#define ARRAY_SIZE(o) (s32)(sizeof(o) / sizeof(o[0]))
#define ARRAY_SIZEU(o) (sizeof(o) / sizeof(o[0]))

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define FOURCC(c0, c1, c2, c3) (u32)((c0 & 0xFF) << 24 | (c1 & 0xFF) << 16 | (c2 & 0xFF) << 8 | (c3 & 0xFF))

#define IS_ALIGNED(x, align) (((unsigned long)(x) & ((align) - 1)) == 0)
#define IS_NOT_ALIGNED(X, N) (((X) & ((N) - 1)) != 0)

// Comparing a non-volatile reference type to NULL is tautological
// and triggers a warning on modern compilers, but in some cases is
// required to match the original assembly.
#if defined(__MWERKS__) || defined(DECOMPCTX)
#define IS_REF_NULL(r) (&(r) == NULL)
#define IS_REF_NONNULL(r) (&(r) != NULL)
#else
#define IS_REF_NULL(r) (0)
#define IS_REF_NONNULL(r) (1)
#endif

/* just some common intrinsics */

#ifndef __MWERKS__
f32 __frsqrte(f32);
u32 __cntlzw(u32);
s32 __abs(s32);
f32 __fabsf(f32);
f64 __fabs(f64);
void* __memcpy(void*, const void*, int);
#endif

#endif  // TYPES_H
