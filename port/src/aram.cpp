// ARAM (AR/ARQ) emulation.
//
// The Wii has no ARAM; the RVL SDK's "aralt" library (src/RVL_SDK/aralt)
// emulates it inside MEM2, and this is a host version of the same thing:
//   - ARInit takes the current MEM2 arena as the "ARAM" backing store;
//   - ARAM addresses are offsets into it, starting at ARGetBaseAddress()
//     (0x4000, as on the Wii);
//   - ARStartDMA is a synchronous memcpy.
// JASKernel builds its ARAM heap from these offsets (never dereferenced by the
// CPU), and every transfer goes through ARStartDMA, which adds the MEM2 base.

#include "port/log.hpp"
#include "port/savestate.hpp"

#include <revolution/aralt.h>
#include <revolution/os.h>

#include <cstring>

namespace {

PORT_SAVED u32 sBaseAdr = 0;           // Wii address of the ARAM backing (MEM2 arena low)
PORT_SAVED u32 sMemoryTop = 0x90000000;  // next ARAlloc address (Wii address)
PORT_SAVED u32 sSize = 0;
PORT_SAVED bool sInitialized = false;

}  // namespace

extern "C" {

void ARStartDMA(u32 type, u32 ram_addr, u32 aram_addr, u32 len) {
    // Same argument handling as aralt: for type 0 (MRAM -> ARAM) the third
    // argument is the ARAM destination; for type 1 (ARAM -> MRAM) the second
    // argument is the ARAM source.
    if (type == 0) {
        memcpy(U32_TO_PTR(void*, aram_addr + sBaseAdr), U32_TO_PTR(const void*, ram_addr), len);
    } else if (type == 1) {
        memcpy(U32_TO_PTR(void*, aram_addr), U32_TO_PTR(const void*, ram_addr + sBaseAdr), len);
    }
}

u32 ARAlloc(u32 amount) {
    BOOL en = OSDisableInterrupts();
    u32 top = sMemoryTop;
    sMemoryTop += amount;
    OSRestoreInterrupts(en);
    return top - sBaseAdr;
}

u32 ARGetBaseAddress(void) { return 0x4000; }

u32 ARInit(u32*, u32) {
    if (sInitialized) {
        return 0x4000;
    }
    sInitialized = true;

    const u32 memLo = PTR_TO_U32(OSGetMEM2ArenaLo());
    const u32 memHi = PTR_TO_U32(OSGetMEM2ArenaHi());
    sBaseAdr = memLo;
    sMemoryTop = memLo + ARGetBaseAddress();
    sSize = memHi - memLo;
    PORT_INFO("aram", "ARInit: ARAM emulated in MEM2 0x{:08X}-0x{:08X} ({} KB)", memLo, memHi, sSize >> 10);
    return 0x4000;
}

u32 ARGetSize(void) { return sSize; }

// Requests are never queued: JKRAramPiece calls ARStartDMA directly.
void ARQInit(void) {}

}  // extern "C"
