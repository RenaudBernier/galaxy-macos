#include "Game/Speaker/SpkWave.hpp"
#include <JSystem/JAudio2/JASCriticalSection.hpp>
#include <revolution/os.h>

SpkWave::SpkWave() : mResource(nullptr) {
}

void SpkWave::setResource(void* pResource) {
    JASCriticalSection crit;
    mResource = pResource;
}

s32 SpkWave::getWaveSize(s32 wave) const {
    if (mResource == nullptr) {
        return 0;
    }

    return getWaveData(wave)->mSize;
}

u32 SpkWave::getLoopStartPos(s32 wave) const {
    if (mResource == nullptr) {
        return 0;
    }

    return getWaveData(wave)->mLoopStartPos;
}

u32 SpkWave::getLoopEndPos(s32 wave) const {
    if (mResource == nullptr) {
        return 0;
    }

    return getWaveData(wave)->mLoopEndPos;
}

s16* SpkWave::getWave(s32 wave) const {
    if (mResource == nullptr) {
        return nullptr;
    }

    return getWaveData(wave)->mWave;
}

WaveData* SpkWave::getWaveData(s32 wave) const {
    // PORT: file-reloc (offset table read from the resource)
#ifdef TARGET_PC
    return U32_TO_PTR(WaveData*, PTR_TO_U32(mResource) + PortReadBE32(U32_TO_PTR(u8*, PTR_TO_U32(mResource) + wave * 4 + 8)));
#else
    return U32_TO_PTR(WaveData*, PTR_TO_U32(mResource) + *U32_TO_PTR(u32*, PTR_TO_U32(mResource) + wave * 4 + 8));
#endif
}
