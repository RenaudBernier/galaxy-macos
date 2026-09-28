// MWCC's __memcpy intrinsic. types.h declares it both inside and outside
// extern "C" blocks depending on the includer, so both linkages are provided
// (the C one lives in stubs.cpp). This file must not include types.h.
#include <cstring>

void* __memcpy(void* dst, const void* src, int n) {
    return std::memcpy(dst, src, static_cast<size_t>(n));
}
