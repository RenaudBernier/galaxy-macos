// Shape of the game image (see port/src/vi.cpp).
//
// The game's 640x456 framebuffer is stretched to fill the window, so the game
// adapts its 3D projection and 2D layouts to the window's aspect ratio.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Width / height of the window's image, updated once per frame.
float PortScreenAspect(void);

#ifdef __cplusplus
}
#endif
