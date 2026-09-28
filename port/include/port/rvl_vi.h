// Wii (RVL) VI additions not declared by Aurora. Implemented in port/src/vi.cpp.
#pragma once

#include <dolphin/vi.h>

// RVL-only video modes and limits (values from the RVL SDK headers).
#ifndef VI_TVMODE_EURGB60_PROG
#define VI_TVMODE_EURGB60_PROG ((VITVMode)VI_TVMODE(VI_EURGB60, VI_PROGRESSIVE))
#endif
#define VI_MAX_WIDTH_NTSC 720
#define VI_MAX_HEIGHT_NTSC 480
#define VI_MAX_WIDTH_PAL 720
#define VI_MAX_HEIGHT_PAL 574
#define VI_MAX_WIDTH_MPAL 720
#define VI_MAX_HEIGHT_MPAL 480
#define VI_MAX_WIDTH_EURGB60 VI_MAX_WIDTH_NTSC
#define VI_MAX_HEIGHT_EURGB60 VI_MAX_HEIGHT_NTSC

#ifdef __cplusplus
extern "C" {
#endif

#include <revolution/vi/vi3in1types.h>

BOOL VIEnableDimming(BOOL enable);
BOOL VIResetDimmingCount(void);
u32 VIGetDimmingCount(void);
u32 VIGetCurrentLine(void);
u32 VIGetScanMode(void);
void VISetTrapFilter(VIBool filter);

#ifdef __cplusplus
}
#endif
