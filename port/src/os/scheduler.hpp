// Uniprocessor thread scheduler emulating the Wii's OS (see scheduler.cpp).
#pragma once

#include "port/os_types.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace port::os {

// Must be called on the thread that will run the game's main (default) thread.
void initScheduler(OSThread* defaultThread, void* stackBase, void* stackEnd);

// "Interrupts": work posted from host threads (timers, VI, DVD, audio) that
// must run while no game thread is executing, like an exception handler on the
// Wii. Handlers run at the next preemption point of the running thread, or
// immediately on the posting side when all game threads are idle.
void postInterrupt(std::function<void()> handler);

// Called at points where the Wii could have been interrupted/preempted.
void preemptPoint();

// Brackets long host-side work done by the running game thread (e.g. frame
// submission). Interrupts arriving meanwhile wait until the next preemption
// point instead of being forced by the watchdog.
void beginHostWork();
void endHostWork();

// Interrupt-enable flag of the running thread (OSDisableInterrupts & co.).
bool disableInterrupts();
bool restoreInterrupts(bool enable);

// Scheduler-level helpers used by the other OS modules. All of them must be
// called from a game thread or from interrupt context.
OSThread* currentThread();
void sleepOnQueue(OSThreadQueue* queue);
void wakeupQueue(OSThreadQueue* queue);

// Block the calling game thread (in host terms) until `pred` holds, letting
// other game threads and interrupts run meanwhile. `pred` is evaluated with the
// scheduler lock held.
void waitUntil(const std::function<bool()>& pred);

// --- Save states (port/src/savestate.cpp) -----------------------------------
//
// Game threads are host threads, so a save state can only restore a thread's
// stack where the thread is in the same place: parked in the scheduler, with
// the same frames above the park point. The running thread parks in the
// function that captures or restores the state (`runningFp` is its frame).

struct ThreadState {
    OSThread* thread = nullptr;
    bool started = false;       // has run (a thread that never ran has no frames)
    uintptr_t regionStart = 0;  // stack above the park point...
    uintptr_t regionEnd = 0;    // ...up to the outermost game frame
    std::vector<uintptr_t> chain;  // frame pointers and return addresses from the park point up
    bool interruptsEnabled = true;
    bool yielding = false;
    uint64_t readySeq = 0;
};

struct SchedulerState {
    std::vector<ThreadState> threads;
    uint64_t seq = 0;
};

// The functions below need the scheduler locked (no interrupt can run).
std::unique_lock<std::mutex> lockScheduler();
// Fails with a reason unless every other game thread is waiting (not in the
// middle of some work).
bool captureSchedulerState(SchedulerState& out, uintptr_t runningFp, std::string& why);
// Whether the threads are the same and parked in the same places as in `saved`.
bool matchesSchedulerState(const SchedulerState& saved, uintptr_t runningFp, std::string& why);
void restoreSchedulerState(const SchedulerState& saved);
size_t pendingInterruptCount();
// Interrupts posted before a state is loaded belong to the replaced timeline.
void dropPendingInterrupts();

// Alarms and the time base (time_alarm.cpp).
struct AlarmState {
    int64_t ticks = 0;  // OSGetTime when saved
    std::vector<std::pair<int64_t, OSAlarm*>> queue;
    std::vector<std::pair<OSAlarm*, uint64_t>> generations;
};
std::unique_lock<std::mutex> lockAlarms();
void captureAlarmState(AlarmState& out);
// The time base resumes at the saved time, so the saved alarms stay valid.
void restoreAlarmState(const AlarmState& state);

}  // namespace port::os
