#pragma once

#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JSupport/JSUMemoryOutputStream.hpp>

// Multi-byte values in save data records are big-endian (the Wii layout).
inline u16 BinaryDataReadU16(const void* p) {
#ifdef TARGET_PC
    return PortReadBE16(p);
#else
    return *static_cast< const u16* >(p);
#endif
}

inline void BinaryDataWriteU16(void* p, u16 value) {
#ifdef TARGET_PC
    PortWriteBE16(p, value);
#else
    *static_cast< u16* >(p) = value;
#endif
}

class BinaryDataContentHeaderSerializer {
public:
    BinaryDataContentHeaderSerializer(u8*, u32);

    void addAttribute(const char*, u32);
    void flush();
    u32 getHeaderSize() const;
    u32 getDataSize() const;

    void* getBuffer() const {
        return mStream.mBuffer;
    }

    /* 0x00 */ JSUMemoryOutputStream mStream;
    /* 0x14 */ u32 mAttributeNum;
    /* 0x18 */ u32 mDataSize;
};

class BinaryDataContentAccessor {
public:
    BinaryDataContentAccessor(u8* pData);

    s32 getHeaderSize() const;
    s32 getDataSize() const;
    s32 getAttributeNum() const;  // stripped
    void* getPointer(const char* pAttributeName, u8* pData) const;

    /* 0x00 */ u8* mData;
};
