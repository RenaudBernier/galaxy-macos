// Wii (RVL) GD helpers not provided by Aurora's GD headers.
#pragma once

#include <dolphin/gd.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void GDBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    GDWrite_u8((u8)(vtxfmt | type));
    GDWrite_u16(nverts);
}
static inline void GDEnd(void) {}

static inline void GDPosition3f32(f32 x, f32 y, f32 z) {
    GDWrite_f32(x);
    GDWrite_f32(y);
    GDWrite_f32(z);
}
static inline void GDTexCoord2f32(f32 s, f32 t) {
    GDWrite_f32(s);
    GDWrite_f32(t);
}
static inline void GDColor4u8(u8 r, u8 g, u8 b, u8 a) {
    GDWrite_u8(r);
    GDWrite_u8(g);
    GDWrite_u8(b);
    GDWrite_u8(a);
}

#ifdef __cplusplus
}
#endif
