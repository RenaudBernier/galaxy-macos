#pragma once

#include "JSystem/JSupport/JSUIosBase.hpp"

class JSUInputStream : public JSUIosBase {
public:
    JSUInputStream() : JSUIosBase() {
    }

    virtual ~JSUInputStream() ATTRIBUTE_WEAK;
    virtual s32 getAvailable() const = 0;
    virtual s32 skip(s32);
    virtual u32 readData(void*, s32) = 0;

    s32 read(void*, s32);

    inline u8 readU8() {
        u8 ret;
        read(&ret, sizeof(u8));
        return ret;
    }

    // Streams read big-endian file data.
    inline u16 readU16() {
        u16 ret;
        read(&ret, sizeof(u16));
#ifdef TARGET_PC
        ret = __builtin_bswap16(ret);
#endif
        return ret;
    }

    inline u32 readU32() {
        u32 ret;
        read(&ret, sizeof(u32));
#ifdef TARGET_PC
        ret = __builtin_bswap32(ret);
#endif
        return ret;
    }

    // TODO: probably a lot of other helpers for different types
};
