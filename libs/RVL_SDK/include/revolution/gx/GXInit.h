#if defined(TARGET_PC) && defined(PORT_AURORA)
#include <revolution/gx.h>
#else  // TARGET_PC && PORT_AURORA
#ifndef GXINIT_H
#define GXINIT_H

#ifdef __cplusplus
extern "C" {
#endif

GXFifoObj* GXInit(void*, u32);
void __GXInitGX(void);

#ifdef __cplusplus
}
#endif

#endif // GXINIT_H
#endif  // TARGET_PC && PORT_AURORA
