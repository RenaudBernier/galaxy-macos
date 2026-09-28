#if defined(TARGET_PC) && defined(PORT_AURORA)
#include <revolution/gx.h>
#else  // TARGET_PC && PORT_AURORA
#ifndef GXCULL_H
#define GXCULL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "revolution/types.h"
#include "revolution/gx/GXEnum.h"

void GXSetCullMode(GXCullMode);
void GXSetCoPlanar(GXBool);

#ifdef __cplusplus
}
#endif

#endif // GXCULL_H
#endif  // TARGET_PC && PORT_AURORA
