#pragma once

#include <revolution/gx.h>

class JPAEmitterWorkData;

struct JPAExTexShapeData {
    // Common header.
    /* 0x00 */ u8 mMagic[4];
    /* 0x04 */ BE(u32) mSize;

    /* 0x08 */ BE(u32) mFlags;
    /* 0x0C */ BE(f32) mIndTexMtx[2][3];
    /* 0x24 */ s8 mExpScale;
    /* 0x25 */ s8 mIndTexIdx;
    /* 0x26 */ s8 mSecTexIdx;
};  // Size: 0x28

class JPAExTexShape {
public:
    JPAExTexShape(u8 const*);

    // On the host this points at big-endian floats in the resource.
    const BE(f32)* getIndTexMtx() const {
        return &mpData->mIndTexMtx[0][0];
    }
    s8 getExpScale() const {
        return mpData->mExpScale;
    }
    u8 getIndTexIdx() const {
        return mpData->mIndTexIdx;
    }
    u8 getSecTexIdx() const {
        return mpData->mSecTexIdx;
    }
    bool isUseIndirect() const {
        return !!(mpData->mFlags & 0x01);
    }
    BOOL isUseSecTex() const {
        return (mpData->mFlags & 0x0100);
    }

public:
    const JPAExTexShapeData* mpData;
};

void JPALoadExTex(JPAEmitterWorkData*);
