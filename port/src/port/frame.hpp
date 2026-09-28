// Frame boundary / VI timing (see vi.cpp).
#pragma once

struct SDL_Window;

namespace port::frame {

// Chooses 60 or 120 fps from the display's refresh rate (SMG_FPS=60 forces 60).
void configureFrameRate(SDL_Window* window);
// Call on the game's main thread after the scheduler is initialized.
void init();
// Opens the first Aurora frame so the game can issue GX commands during boot.
void beginFirstFrame();
bool quitRequested();

}  // namespace port::frame
