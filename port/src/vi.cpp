// VI: vertical retrace timing, retrace callbacks and the frame boundary.
//
// A host timer produces retrace "interrupts" at the NTSC field rate
// (59.94 Hz): each one bumps the retrace count, runs the game's pre/post
// retrace callbacks and wakes threads waiting in VIWaitForRetrace, as the
// hardware does.
//
// The game's frame ends when its main thread calls VIWaitForRetrace. There
// the port submits the Aurora frame, pumps window/input events, waits for the
// next retrace and begins the next Aurora frame. VIInit/VIConfigure/VIFlush/
// VIGetTvFormat come from Aurora.
//
// At 120 fps (on displays of 100 Hz or more, unless SMG_FPS=60) the game
// still runs at 60 Hz, and the port shows an extra frame between two game
// frames: Aurora renders each game frame first with its matrices blended
// halfway toward the previous frame's, then exactly (GXAuroraInterpReplay),
// and vsync presents the two on consecutive refreshes. Game code tags the
// objects it draws (port/interp.h) so matrices can be matched across frames.

#include "os/scheduler.hpp"
#include "port/frame.hpp"
#include "port/log.hpp"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <revolution/vi.h>
#include <SDL3/SDL_video.h>

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>

namespace port::input {
void handleEvent(const SDL_Event& event);
void beginFrame();
}  // namespace port::input

namespace port::os {
void requestPowerOff();
}

using namespace port::os;

namespace {

constexpr double kFieldRate = 60000.0 / 1001.0;

std::atomic<u32> sRetraceCount{0};
VIRetraceCallback sPreCallback = nullptr;
VIRetraceCallback sPostCallback = nullptr;
OSThreadQueue sRetraceQueue;
void* sNextFrameBuffer = nullptr;
void* sCurrentFrameBuffer = nullptr;
bool sBlack = true;
bool sDimming = false;
OSThread* sRenderThread = nullptr;
bool sFrameOpen = false;
bool sQuitRequested = false;
SDL_Window* sWindow = nullptr;
bool sInterpolate = false;
bool sFrameRateStale = true;
const void* sInterpTag = nullptr;

// 120 fps needs a display of 100 Hz or more: the two frames of each game
// frame are paced by the display (vsync), so a 60 Hz display would run the
// game at half speed. At 60 fps the VI retrace timer paces frames instead.
void updateFrameRate() {
    sFrameRateStale = false;
    const char* setting = getenv("SMG_FPS");
    const SDL_DisplayMode* mode = sWindow != nullptr ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(sWindow)) : nullptr;
    const bool interpolate = !(setting != nullptr && std::string_view(setting) == "60") && mode != nullptr &&
                             mode->refresh_rate >= 100.0f;
    if (interpolate == sInterpolate) {
        return;
    }
    sInterpolate = interpolate;
    GXAuroraInterpSetEnabled(interpolate ? GX_TRUE : GX_FALSE);
    aurora_enable_vsync(interpolate);
    PORT_INFO("vi", "{} fps", interpolate ? 120 : 60);
}

// Ends a game frame at 120 fps: the frame just drawn was rendered halfway
// toward the previous one and is presented first, then the exact frame.
void endInterpolatedFrame() {
    aurora_end_frame();
    if (aurora_begin_frame()) {
        GXAuroraInterpReplay();
        aurora_end_frame();
    }
}

void retraceInterrupt() {
    const u32 count = ++sRetraceCount;
    if (sPreCallback != nullptr) {
        sPreCallback(count);
    }
    sCurrentFrameBuffer = sNextFrameBuffer;
    OSWakeupThread(&sRetraceQueue);
    if (sPostCallback != nullptr) {
        sPostCallback(count);
    }
}

void retraceTimerMain() {
    pthread_setname_np("VI retrace");
    using namespace std::chrono;
    const auto period = duration_cast<steady_clock::duration>(duration<double>(1.0 / kFieldRate));
    auto next = steady_clock::now() + period;
    for (;;) {
        std::this_thread::sleep_until(next);
        next += period;
        const auto now = steady_clock::now();
        if (now > next + period * 4) {
            next = now + period;  // don't try to catch up after a long stall
        }
        postInterrupt(retraceInterrupt);
    }
}

void pumpEvents() {
    const AuroraEvent* event = aurora_update();
    for (; event != nullptr && event->type != AURORA_NONE; event++) {
        switch (event->type) {
        case AURORA_SDL_EVENT:
            if (event->sdl.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event->sdl.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED) {
                sFrameRateStale = true;
            }
            port::input::handleEvent(event->sdl);
            break;
        case AURORA_EXIT:
            if (!sQuitRequested) {
                sQuitRequested = true;
                port::os::requestPowerOff();
            }
            break;
        default:
            break;
        }
    }
}

}  // namespace

namespace port::frame {

void configureFrameRate(SDL_Window* window) {
    sWindow = window;
    updateFrameRate();
}

void init() {
    OSInitThreadQueue(&sRetraceQueue);
    sRenderThread = OSGetCurrentThread();
    std::thread(retraceTimerMain).detach();
}

void beginFirstFrame() {
    beginHostWork();
    pumpEvents();
    while (!aurora_begin_frame()) {
        pumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    endHostWork();
    sFrameOpen = true;
    port::input::beginFrame();
}

bool quitRequested() { return sQuitRequested; }

}  // namespace port::frame

extern "C" {

void VIWaitForRetrace(void) {
    const u32 start = sRetraceCount.load();
    while (sRetraceCount.load() == start) {
        OSSleepThread(&sRetraceQueue);
    }
    if (OSGetCurrentThread() == sRenderThread) {
        // Keep the window responsive during loading loops.
        beginHostWork();
        pumpEvents();
        endHostWork();
    }
}

// The game ends each rendered frame by copying the EFB to the XFB; that is
// where the Aurora frame is submitted and the next one begins.
void PortGXCopyDisp(void* dest, GXBool clear) {
    (GXCopyDisp)(dest, clear);
    if (OSGetCurrentThread() != sRenderThread || !sFrameOpen) {
        return;
    }
    beginHostWork();
    if (sInterpolate) {
        endInterpolatedFrame();
    } else {
        aurora_end_frame();
    }
    sFrameOpen = false;
    pumpEvents();
    if (sFrameRateStale) {
        updateFrameRate();  // between frames, so no frame is half interpolated
    }
    while (!aurora_begin_frame()) {
        // Window not presentable (e.g. minimized).
        pumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    sFrameOpen = true;
    endHostWork();
    port::input::beginFrame();
}

u32 VIGetRetraceCount(void) {
    preemptPoint();
    return sRetraceCount.load();
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    const BOOL level = OSDisableInterrupts();
    VIRetraceCallback prev = sPreCallback;
    sPreCallback = cb;
    OSRestoreInterrupts(level);
    return prev;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    const BOOL level = OSDisableInterrupts();
    VIRetraceCallback prev = sPostCallback;
    sPostCallback = cb;
    OSRestoreInterrupts(level);
    return prev;
}

void VISetNextFrameBuffer(void* fb) { sNextFrameBuffer = fb; }
void* VIGetNextFrameBuffer(void) { return sNextFrameBuffer; }
void* VIGetCurrentFrameBuffer(void) { return sCurrentFrameBuffer; }

void VISetBlack(BOOL black) { sBlack = black != 0; }

u32 VIGetDTVStatus(void) { return 1; }  // component cable: progressive capable

BOOL VIEnableDimming(BOOL enable) {
    const BOOL prev = sDimming;
    sDimming = enable != 0;
    return prev;
}
u32 VIGetDimmingCount(void) { return 0; }
BOOL VIResetDimmingCount(void) { return TRUE; }
u32 VIGetCurrentLine(void) { return 0; }
u32 VIGetScanMode(void) { return 2; }  // progressive
void VISetTrapFilter(VIBool) {}

void PortInterpTag(const void* tag) {
    if (sInterpolate) {
        sInterpTag = tag;
        GXAuroraInterpTag(reinterpret_cast<uintptr_t>(tag));
    }
}

const void* PortInterpCurrentTag(void) { return sInterpTag; }

}  // extern "C"
