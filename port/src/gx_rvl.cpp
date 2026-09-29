// Wii GX functions that Aurora (a GameCube-oriented GX layer) doesn't
// provide, and GPU features that have no host equivalent.

#include "os/scheduler.hpp"
#include "port/log.hpp"
#include "port/savestate.hpp"

#include <revolution/gx.h>
#include <revolution/mtx.h>

#include <cstring>

volatile PortWGPipe PortGXFifo;

namespace {

PORT_SAVED GXDrawSyncCallback sDrawSyncCallback = nullptr;
PORT_SAVED u16 sLastDrawSyncToken = 0;

// From the RVL SDK's GXFrameBuf.c (src/RVL_SDK/gx).
u32 numXfbLines(u32 efbHt, u32 iScale) {
    u32 count = (efbHt - 1) * 256;
    u32 realHt = count / iScale + 1;
    u32 iScaleD = iScale;
    if (iScaleD > 0x80 && iScaleD < 0x100) {
        while ((iScaleD & 0x01) == 0) {
            iScaleD >>= 1;
        }
        if ((efbHt % iScaleD) == 0) {
            ++realHt;
        }
    }
    if (realHt > 1024) {
        realHt = 1024;
    }
    return realHt;
}

}  // namespace

extern "C" {

u16 GXGetNumXfbLines(u16 efbHeight, f32 yScale) {
    const u32 iScale = (u32)(256.0f / yScale) & 0x1ff;
    return (u16)numXfbLines(efbHeight, iScale);
}

f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) {
    u32 tgtHt = xfbHeight;
    f32 yScale = (f32)xfbHeight / (f32)efbHeight;
    u32 iScale = (u32)(256.0f / yScale) & 0x1ff;
    u32 realHt = numXfbLines(efbHeight, iScale);
    while (realHt > xfbHeight) {
        tgtHt--;
        yScale = (f32)tgtHt / (f32)efbHeight;
        iScale = (u32)(256.0f / yScale) & 0x1ff;
        realHt = numXfbLines(efbHeight, iScale);
    }
    f32 fScale = yScale;
    while (realHt < xfbHeight) {
        fScale = yScale;
        tgtHt++;
        yScale = (f32)tgtHt / (f32)efbHeight;
        iScale = (u32)(256.0f / yScale) & 0x1ff;
        realHt = numXfbLines(efbHeight, iScale);
    }
    return fScale;
}

// Draw sync tokens: the host GPU consumes the command stream at frame end, so
// a token is reported as reached as soon as it is issued (delivered as an
// interrupt, like the PE token interrupt).
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb) {
    const BOOL level = OSDisableInterrupts();
    GXDrawSyncCallback prev = sDrawSyncCallback;
    sDrawSyncCallback = cb;
    OSRestoreInterrupts(level);
    return prev;
}

void GXSetDrawSync(u16 token) {
    sLastDrawSyncToken = token;
    if (sDrawSyncCallback != nullptr) {
        port::os::postInterrupt([token] {
            if (sDrawSyncCallback != nullptr) {
                sDrawSyncCallback(token);
            }
        });
    }
}

u16 GXReadDrawSync(void) { return sLastDrawSyncToken; }

void GXLoadTexMtxIndx(u16 mtx_indx, u32 id, GXTexMtxType type) {
    // CP LOAD_INDX_C: texture matrices live at XF address id * 4.
    const u32 count = type == GX_MTX3x4 ? 12 : 8;
    GXCmd1u8(0x30);
    GXCmd1u16(mtx_indx);
    GXCmd1u16((u16)(((count - 1) << 12) | (id * 4)));
}

void GXEnableBreakPt(void*) {}
void GXDisableBreakPt(void) {}
void GXAbortFrame(void) {}
void GXInitTexCacheRegion(GXTexRegion*, GXBool, u32, GXTexCacheSize, u32, GXTexCacheSize) {}
void GXPokeAlphaRead(GXAlphaReadMode) {}
void GXSetMisc(GXMiscToken, u32) {}
void GXSetCopyClamp(GXFBClamp) {}
void GXSetZScaleOffset(f32, f32) {}
void GXSetScissorBoxOffset(s32, s32) {}

void GXReadXfRasMetric(u32* xfWaitIn, u32* xfWaitOut, u32* rasBusy, u32* clocks) {
    *xfWaitIn = *xfWaitOut = *rasBusy = *clocks = 0;
}

void GXPeekARGB(u16, u16, u32* color) {
    // PORT: EFB readback is not supported yet.
    *color = 0;
}

void C_MTX44Identity(Mtx44 m) {
    std::memset(m, 0, sizeof(Mtx44));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

void C_MTX44Copy(const Mtx44 src, Mtx44 dst) {
    if (src != dst) {
        std::memcpy(dst, src, sizeof(Mtx44));
    }
}

}  // extern "C"
