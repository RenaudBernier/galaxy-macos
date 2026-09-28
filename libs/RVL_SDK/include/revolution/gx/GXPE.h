#if defined(TARGET_PC) && defined(PORT_AURORA)
#include <revolution/gx.h>
#else  // TARGET_PC && PORT_AURORA
#ifndef GXPE_H
#define GXPE_H

#ifdef __cplusplus
extern "C" {
#endif

void __GXPEInit();

#ifdef __cplusplus
}
#endif

#endif // GXPE_H
#endif  // TARGET_PC && PORT_AURORA
