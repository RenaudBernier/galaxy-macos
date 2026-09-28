#if defined(TARGET_PC) && defined(PORT_AURORA)
#include <revolution/gx.h>
#else  // TARGET_PC && PORT_AURORA
#ifndef GXDISPLIST_H
#define GXDISPLIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/types.h"

void GXBeginDisplayList(void *, u32);
u32 GXEndDisplayList(void);
void GXCallDisplayList(const void *, u32);

#ifdef __cplusplus
}
#endif

#endif // GXDISPLIST_H
#endif  // TARGET_PC && PORT_AURORA
