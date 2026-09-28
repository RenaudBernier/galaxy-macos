// System configuration (SC): the settings the Wii menu stores for the console.
//
// Defaults suit the RMGK01 data: Korean language, 16:9, progressive scan,
// stereo. SMG_LANGUAGE (0-9, see SC_LANG_*) and SMG_ASPECT ("4:3"/"16:9")
// override them.

#include "port/log.hpp"

#include <revolution/sc.h>

#include <cstdlib>
#include <cstring>

namespace {

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
        return static_cast<u8>(SC_ASPECT_RATIO_16x9);
    }();
    return aspect;
}

u8 SCGetEuRgb60Mode(void) { return SC_EURGB60_MODE_OFF; }

u8 SCGetProgressiveMode(void) { return SC_PROGRESSIVE_MODE_ON; }

u8 SCGetSoundMode(void) { return SC_SOUND_MODE_STEREO; }

}  // extern "C"
