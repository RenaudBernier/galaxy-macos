// Wii (RVL) additions to the GX API that Aurora's GameCube-oriented headers
// don't declare, plus call-compatibility shims. Implemented in port/src/gx_rvl.cpp.
#pragma once

#include <dolphin/gx.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXGetTexObjAll(const GXTexObj* obj, void** image_ptr, u16* width, u16* height, GXTexFmt* format,
                    GXTexWrapMode* wrap_s, GXTexWrapMode* wrap_t, GXBool* mipmap);
void GXGetTexObjLODAll(const GXTexObj* obj, GXTexFilter* min_filt, GXTexFilter* mag_filt, f32* min_lod, f32* max_lod,
                       f32* lod_bias, GXBool* bias_clamp, GXBool* do_edge_lod, GXAnisotropy* max_aniso);
void GXSetZScaleOffset(f32 scale, f32 offset);
void GXSetDrawSync(u16 token);
u16 GXReadDrawSync(void);
void GXEnableBreakPt(void* breakPt);
void GXDisableBreakPt(void);
void GXLoadTexMtxIndx(u16 mtx_indx, u32 id, GXTexMtxType type);
// The EFB -> XFB copy ends a rendered frame; the port presents the Aurora
// frame there (port/src/vi.cpp).
void PortGXCopyDisp(void* dest, GXBool clear);
void GXInitSpecularDirHA(GXLightObj* lt_obj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz);

static inline void GXCmd1f32(f32 x) {
    union {
        f32 f;
        u32 u;
    } v;
    v.f = x;
    GXCmd1u32(v.u);
}

#ifdef __cplusplus
}

// RVL signatures that differ from Aurora's GameCube-style declarations. These
// are C++ overloads; the header may be included from inside extern "C" blocks.
extern "C++" {
inline GXBool GXGetCPUFifo(GXFifoObj* fifo) {
    *fifo = *GXGetCPUFifo();
    return GX_TRUE;
}
inline void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf, const u8 vfilter[7]) {
    GXSetCopyFilter(aa, const_cast< u8(*)[2] >(sample_pattern), vf, const_cast< u8* >(vfilter));
}
}  // extern "C++"
#endif

// GXSetArray: the Wii API is GXSetArray(attr, data, stride). Aurora needs the
// array's byte size and endianness as well. Game and loader code keeps vertex
// arrays in host byte order (file data is swapped at load time), and passes
// size 0 meaning "unknown": Aurora then uploads as much of the array as each
// draw's indices reference (port/patches/aurora/0001-*.patch).
// The 5-argument form still reaches Aurora's function directly; a macro is not
// re-expanded inside its own expansion.
#define PORT_GXSETARRAY_3(attr, data, stride) GXSetArray((attr), (data), 0, (stride), true)
#define PORT_GXSETARRAY_5(attr, data, size, stride, le) GXSetArray((attr), (data), (size), (stride), (le))
#define PORT_GXSETARRAY_PICK(_1, _2, _3, _4, _5, NAME, ...) NAME
#define GXSetArray(...) PORT_GXSETARRAY_PICK(__VA_ARGS__, PORT_GXSETARRAY_5, PORT_GXSETARRAY_4_INVALID, PORT_GXSETARRAY_3)(__VA_ARGS__)

#ifdef __cplusplus
// The write-gather pipe: on the Wii, stores to GXWGFifo go straight to the GPU
// FIFO at 0xCC008000. On the host, each member assignment appends to Aurora's
// command stream instead.
extern "C++" {
struct PortWGPipe {
    struct U8 {
        void operator=(::u8 v) const volatile { GXCmd1u8(v); }
    } u8;
    struct S8 {
        void operator=(::s8 v) const volatile { GXCmd1u8((::u8)v); }
    } s8;
    struct U16 {
        void operator=(::u16 v) const volatile { GXCmd1u16(v); }
    } u16;
    struct S16 {
        void operator=(::s16 v) const volatile { GXCmd1u16((::u16)v); }
    } s16;
    struct U32 {
        void operator=(::u32 v) const volatile { GXCmd1u32(v); }
    } u32;
    struct S32 {
        void operator=(::s32 v) const volatile { GXCmd1u32((::u32)v); }
    } s32;
    struct F32 {
        void operator=(::f32 v) const volatile { GXCmd1f32(v); }
    } f32;
};
extern volatile PortWGPipe PortGXFifo;
}  // extern "C++"
#undef GXWGFifo
#define GXWGFifo PortGXFifo
#endif

#define GXCopyDisp(dest, clear) PortGXCopyDisp((dest), (clear))
