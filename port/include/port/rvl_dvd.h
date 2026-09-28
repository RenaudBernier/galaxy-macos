// Wii (RVL) DVD calls not declared by Aurora. Implemented in port/src/dvd_rvl.cpp.
#pragma once

#include <dolphin/dvd.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCBCallback callback);

#ifdef __cplusplus
}
#endif
