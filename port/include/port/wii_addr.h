// Emulation of the Wii's 32-bit address space on 64-bit hosts.
//
// Game code freely converts pointers to u32 (address arithmetic, pointers kept
// in u32 fields, `addr >= 0x80000000` checks that tell pointers from offsets or
// tokens). On the host, the game's memory (heaps, thread stacks and the
// executable's static data) lives inside a single window of at most 2GB that
// starts at `gPortWiiBase`; that window is presented to game code as the Wii
// virtual range 0x80000000-0xFFFFFFFF.
//
//   PTR_TO_U32(p)     host pointer -> Wii address
//   U32_TO_PTR(T, a)  Wii address  -> host pointer of type T
//
// Values below 0x80000000 are passed through unchanged in both directions: they
// are null, offsets or integers smuggled through pointer types (e.g. OSMessage
// tokens). Real host pointers are never below 4GB on 64-bit macOS (__PAGEZERO),
// so the two cases cannot be confused. Dereferencing an untranslated Wii address
// faults inside __PAGEZERO, which makes missed conversions easy to find.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t gPortWiiBase;
void PortWiiAddrFault(uintptr_t hostAddr);

static inline uint32_t PortPtrToU32(const volatile void* p) {
    uintptr_t v = (uintptr_t)p;
    if (v < 0x100000000ull) {
        return (uint32_t)v;
    }
    uintptr_t off = v - gPortWiiBase;
    if (__builtin_expect(off >= 0x80000000ull, 0)) {
        PortWiiAddrFault(v);
    }
    return 0x80000000u + (uint32_t)off;
}

static inline void* PortU32ToPtr(uint32_t a) {
    if (a < 0x80000000u) {
        return (void*)(uintptr_t)a;
    }
    return (void*)(gPortWiiBase + (a - 0x80000000u));
}

#ifdef __cplusplus
}
#endif

#define PTR_TO_U32(p) PortPtrToU32((const volatile void*)(p))
#define U32_TO_PTR(T, a) ((T)PortU32ToPtr((uint32_t)(a)))

#ifdef __cplusplus
extern "C++" {
// A pointer stored as a 32-bit Wii address, for pointer fields inside
// structures that are overlaid on file data (their layout must stay 4 bytes).
// Values are written at runtime, so they're kept in host byte order.
template <typename T>
struct Ptr32 {
    uint32_t addr;

    T* get() const { return static_cast<T*>(PortU32ToPtr(addr)); }
    void set(const T* p) { addr = PortPtrToU32(p); }

    operator T*() const { return get(); }
    Ptr32& operator=(T* p) {
        set(p);
        return *this;
    }
    T* operator->() const { return get(); }
    explicit operator bool() const { return addr != 0; }
};

template <>
struct Ptr32<void> {
    uint32_t addr;

    void* get() const { return PortU32ToPtr(addr); }
    void set(const void* p) { addr = PortPtrToU32(p); }

    operator void*() const { return get(); }
    Ptr32& operator=(void* p) {
        set(p);
        return *this;
    }
    explicit operator bool() const { return addr != 0; }
};

}  // extern "C++"

#define PTR32(T) Ptr32< T >
#endif
