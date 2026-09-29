// Reset/power: the Wii's power and reset buttons and system shutdown calls.

#include "port/power.hpp"
#include "port/log.hpp"
#include "os/scheduler.hpp"
#include "port/savestate.hpp"

#include <revolution/os.h>

#include <atomic>
#include <cstdlib>

namespace {
PORT_SAVED std::atomic<OSPowerCallback> sPowerCallback{nullptr};
}  // namespace

namespace port::os {

void requestPowerOff() {
    OSPowerCallback cb = sPowerCallback.load();
    if (cb == nullptr) {
        exitGame("window closed");
    }
    postInterrupt([cb] { cb(); });
}

void exitGame(const char* reason) {
    PORT_INFO("os", "exiting: {}", reason);
    fflush(stdout);
    fflush(stderr);
    std::_Exit(0);
}

}  // namespace port::os

extern "C" {

BOOL OSGetResetButtonState(void) { return FALSE; }

OSPowerCallback OSSetPowerCallback(OSPowerCallback cb) { return sPowerCallback.exchange(cb); }

void OSShutdownSystem(void) { port::os::exitGame("OSShutdownSystem"); }

void OSRebootSystem(void) { port::os::exitGame("OSRebootSystem"); }

void OSRestart(u32 resetCode) {
    (void)resetCode;
    port::os::exitGame("OSRestart");
}

void OSReturnToMenu(void) { port::os::exitGame("OSReturnToMenu"); }

}  // extern "C"
