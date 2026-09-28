// MSL C++ library behaviour the game depends on, made available alongside the
// host C++ library.
#pragma once

#include <algorithm>
#include <functional>
#include <iterator>
#include <functional.hpp>

// MSL's <algorithm> is compiled into namespace msl so it can coexist with the
// host's. Its sort() differs from libc++ (and keeps static state across calls),
// so call sites that sort data with possible ties use msl::sort explicitly.
#define std msl
#include "../../../libs/MSL_C++/include/algorithm"
#undef std

namespace std {
    // MSL-only extensions.
    using msl::find_if_array;
    using msl::for_each_array;
    using msl::rfind_if;
}  // namespace std
