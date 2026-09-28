// Power button / reset emulation.
#pragma once

namespace port::os {

// Called by the host when the window is closed: runs the game's power callback
// (as the Wii does when the power button is pressed), which leads the game to
// save state and call OSShutdownSystem(). Without a callback, exits directly.
void requestPowerOff();

// Terminates the process after the game asked the system to shut down/reset.
[[noreturn]] void exitGame(const char* reason);

}  // namespace port::os
