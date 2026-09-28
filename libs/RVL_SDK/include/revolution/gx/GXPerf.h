#if defined(TARGET_PC) && defined(PORT_AURORA)
#include <revolution/gx.h>
#else  // TARGET_PC && PORT_AURORA
#ifndef GXPERF_H
#define GXPERF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/types.h"
#include "revolution/gx/GXEnum.h"

void GXSetGPMetric(GXPerf0, GXPerf1);
void GXClearGPMetric(void);
void GXReadXfRasMetric(u32*, u32*, u32*, u32*);

#ifdef __cplusplus
}
#endif

#endif // GXPERF_H
#endif  // TARGET_PC && PORT_AURORA
