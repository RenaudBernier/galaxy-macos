#pragma once

#include "JSystem/J3DGraphLoader/J3DModelLoader.hpp"

class J3DShape;
class J3DShapeMtx;
class J3DShapeDraw;
struct ResNTAB;

// SHP1 entries, read in place from (big-endian) model data.
struct J3DShapeInitData {
    /* 0x00 */ u8 mShapeMtxType;
    /* 0x02 */ BE(u16) mMtxGroupNum;
    /* 0x04 */ BE(u16) mVtxDescListIndex;
    /* 0x06 */ BE(u16) mMtxInitDataIndex;
    /* 0x08 */ BE(u16) mDrawInitDataIndex;
#ifdef TARGET_PC
    /* 0x0C */ BE(f32) mRadius;
    /* 0x10 */ BE(f32) mMin[3];
    /* 0x1C */ BE(f32) mMax[3];
#else
    /* 0x0C */ f32 mRadius;
    /* 0x10 */ Vec mMin;
    /* 0x1C */ Vec mMax;
#endif
};

struct J3DShapeMtxInitData {
    /* 0x00 */ BE(u16) mUseMtxIndex;
    /* 0x02 */ BE(u16) mUseMtxCount;
    /* 0x04 */ BE(u32) mFirstUseMtxIndex;
};

struct J3DShapeDrawInitData {
    /* 0x00 */ BE(u32) mDisplayListSize;
    /* 0x04 */ BE(u32) mDisplayListIndex;
};

struct J3DShapeFactory {
    J3DShapeFactory(J3DShapeBlock const&);
    J3DShape* create(int, u32, GXVtxDescList*);
    J3DShapeMtx* newShapeMtx(u32, int, int) const;
    J3DShapeDraw* newShapeDraw(int, int) const;
    void allocVcdVatCmdBuffer(u32);
    s32 calcSize(int, u32);
    s32 calcSizeVcdVatCmdBuffer(u32);
    s32 calcSizeShapeMtx(u32, int, int) const;

    /* 0x00 */ J3DShapeInitData* mShapeInitData;
    /* 0x04 */ BE(u16)* mIndexTable;
    /* 0x08 */ GXVtxDescList* mVtxDescList;  // converted to host order by the loader
    /* 0x0C */ u16* mMtxTable;               // converted to host order by the loader
    /* 0x10 */ u8* mDisplayListData;
    /* 0x14 */ J3DShapeMtxInitData* mMtxInitData;
    /* 0x18 */ J3DShapeDrawInitData* mDrawInitData;
    /* 0x1C */ u8* mVcdVatCmdBuffer;

    u32 getMtxGroupNum(int no) const {
        return mShapeInitData[mIndexTable[no]].mMtxGroupNum;
    }
    GXVtxDescList* getVtxDescList(int no) const {
        return (GXVtxDescList*)((u8*)mVtxDescList + mShapeInitData[mIndexTable[no]].mVtxDescListIndex);
    }
    f32 getRadius(int no) const {
        return mShapeInitData[mIndexTable[no]].mRadius;
    }
#ifdef TARGET_PC
    Vec getMin(int no) const {
        const J3DShapeInitData& d = mShapeInitData[mIndexTable[no]];
        Vec v = {d.mMin[0], d.mMin[1], d.mMin[2]};
        return v;
    }
    Vec getMax(int no) const {
        const J3DShapeInitData& d = mShapeInitData[mIndexTable[no]];
        Vec v = {d.mMax[0], d.mMax[1], d.mMax[2]};
        return v;
    }
#else
    Vec& getMin(int no) const {
        return mShapeInitData[mIndexTable[no]].mMin;
    }
    Vec& getMax(int no) const {
        return mShapeInitData[mIndexTable[no]].mMax;
    }
#endif
};
