// Game code includes MSL's <functional.hpp>; route it to the MSL header.
// Its binders/adaptors were removed from host C++17 libraries, so they don't clash.
#pragma once
#include <functional>
#include "../../libs/MSL_C++/include/functional.hpp"
