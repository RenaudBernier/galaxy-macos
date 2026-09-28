#pragma once

#include <revolution/types.h>

// Wii Remote speaker wave (SpkRes.arc); big-endian file data. The samples
// stay big-endian: the remote speaker isn't emulated on the host.
struct WaveData {
    BE(u32) mSize;
    BE(u32) mLoopStartPos;
    BE(u32) mLoopEndPos;
    s16 mWave[];
};

class SpkWave {
public:
    SpkWave();

    void setResource(void*);
    s32 getWaveSize(s32) const;
    u32 getLoopStartPos(s32) const;
    u32 getLoopEndPos(s32) const;
    s16* getWave(s32) const;
    WaveData* getWaveData(s32) const;

    /* 0x0 */ void* mResource;  // Raw data of AudioRes/SpkRes/SpkRes.arc
};
