// Wii Remote + Nunchuk emulation (WPAD/KPAD) on top of SDL3 (see input.cpp).
//
// Wiring (main thread):
//   - handleEvent() for every SDL event returned by aurora_update();
//   - beginFrame() once per frame, after the events are handled.
// The game reads the resulting state through KPADRead() from any thread.
#pragma once

#include <SDL3/SDL_events.h>

namespace port::input {

void handleEvent(const SDL_Event& event);
void beginFrame();

// Region of the window (in window coordinates) where the game image is shown.
// The pointer maps this rectangle to KPAD's [-1, 1] range. Defaults to the
// whole window; call it when the presentation is letterboxed.
void setPointerViewport(float x, float y, float width, float height);

// Called by the game while a "press A" prompt waits (see AUTOA in input.cpp).
void notifyPromptA();

}  // namespace port::input
