// TPL texture palettes (used by nw4r layouts).
//
// On the Wii, TPLBind() relocates the file's offsets into pointers in place.
// The SDK structs contain pointers, so on the host their layout no longer
// matches the (32-bit, big-endian) file. Instead, TPLBind parses the file with
// explicit big-endian reads and builds host-layout descriptors in memory that
// lives inside the emulated Wii address window (static storage), then stores
// the descriptor array pointer in the palette's descriptorArray field so the
// palette looks bound to game code. Texture and CLUT data stay in the file,
// in the big-endian layout the GX backend expects.
//
// File layout (big-endian):
//   palette:    u32 version, u32 numDescriptors, u32 descriptorArrayOffset
//   descriptor: u32 textureHeaderOffset, u32 clutHeaderOffset (0 = none)
//   texture:    u16 height, u16 width, u32 format, u32 dataOffset, u32 wrapS,
//               u32 wrapT, u32 minFilter, u32 magFilter, f32 LODBias,
//               u8 edgeLODEnable, u8 minLOD, u8 maxLOD, u8 unpacked
//   clut:       u16 numEntries, u8 unpacked, u8 pad, u32 format, u32 dataOffset
// All offsets are relative to the start of the palette.

#include "port/log.hpp"

#include <revolution/tpl.h>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>

namespace {

inline u16 be16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }
inline u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }
inline f32 beF32(const u8* p) {
    u32 v = be32(p);
    f32 f;
    memcpy(&f, &v, sizeof(f));
    return f;
}

// Descriptor storage inside the executable image, i.e. inside the Wii address
// window: game code converts these pointers with PTR_TO_U32.
alignas(32) u8 sPool[512 * 1024];
size_t sPoolUsed = 0;

void* poolAlloc(size_t size) {
    size = (size + 31) & ~size_t(31);
    if (sPoolUsed + size > sizeof(sPool)) {
        PORT_ERROR("tpl", "descriptor pool exhausted");
        abort();
    }
    void* p = sPool + sPoolUsed;
    sPoolUsed += size;
    return p;
}

struct Bound {
    TPLDescriptor* descriptors;
    u32 count;
};

std::mutex sMutex;
std::unordered_map<const TPLPalette*, Bound> sBound;

bool isBound(TPLPalette* pal, Bound** out) {
    auto it = sBound.find(pal);
    // The same memory may since have been reused for another palette; ours is
    // still bound only if our pointer is still in place.
    if (it == sBound.end() || pal->descriptorArray != it->second.descriptors) {
        return false;
    }
    *out = &it->second;
    return true;
}

Bound bind(TPLPalette* pal) {
    u8* file = reinterpret_cast<u8*>(pal);
    const u32 version = be32(file + 0);
    const u32 count = be32(file + 4);
    const u32 descOffset = be32(file + 8);

    auto* descs = static_cast<TPLDescriptor*>(poolAlloc(sizeof(TPLDescriptor) * count));
    for (u32 i = 0; i < count; i++) {
        const u8* d = file + descOffset + i * 8;
        const u32 texOffset = be32(d);
        const u32 clutOffset = be32(d + 4);

        auto* tex = new (poolAlloc(sizeof(TPLHeader))) TPLHeader{};
        const u8* t = file + texOffset;
        tex->height = be16(t + 0);
        tex->width = be16(t + 2);
        tex->format = be32(t + 4);
        tex->data = reinterpret_cast<char*>(file + be32(t + 8));
        tex->wrapS = static_cast<GXTexWrapMode>(be32(t + 12));
        tex->wrapT = static_cast<GXTexWrapMode>(be32(t + 16));
        tex->minFilter = static_cast<GXTexFilter>(be32(t + 20));
        tex->magFilter = static_cast<GXTexFilter>(be32(t + 24));
        tex->LODBias = beF32(t + 28);
        tex->edgeLODEnable = t[32];
        tex->minLOD = t[33];
        tex->maxLOD = t[34];
        tex->unpacked = 1;
        descs[i].textureHeader = tex;

        descs[i].CLUTHeader = nullptr;
        if (clutOffset != 0) {
            auto* clut = new (poolAlloc(sizeof(TPLClutHeader))) TPLClutHeader{};
            const u8* c = file + clutOffset;
            clut->numEntries = be16(c + 0);
            clut->unpacked = 1;
            clut->format = static_cast<GXTlutFmt>(be32(c + 4));
            clut->data = reinterpret_cast<char*>(file + be32(c + 8));
            descs[i].CLUTHeader = clut;
        }
    }

    // PORT: file-reloc. Rewrite the palette header in host layout; bytes 8..15
    // of the file now hold the host pointer (the file offsets were consumed).
    pal->versionNumber = version;
    pal->numDescriptors = count;
    pal->descriptorArray = descs;
    return Bound{descs, count};
}

}  // namespace

extern "C" {

void TPLBind(TPLPalettePtr pal) {
    std::lock_guard lock(sMutex);
    Bound* b;
    if (isBound(pal, &b)) {
        return;
    }
    sBound[pal] = bind(pal);
}

TPLDescriptorPtr TPLGet(TPLPalettePtr pal, u32 id) {
    std::lock_guard lock(sMutex);
    Bound* b;
    if (!isBound(pal, &b)) {
        sBound[pal] = bind(pal);
        b = &sBound[pal];
    }
    if (b->count == 0) {
        return nullptr;
    }
    return &b->descriptors[id % b->count];
}

// Lets game code on the host ask whether a palette is bound: the Wii check
// (descriptorArray < 0x80000000) reads the file's 32-bit offset through the
// host struct layout.
BOOL PortTPLIsBound(TPLPalettePtr pal) {
    std::lock_guard lock(sMutex);
    Bound* b;
    return isBound(pal, &b) ? TRUE : FALSE;
}

}  // extern "C"
