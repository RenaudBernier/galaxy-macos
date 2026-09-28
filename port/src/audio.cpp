// Audio interface (AI) and DSP emulation.
//
// Both devices behave like the hardware as far as JAudio2 can tell, so its
// threads run their normal frame loop:
//
// AI: after AIStartDMA, a host timer raises the AI DMA interrupt once per DMA
//     block (block length from AIInitDMA, 32 kHz or 48 kHz stereo s16). Each
//     tick first hands the block the AI would now play to the host output
//     (audio_output.cpp); the timer is nudged to follow the device's clock.
//
// DSP: implements the mailbox protocol spoken by the JAudio2 DSP ucode
//     (src/JSystem/JAudio2/dsptask.cpp, osdsp_task.cpp, JASAudioThread.cpp):
//     - booting the audio task mails 0xDCD10000 + a handshake word;
//     - a command is a word count followed by that many words (0 means 1);
//     - 0x81 (table setup) and 0x8E (VARAM) are acknowledged with
//       0xF355xxxx (xxxx = the command's upper half) through the task's request
//       callback. The CPU busy-waits on these with no OS calls in the loop, so
//       they are answered synchronously;
//     - 0x82 (sync frame, N sub-frames) is answered with N "sub-frame done"
//       requests (0xF355FF00), delivered as interrupts shortly afterwards;
//       each sub-frame is rendered into the DAC buffers (audio_dsp.cpp) in
//       that interrupt, just before it is reported done, so channel updates
//       JAudio2 makes between sub-frames are honoured as on the hardware.
//     Every DSP interrupt is delivered by calling the game's __DSPHandler,
//     which reads 0xDCD10004 and then hands the next mail to the task.

#include "os/scheduler.hpp"
#include "port/audio.hpp"
#include "port/log.hpp"

#include <port/wii_addr.h>

#include <revolution/ai.h>
#include <revolution/dsp.h>
#include <revolution/dsp/dsp_task.h>
#include <revolution/os.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
DSPTaskInfo* __DSP_first_task = nullptr;
DSPTaskInfo* __DSP_curr_task = nullptr;
}

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------

struct AiState {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    AIDCallback callback = nullptr;
    u32 dmaAddr = 0;
    u32 dmaLength = 0;  // bytes
    u32 sampleRate = 0;  // 0: 32 kHz, 1: 48 kHz (AISetDSPSampleRate)
    bool running = false;
    bool started = false;
};

AiState& ai() {
    static AiState s;
    return s;
}

// The Wii's nominal 32/48 kHz rates are really 32028.5 / 48043 Hz.
double aiRateHz(u32 rate) { return rate == 0 ? 32028.5 : 48043.0; }

// JASAiCtrl passes AIInitDMA a host pointer cast to 32 bits; accept that as
// well as a proper Wii address.
const s16* resolveDmaBuffer(u32 addr) {
    if (addr >= 0x80000000u) {
        return U32_TO_PTR(const s16*, addr);
    }
    uintptr_t candidate = (gPortWiiBase & ~static_cast<uintptr_t>(0xFFFFFFFFu)) | addr;
    if (candidate < gPortWiiBase) {
        candidate += static_cast<uintptr_t>(1) << 32;
    }
    if (candidate - gPortWiiBase >= 0x80000000u) {
        return nullptr;
    }
    return reinterpret_cast<const s16*>(candidate);
}

void aiThreadMain() {
    AiState& s = ai();
    std::unique_lock lock(s.mutex);
    Clock::time_point next = Clock::now();
    while (true) {
        s.cv.wait(lock, [&] { return s.running && s.dmaLength != 0; });
        double seconds = (s.dmaLength / 4.0) / aiRateHz(s.sampleRate);
        // Follow the output device's clock: run slightly fast while little
        // audio is queued and slightly slow while a lot is.
        const double queued = port::audio::outputQueuedSeconds();
        if (queued >= 0.0) {
            const double target = seconds * 3.0;
            const double adjust = std::clamp((queued - target) / target * 0.02, -0.02, 0.02);
            seconds *= 1.0 + adjust;
        }
        const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
        const auto now = Clock::now();
        next += period;
        if (next < now - period * 4) {
            next = now + period;  // resynchronise after a stall instead of bursting
        }
        if (s.cv.wait_until(lock, next, [&] { return !s.running; })) {
            next = Clock::now();
            continue;
        }
        // The block the AI plays now is the one the game handed to AIInitDMA
        // during the previous interrupt.
        port::audio::outputPushDma(resolveDmaBuffer(s.dmaAddr), s.dmaLength / 4, aiRateHz(s.sampleRate));
        AIDCallback cb = s.callback;
        if (cb != nullptr) {
            lock.unlock();
            port::os::postInterrupt([cb] { cb(); });
            lock.lock();
        }
    }
}

// ---------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------

constexpr u32 kMailInitDone = 0xDCD10000;
constexpr u32 kMailRequest = 0xDCD10004;
constexpr u32 kMailHandshake = 0x8071FEED;
constexpr u32 kMailSubFrameDone = 0xF355FF00;

struct DspState {
    std::mutex mutex;
    std::deque<u32> fromDsp;

    // Command parser.
    bool expectCount = true;
    u32 remaining = 0;
    std::vector<u32> words;

    // Sub-frame rendering/completion worker.
    struct SubFrame {
        u32 dacL, dacR, index, mixerLevel;
    };
    std::condition_variable cv;
    std::thread thread;
    bool threadStarted = false;
    std::deque<SubFrame> pendingSubFrames;
};

DspState& dsp() {
    static DspState s;
    return s;
}

// Time between sub-frame completions. The real DSP renders a sub-frame in a
// fraction of its playback time.
constexpr auto kSubFrameStep = std::chrono::microseconds(500);

// Raises the DSP interrupt with the given mails queued. Must run where an
// interrupt could: in interrupt context or on a game thread.
void raiseDspInterrupt(std::initializer_list<u32> mails) {
    DspState& s = dsp();
    {
        std::lock_guard lock(s.mutex);
        for (u32 m : mails) {
            s.fromDsp.push_back(m);
        }
    }
    __DSPHandler(__OS_INTERRUPT_DSP_DSP, OSGetCurrentContext());
}

void dspWorkerMain() {
    DspState& s = dsp();
    std::unique_lock lock(s.mutex);
    while (true) {
        s.cv.wait(lock, [&] { return !s.pendingSubFrames.empty(); });
        const DspState::SubFrame sf = s.pendingSubFrames.front();
        s.pendingSubFrames.pop_front();
        lock.unlock();
        std::this_thread::sleep_for(kSubFrameStep);
        port::os::postInterrupt([sf] {
            // Interrupt context: the CPU isn't touching the channel blocks.
            port::audio::dspRenderSubFrame(sf.dacL, sf.dacR, sf.index, sf.mixerLevel);
            raiseDspInterrupt({kMailRequest, kMailSubFrameDone});
        });
        lock.lock();
    }
}

void runCommand(const std::vector<u32>& words) {
    if (words.empty()) {
        return;
    }
    const u32 head = words[0];
    switch (head >> 24) {
    case 0x81:  // DsetupTable: channel blocks, resampling/ADPCM filters, FX buffers
        if (words.size() >= 5) {
            port::audio::dspSetupTable(words[1], words[2], words[3], words[4]);
        }
        raiseDspInterrupt({kMailRequest, 0xF3550000 | (head >> 16)});
        break;
    case 0x8E:  // DsetVARAM: base of the (emulated) ARAM
        if (words.size() >= 2) {
            port::audio::dspSetVaram(words[1]);
        }
        raiseDspInterrupt({kMailRequest, 0xF3550000 | (head >> 16)});
        break;
    case 0x82: {  // DsyncFrame: render N sub-frames into the DAC buffers
        const u32 subFrames = (head >> 16) & 0xFF;
        const u32 dacL = words.size() >= 2 ? words[1] : 0;
        const u32 dacR = words.size() >= 3 ? words[2] : 0;
        DspState& s = dsp();
        std::lock_guard lock(s.mutex);
        if (!s.threadStarted) {
            s.threadStarted = true;
            s.thread = std::thread(dspWorkerMain);
            s.thread.detach();
        }
        for (u32 i = 0; i < subFrames; i++) {
            s.pendingSubFrames.push_back({dacL, dacR, i, head & 0xFFFF});
        }
        s.cv.notify_one();
        break;
    }
    default:
        // Channel halt release and other commands need no reply.
        break;
    }
}

}  // namespace

extern "C" {

// --- AI --------------------------------------------------------------------

void AIInit(u8*) {}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    AiState& s = ai();
    std::lock_guard lock(s.mutex);
    AIDCallback prev = s.callback;
    s.callback = callback;
    return prev;
}

void AIInitDMA(u32 addr, u32 length) {
    AiState& s = ai();
    std::lock_guard lock(s.mutex);
    s.dmaAddr = addr;
    s.dmaLength = length;
    s.cv.notify_all();
}

void AISetDSPSampleRate(u32 rate) {
    AiState& s = ai();
    std::lock_guard lock(s.mutex);
    s.sampleRate = rate;
}

void AIStartDMA(void) {
    AiState& s = ai();
    std::lock_guard lock(s.mutex);
    s.running = true;
    if (!s.started) {
        s.started = true;
        s.thread = std::thread(aiThreadMain);
        s.thread.detach();
    }
    s.cv.notify_all();
}

void AIStopDMA(void) {
    AiState& s = ai();
    std::lock_guard lock(s.mutex);
    s.running = false;
    s.cv.notify_all();
}

// --- DSP -------------------------------------------------------------------

void DSPInit(void) {
    DspState& s = dsp();
    std::lock_guard lock(s.mutex);
    s.fromDsp.clear();
    s.expectCount = true;
    s.remaining = 0;
    s.words.clear();
}

u32 DSPCheckMailToDSP(void) {
    // The emulated DSP consumes mail as soon as it is written.
    return 0;
}

u32 DSPCheckMailFromDSP(void) {
    DspState& s = dsp();
    std::lock_guard lock(s.mutex);
    return s.fromDsp.empty() ? 0 : 1;
}

u32 DSPReadMailFromDSP(void) {
    DspState& s = dsp();
    std::lock_guard lock(s.mutex);
    if (s.fromDsp.empty()) {
        return 0;
    }
    u32 mail = s.fromDsp.front();
    s.fromDsp.pop_front();
    return mail;
}

void DSPSendMailToDSP(u32 mail) {
    DspState& s = dsp();
    std::vector<u32> command;
    {
        std::lock_guard lock(s.mutex);
        if (s.expectCount) {
            s.remaining = mail == 0 ? 1 : mail;
            s.expectCount = false;
            s.words.clear();
            return;
        }
        s.words.push_back(mail);
        if (--s.remaining != 0) {
            return;
        }
        s.expectCount = true;
        command.swap(s.words);
    }
    runCommand(command);
}

void DSPAssertInt(void) {}

// Task switching: the audio task is the only DSP task in the game.
void __DSP_boot_task(DSPTaskInfo*) {
    // The ucode starts and reports in; the task's init callback expects a
    // second (handshake) mail to be waiting.
    raiseDspInterrupt({kMailInitDone, kMailHandshake});
}

void __DSP_exec_task(DSPTaskInfo*, DSPTaskInfo*) {}

void __DSP_remove_task(DSPTaskInfo* task) {
    if (__DSP_first_task == task) {
        __DSP_first_task = task != nullptr ? task->next : nullptr;
    }
}

}  // extern "C"
