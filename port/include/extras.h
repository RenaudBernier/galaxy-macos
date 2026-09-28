// MSL <extras.h> shim for host builds.
#pragma once
#include <string.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
static inline int strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }

#ifdef __cplusplus
}
#endif
