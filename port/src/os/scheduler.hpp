// Uniprocessor thread scheduler emulating the Wii's OS (see scheduler.cpp).
#pragma once

#include "port/os_types.hpp"

#include <functional>

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

}  // namespace port::os
