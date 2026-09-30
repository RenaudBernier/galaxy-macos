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

#include "os/scheduler.hpp"
#include "port/frame.hpp"
#include "port/log.hpp"
#include "port/savestate.hpp"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <revolution/vi.h>

#include <pthread.h>

#include <atomic>
#include <cstring>
#include <chrono>
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

PORT_SAVED std::atomic<u32> sRetraceCount{0};
PORT_SAVED VIRetraceCallback sPreCallback = nullptr;
PORT_SAVED VIRetraceCallback sPostCallback = nullptr;
PORT_SAVED OSThreadQueue sRetraceQueue;
PORT_SAVED void* sNextFrameBuffer = nullptr;
PORT_SAVED void* sCurrentFrameBuffer = nullptr;
PORT_SAVED bool sBlack = true;
PORT_SAVED bool sDimming = false;
OSThread* sRenderThread = nullptr;
bool sFrameOpen = false;
bool sQuitRequested = false;
float sScreenAspect = 16.0f / 9.0f;

// The game reads the aspect ratio while it builds a frame; sample it between
// frames so a window resize never splits one.
void updateScreenAspect() {
    u32 width = 0;
    u32 height = 0;
    AuroraGetRenderSize(&width, &height);
    if (width != 0 && height != 0) {
        sScreenAspect = static_cast<float>(width) / static_cast<float>(height);
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

// Submits the frame, handles window events and save states, and begins the
// next frame. Loading a state puts this function's frame (and its callers')
// back as they were when the state was saved, so nothing after the save state
// point may use a value computed before it.
[[gnu::noinline]] void finishFrame() {
    beginHostWork();
    aurora_end_frame();
    sFrameOpen = false;
    pumpEvents();
    updateScreenAspect();
    port::savestate::processRequests();
    while (!aurora_begin_frame()) {
        // Window not presentable (e.g. minimized).
        pumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    sFrameOpen = true;
    endHostWork();
    port::input::beginFrame();
}

}  // namespace

namespace port::frame {

void init() {
    OSInitThreadQueue(&sRetraceQueue);
    sRenderThread = OSGetCurrentThread();
    std::thread(retraceTimerMain).detach();
}

void beginFirstFrame() {
    beginHostWork();
    pumpEvents();
    updateScreenAspect();
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
    finishFrame();
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

float PortScreenAspect(void) { return sScreenAspect; }

}  // extern "C"
