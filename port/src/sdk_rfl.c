// Mii library (RVLFaceLib) pieces missing from the decomp's build.
// Compiled with -fshort-wchar: RFL structs contain 16-bit wchar_t names.
//
// The host has no Mii database; RFL behaves as if none were ever created.

#include "../../src/RVLFaceLib/RFL_MakeRandomFace.c"

#include <stddef.h>
#include <string.h>

// Two faces are "the same" when every facial part matches (faceline through
// mole); names, birthdays and IDs don't matter.
BOOL RFLiIsSameFaceCore(const RFLiCharInfo* lhs, const RFLiCharInfo* rhs) {
    return memcmp(lhs, rhs, offsetof(RFLiCharInfo, body)) == 0 ? TRUE : FALSE;
}

// Would create the Mii database file on NAND. There is none on the host, so
// finish the pending operation as "not available".
RFLErrcode RFLiFormatAsync(RFLiCallback cb) {
    (void)cb;
    RFLiEndWorking(RFLErrcode_NotAvailable);
    return RFLErrcode_NotAvailable;
}
