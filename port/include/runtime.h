// Metrowerks runtime helpers used directly by game code.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint64_t __cvt_dbl_usll(double d) { return (uint64_t)d; }
static inline uint32_t __cvt_fp2unsigned(double d) { return (uint32_t)d; }

#ifdef __cplusplus
}
#endif
