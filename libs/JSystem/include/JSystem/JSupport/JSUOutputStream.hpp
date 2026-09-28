#pragma once

#include "JSystem/JSupport/JSUIosBase.hpp"

class JSUOutputStream : public JSUIosBase {
public:
    JSUOutputStream() {
    }

    virtual ~JSUOutputStream() ATTRIBUTE_WEAK;
    virtual s32 skip(s32, s8);
    virtual s32 writeData(const void*, s32) = 0;

    s32 write(const void*, s32);

    inline void writeU8(u8 val) {
        write(&val, sizeof(u8));
    }

    // Streams write big-endian data (matching JSUInputStream).
    inline void writeU16(u16 val) {
#ifdef TARGET_PC
        val = __builtin_bswap16(val);
#endif
        write(&val, sizeof(u16));
    }

    inline void writeU32(u32 val) {
#ifdef TARGET_PC
        val = __builtin_bswap32(val);
#endif
        write(&val, sizeof(u32));
    }

    // TODO: probably a lot of other helpers for different types
};
