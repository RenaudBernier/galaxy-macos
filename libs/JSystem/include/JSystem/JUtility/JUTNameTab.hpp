#pragma once

#include <revolution.h>

// Name table; big-endian file data.
struct ResNTAB {
    BE(u16) mEntryNum;
    BE(u16) _2;

    struct Entry {
        BE(u16) mKeyCode;
        BE(u16) mOffs;
    } mEntries[1];
};

class JUTNameTab {
public:
    JUTNameTab();
    JUTNameTab(const ResNTAB*);

    virtual ~JUTNameTab() {
    }

    void setResource(const ResNTAB*);
    s32 getIndex(const char*) const;
    const char* getName(u16) const;
    u16 calcKeyCode(const char*) const;

    const ResNTAB* mResource;  // 0x4
    const char* mStrData;      // 0x8
    u16 mNameNum;              // 0xC
    u16 _E;
};
