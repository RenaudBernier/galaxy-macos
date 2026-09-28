#pragma once

#include "JSystem/J3DGraphBase/J3DTransform.hpp"

class J3DJoint;
struct J3DJointBlock;

#ifdef TARGET_PC
// JNT1 joint entries as stored in the (big-endian) file.
struct J3DJointInitData {
    /* 0x00 */ BE(u16) mKind;
    /* 0x02 */ u8 mScaleCompensate;
    /* 0x03 */ u8 _3;
    /* 0x04 */ BE(f32) mScale[3];
    /* 0x10 */ BE(s16) mRotation[3];
    /* 0x16 */ BE(u16) _16;
    /* 0x18 */ BE(f32) mTranslate[3];
    /* 0x24 */ BE(f32) mRadius;
    /* 0x28 */ BE(f32) mMin[3];
    /* 0x34 */ BE(f32) mMax[3];
};  // Size: 0x40
#else
struct J3DJointInitData {
    /* 0x00 */ u16 mKind;
    /* 0x02 */ bool mScaleCompensate;
    /* 0x04 */ J3DTransformInfo mTransformInfo;
    /* 0x24 */ f32 mRadius;
    /* 0x28 */ Vec mMin;
    /* 0x2C */ Vec mMax;
};  // Size: 0x30
#endif

struct J3DJointFactory {
    J3DJointFactory(J3DJointBlock const&);
    J3DJoint* create(int);

    J3DJointInitData* mJointInitData;
    BE(u16)* mIndexTable;

    u16 getKind(int no) const {
        return mJointInitData[mIndexTable[no]].mKind;
    }
    u8 getScaleCompensate(int no) const {
        return mJointInitData[mIndexTable[no]].mScaleCompensate;
    }
#ifdef TARGET_PC
    J3DTransformInfo getTransformInfo(int no) const {
        const J3DJointInitData& d = mJointInitData[mIndexTable[no]];
        J3DTransformInfo info;
        info.mScale.x = d.mScale[0];
        info.mScale.y = d.mScale[1];
        info.mScale.z = d.mScale[2];
        info.mRotation.x = d.mRotation[0];
        info.mRotation.y = d.mRotation[1];
        info.mRotation.z = d.mRotation[2];
        info._12 = d._16;
        info.mTranslate.x = d.mTranslate[0];
        info.mTranslate.y = d.mTranslate[1];
        info.mTranslate.z = d.mTranslate[2];
        return info;
    }
    f32 getRadius(int no) const {
        return mJointInitData[mIndexTable[no]].mRadius;
    }
    Vec getMin(int no) const {
        const J3DJointInitData& d = mJointInitData[mIndexTable[no]];
        Vec v = {d.mMin[0], d.mMin[1], d.mMin[2]};
        return v;
    }
    Vec getMax(int no) const {
        const J3DJointInitData& d = mJointInitData[mIndexTable[no]];
        Vec v = {d.mMax[0], d.mMax[1], d.mMax[2]};
        return v;
    }
#else
    const J3DTransformInfo& getTransformInfo(int no) const {
        return mJointInitData[mIndexTable[no]].mTransformInfo;
    }
    f32 getRadius(int no) const {
        return mJointInitData[mIndexTable[no]].mRadius;
    }
    Vec& getMin(int no) const {
        return mJointInitData[mIndexTable[no]].mMin;
    }
    Vec& getMax(int no) const {
        return mJointInitData[mIndexTable[no]].mMax;
    }
#endif
};
