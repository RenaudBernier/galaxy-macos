#include "Game/Speaker/SpkTable.hpp"

SpkTable::SpkTable() {
    mInitialized = false;
    mResourceCount = 0;
    mParameters = nullptr;
    mNames = nullptr;
}

void SpkTable::setResource(void* pRes) {
    mInitialized = false;

    BE(s32)* cursor = (BE(s32)*)pRes;

    s32 resourceCount = *cursor++;
    s32 entryOff = *cursor++;
    s32 dataOffsetsStartOff = *cursor++;
    BE(s32)* pIsDataOffsetsInitialized = cursor;
    BOOL isDataOffsetsInitialized = *cursor++;

    mResourceCount = resourceCount;

    // PORT: file-reloc (name pointer table with 4-byte slots inside the resource)
    SpkParameters* entryOffset = U32_TO_PTR(SpkParameters*, PTR_TO_U32(pRes) + entryOff);
    mParameters = entryOffset;
#ifdef TARGET_PC
    PTR32(const char)* names = U32_TO_PTR(PTR32(const char)*, PTR_TO_U32(pRes) + dataOffsetsStartOff);
    if (!isDataOffsetsInitialized) {
        for (s32 i = 0; i < mResourceCount; i++) {
            names[i].addr = PortReadBE32(&names[i].addr) + PTR_TO_U32(pRes);
        }
    }
#else
    const char** names = U32_TO_PTR(const char**, PTR_TO_U32(pRes) + dataOffsetsStartOff);
    if (!isDataOffsetsInitialized) {
        for (s32 i = 0; i < mResourceCount; i++) {
            names[i] = U32_TO_PTR(const char*, PTR_TO_U32(names[i]) + PTR_TO_U32(pRes));
        }
    }
#endif

    mNames = names;
    *pIsDataOffsetsInitialized = TRUE;
    mInitialized = true;
}
