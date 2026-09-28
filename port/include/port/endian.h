// Big-endian data on a little-endian host.
//
// Everything the game reads from the disc is big-endian. Two tools:
//
//   BE(T)            A field type for structs overlaid on file data: same size
//                    and alignment as T, stores big-endian bytes, converts on
//                    every read/write. On the Wii build BE(T) is just T.
//
//   PortReadBE32(p)  Explicit reads/writes of big-endian values at (possibly
//   PortWriteBE32..  unaligned) addresses, for code that walks raw bytes.
//
// Prefer BE(T) fields for file structures so that loader code stays
// unchanged; code that takes the address of such a field as a plain T* will
// fail to compile, which flags the spots that need attention.
#pragma once

#include <stdint.h>
#include <string.h>

static inline uint16_t PortReadBE16(const void* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}
static inline uint32_t PortReadBE32(const void* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}
static inline uint64_t PortReadBE64(const void* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}
static inline float PortReadBEF32(const void* p) {
    uint32_t u = PortReadBE32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static inline void PortWriteBE16(void* p, uint16_t v) {
    v = __builtin_bswap16(v);
    memcpy(p, &v, 2);
}
static inline void PortWriteBE32(void* p, uint32_t v) {
    v = __builtin_bswap32(v);
    memcpy(p, &v, 4);
}
static inline void PortWriteBEF32(void* p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    PortWriteBE32(p, u);
}

// In-place conversion of arrays (e.g. vertex data swapped once at load time).
static inline void PortSwap16Array(void* p, size_t count) {
    uint16_t* a = (uint16_t*)p;
    for (size_t i = 0; i < count; i++) {
        a[i] = __builtin_bswap16(a[i]);
    }
}
static inline void PortSwap32Array(void* p, size_t count) {
    uint32_t* a = (uint32_t*)p;
    for (size_t i = 0; i < count; i++) {
        a[i] = __builtin_bswap32(a[i]);
    }
}

#ifdef __cplusplus

// This header may be reached from inside extern "C" blocks.
extern "C++" {
#include <type_traits>

template <typename T>
struct BE {
    static_assert(std::is_trivially_copyable<T>::value, "BE<T> needs a trivially copyable T");
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "unsupported size");

    T raw;

    static T swap(T v) {
        if constexpr (sizeof(T) == 1) {
            return v;
        } else if constexpr (sizeof(T) == 2) {
            uint16_t u;
            memcpy(&u, &v, 2);
            u = __builtin_bswap16(u);
            memcpy(&v, &u, 2);
            return v;
        } else if constexpr (sizeof(T) == 4) {
            uint32_t u;
            memcpy(&u, &v, 4);
            u = __builtin_bswap32(u);
            memcpy(&v, &u, 4);
            return v;
        } else {
            uint64_t u;
            memcpy(&u, &v, 8);
            u = __builtin_bswap64(u);
            memcpy(&v, &u, 8);
            return v;
        }
    }

    T get() const { return swap(raw); }
    void set(T v) { raw = swap(v); }

    operator T() const { return get(); }
    BE& operator=(T v) {
        set(v);
        return *this;
    }

    BE& operator+=(T v) { return *this = get() + v; }
    BE& operator-=(T v) { return *this = get() - v; }
    BE& operator*=(T v) { return *this = get() * v; }
    BE& operator/=(T v) { return *this = get() / v; }
    BE& operator|=(T v) { return *this = get() | v; }
    BE& operator&=(T v) { return *this = get() & v; }
    BE& operator^=(T v) { return *this = get() ^ v; }
    BE& operator<<=(int n) { return *this = get() << n; }
    BE& operator>>=(int n) { return *this = get() >> n; }
    BE& operator++() { return *this = get() + 1; }
    BE& operator--() { return *this = get() - 1; }
    T operator++(int) {
        T old = get();
        *this = old + 1;
        return old;
    }
    T operator--(int) {
        T old = get();
        *this = old - 1;
        return old;
    }
};

}  // extern "C++"

#define BE(T) BE< T >

#endif  // __cplusplus
