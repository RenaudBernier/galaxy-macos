#ifndef OSFASTCAST_H
#define OSFASTCAST_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TARGET_PC
// Host equivalents of the paired-single quantized load/store casts. The
// hardware saturates to the destination range and truncates, as Dolphin does.
static inline void OSInitFastCast(void) {}
static inline u16 __OSf32tou16(f32 in) {
    return (u16)(in <= 0.0f ? 0.0f : in >= 65535.0f ? 65535.0f : in);
}
static inline s16 __OSf32tos16(f32 in) {
    return (s16)(in <= -32768.0f ? -32768.0f : in >= 32767.0f ? 32767.0f : in);
}
static inline u8 __OSf32tou8(f32 in) {
    return (u8)(in <= 0.0f ? 0.0f : in >= 255.0f ? 255.0f : in);
}
static inline s8 __OSf32tos8(f32 in) {
    return (s8)(in <= -128.0f ? -128.0f : in >= 127.0f ? 127.0f : in);
}
static inline void OSf32tou16(const f32* in, volatile u16* out) { *out = __OSf32tou16(*in); }
static inline void OSf32tos16(const f32* in, volatile s16* out) { *out = __OSf32tos16(*in); }
static inline void OSf32tou8(const f32* in, volatile u8* out) { *out = __OSf32tou8(*in); }
static inline void OSf32tos8(const f32* in, volatile s8* out) { *out = __OSf32tos8(*in); }
static inline void OSu16tof32(const volatile u16* in, f32* out) { *out = (f32)*in; }
static inline void OSs16tof32(const volatile s16* in, f32* out) { *out = (f32)*in; }
static inline void OSu8tof32(const volatile u8* in, f32* out) { *out = (f32)*in; }
static inline void OSs8tof32(const volatile s8* in, f32* out) { *out = (f32)*in; }
#else
#ifdef __MWERKS__
static inline u16 __OSf32tou16(register f32 in) {
    f32 a;
    register f32* ptr = &a;
    register u16 r;
    asm {
        psq_st in, 0(ptr), 1, 3
        lhz r, 0(ptr)
    }
    return r;
}

static inline void OSf32tou16(register f32* in, volatile register u16* out) {
    *out = __OSf32tou16(*in);
}
#else
#define OSf32tou16(in, out) asm volatile("psq_st   %1, 0(%0), 1, 3 " : : "b"(out), "f"(*(in)) : "memory")
#endif
#define OSu16tof32(in, out) asm volatile("psq_l   %0, 0(%1), 1, 3  " : "=f"(*(out)) : "b"(in))

static inline void OSInitFastCast(void) {
#ifdef __MWERKS__
    asm
    {
        li      r3, 4
        oris    r3, r3, 4
        mtspr   0x392, r3

        li      r3, 5
        oris    r3, r3, 5
        mtspr   0x393, r3

        li      r3, 6
        oris    r3, r3, 6
        mtspr   0x394, r3

        li      r3, 7
        oris    r3, r3, 7
        mtspr   0x395, r3
    }
#endif
}
#endif  // TARGET_PC

#ifdef __cplusplus
}
#endif

#endif  // OSFASTCAST_H
