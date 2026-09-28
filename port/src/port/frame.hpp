// Frame boundary / VI timing (see vi.cpp).
#pragma once

namespace port::frame {

// Call on the game's main thread after the scheduler is initialized.
void init();
// Opens the first Aurora frame so the game can issue GX commands during boot.
void beginFirstFrame();
bool quitRequested();

}  // namespace port::frame
