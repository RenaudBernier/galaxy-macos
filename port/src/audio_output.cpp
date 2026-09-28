// Host audio output: plays the AI DMA blocks through an SDL3 audio stream.
//
// JAudio2 fills one AI DMA block per AI interrupt (interleaved stereo s16,
// right channel first as the Wii's AI reads it). Each block is queued on an
// SDL audio stream at the AI rate; SDL resamples to the device.
//
// Environment: SMG_NOAUDIO=1 disables output, SMG_VOLUME=0..1 sets the gain.

#include "port/audio.hpp"
#include "port/log.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace {

std::mutex sMutex;
SDL_AudioStream* sStream = nullptr;
int sStreamRate = 0;
bool sDisabled = false;
bool sInitTried = false;
std::vector<s16> sScratch;

// Anything queued beyond this is dropped (the device fell behind).
constexpr double kMaxQueuedSeconds = 0.15;

bool ensureStream(double rateHz) {
    const int rate = static_cast<int>(std::lround(rateHz));
    if (sStream != nullptr && rate == sStreamRate) {
        return true;
    }
    if (sDisabled) {
        return false;
    }
    if (!sInitTried) {
        sInitTried = true;
        if (getenv("SMG_NOAUDIO") != nullptr) {
            sDisabled = true;
            PORT_INFO("audio", "audio output disabled (SMG_NOAUDIO)");
            return false;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            sDisabled = true;
            PORT_WARN("audio", "SDL audio unavailable: {}", SDL_GetError());
            return false;
        }
    }
    if (sStream != nullptr) {
        SDL_DestroyAudioStream(sStream);
        sStream = nullptr;
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, rate};
    sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (sStream == nullptr) {
        sDisabled = true;
        PORT_WARN("audio", "could not open an audio device: {}", SDL_GetError());
        return false;
    }
    sStreamRate = rate;
    float gain = 1.0f;
    if (const char* v = getenv("SMG_VOLUME")) {
        gain = std::fmax(0.0f, std::fmin(1.0f, static_cast<float>(atof(v))));
    }
    SDL_SetAudioStreamGain(sStream, gain);
    SDL_ResumeAudioStreamDevice(sStream);
    PORT_INFO("audio", "audio output: {} Hz stereo", rate);
    return true;
}

}  // namespace

namespace port::audio {

void outputPushDma(const s16* rightLeft, u32 frames, double rateHz) {
    std::lock_guard lock(sMutex);
    if (rightLeft == nullptr || frames == 0 || !ensureStream(rateHz)) {
        return;
    }
    const int queuedBytes = SDL_GetAudioStreamQueued(sStream);
    if (queuedBytes > 0 && queuedBytes / 4.0 / sStreamRate > kMaxQueuedSeconds) {
        return;  // drop this block to catch up
    }
    sScratch.resize(frames * 2);
    for (u32 i = 0; i < frames; i++) {
        sScratch[i * 2 + 0] = rightLeft[i * 2 + 1];  // left
        sScratch[i * 2 + 1] = rightLeft[i * 2 + 0];  // right
    }
    SDL_PutAudioStreamData(sStream, sScratch.data(), static_cast<int>(frames * 4));
}

double outputQueuedSeconds() {
    std::lock_guard lock(sMutex);
    if (sStream == nullptr || sStreamRate == 0) {
        return -1.0;
    }
    return SDL_GetAudioStreamQueued(sStream) / 4.0 / sStreamRate;
}

}  // namespace port::audio
