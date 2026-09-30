// System configuration (SC): the settings the Wii menu stores for the console.
//
// Defaults suit the RMGK01 data: Korean language, progressive scan, stereo.
// SMG_LANGUAGE (0-9, see SC_LANG_*) overrides the language.
//
// The aspect ratio setting picks the game's 4:3 or 16:9 layouts (HUD, menus);
// the image itself follows the window's shape either way (port/screen.h). The
// 16:9 layouts need a screen at least that wide, so they are used on 16:9 and
// wider displays and the 4:3 ones elsewhere (e.g. 16:10 laptops).
// SMG_ASPECT ("4:3"/"16:9") overrides the choice.

#include "port/log.hpp"

#include <revolution/sc.h>
#include <SDL3/SDL_video.h>

#include <cstdlib>
#include <cstring>

namespace {

bool sWideDisplay = true;

u8 envU8(const char* name, u8 fallback, u8 max) {
    const char* v = getenv(name);
    if (v == nullptr || *v == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long n = strtol(v, &end, 10);
    if (end == v || n < 0 || n > max) {
        PORT_WARN("sc", "ignoring {}={}", name, v);
        return fallback;
    }
    return static_cast<u8>(n);
}

}  // namespace

namespace port::sc {

void detectDisplayShape(SDL_Window* window) {
    const SDL_DisplayMode* mode = window != nullptr ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)) : nullptr;
    if (mode != nullptr && mode->h != 0) {
        sWideDisplay = static_cast<float>(mode->w) / mode->h >= 1.7f;
    }
}

}  // namespace port::sc

extern "C" {

u8 SCGetLanguage(void) {
    static const u8 lang = envU8("SMG_LANGUAGE", SC_LANG_KOREAN, SC_LANG_KOREAN);
    return lang;
}

u8 SCGetAspectRatio(void) {
    static const u8 aspect = [] {
        const char* v = getenv("SMG_ASPECT");
        if (v != nullptr && (strcmp(v, "4:3") == 0 || strcmp(v, "0") == 0)) {
            return static_cast<u8>(SC_ASPECT_RATIO_4x3);
        }
        if (v != nullptr && (strcmp(v, "16:9") == 0 || strcmp(v, "1") == 0)) {
            return static_cast<u8>(SC_ASPECT_RATIO_16x9);
        }
        return static_cast<u8>(sWideDisplay ? SC_ASPECT_RATIO_16x9 : SC_ASPECT_RATIO_4x3);
    }();
    return aspect;
}

u8 SCGetEuRgb60Mode(void) { return SC_EURGB60_MODE_OFF; }

u8 SCGetProgressiveMode(void) { return SC_PROGRESSIVE_MODE_ON; }

u8 SCGetSoundMode(void) { return SC_SOUND_MODE_STEREO; }

}  // extern "C"
