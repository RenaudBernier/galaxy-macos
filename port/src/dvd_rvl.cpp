// Wii DVD calls that Aurora (GameCube-oriented) doesn't provide.

#include "os/scheduler.hpp"

#include <revolution/dvd.h>

extern "C" {

// The disc is always present on the host. The result (non-zero = disc
// inserted) is delivered like a DVD interrupt.
BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    if (callback != nullptr) {
        port::os::postInterrupt([block, callback] { callback(1, block); });
    }
    return TRUE;
}

}  // extern "C"
