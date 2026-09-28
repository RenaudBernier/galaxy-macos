#pragma once

namespace nw4r {
#ifdef TARGET_PC
    typedef uintptr_t IntPtr;
    typedef intptr_t PtrDiff;
#else
    typedef unsigned long IntPtr;
    typedef signed long PtrDiff;
#endif
};
