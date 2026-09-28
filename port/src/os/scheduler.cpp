// Uniprocessor scheduler emulating the Wii OS thread model on host threads.
//
// Every OSThread is backed by a host pthread, but exactly one of them executes
// game code at any time: the highest-priority runnable thread (lower value =
// higher priority; equal priorities are not time-sliced), as on the Wii's
// single Broadway core. Threads hand the CPU over only at OS calls (blocking,
// waking, priority changes) and at preemption points.
//
// Asynchronous hardware events (alarms, VI retrace, DVD/NAND completion, audio
// DMA) are "interrupts": handlers posted from host threads run either at the
// running thread's next preemption point (typically OSRestoreInterrupts) or,
// when every game thread is blocked, directly on the posting thread. No game
// thread runs while a handler executes.

#include "os/scheduler.hpp"

#include "port/log.hpp"
#include "port/memory.hpp"

#include <pthread.h>
#include <sys/mman.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace port::os {
namespace {

constexpr u16 kReady = OS_THREAD_STATE_READY;
constexpr u16 kRunning = OS_THREAD_STATE_RUNNING;
constexpr u16 kWaiting = OS_THREAD_STATE_WAITING;
constexpr u16 kMoribund = OS_THREAD_STATE_MORIBUND;
constexpr u16 kAttrDetach = 1;
constexpr u32 kStackMagic = 0xDEADBABE;

struct HostThread {
    OSThread* thread = nullptr;
    pthread_t pthread{};
    std::condition_variable cv;
    void* (*entry)(void*) = nullptr;
    void* arg = nullptr;
    void* stack = nullptr;
    bool interruptsEnabled = true;
    bool cancelled = false;
    bool yielding = false;
    bool exited = false;
    uint64_t readySeq = 0;
    OSContext context{};
};

std::mutex gLock;
OSThread* gRunning = nullptr;
bool gInterruptActive = false;
std::deque<std::function<void()>> gPending;
std::atomic<bool> gHasPending{false};
std::atomic<int64_t> gOldestPendingNs{0};
std::unordered_map<OSThread*, HostThread*> gHost;
std::vector<OSThread*> gThreads;
std::vector<HostThread*> gZombies;
uint64_t gSeq = 0;
int gSchedulerSuspend = 0;

// Interrupt handlers that arrive while every game thread is blocked run on a
// dedicated host thread whose stack lives in the Wii address window (game code
// may convert stack addresses to Wii addresses).
std::condition_variable gInterruptCv;
bool gForceInterrupts = false;  // watchdog override, see watchdogMain
std::atomic<int> gHostWork{0};

thread_local HostThread* tSelf = nullptr;
thread_local bool tInInterrupt = false;
thread_local bool tHostInterruptsEnabled = true;  // for non-game host threads

using Lock = std::unique_lock<std::mutex>;

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

HostThread* host(OSThread* t) {
    auto it = gHost.find(t);
    return it == gHost.end() ? nullptr : it->second;
}

bool runnable(OSThread* t) { return t->suspend <= 0 && (t->state == kReady || t->state == kRunning); }

OSThread* pickNext() {
    OSThread* best = nullptr;
    uint64_t bestSeq = 0;
    for (OSThread* t : gThreads) {
        if (!runnable(t)) {
            continue;
        }
        HostThread* h = host(t);
        // The running thread keeps the CPU against equal priorities (no
        // time-slicing) unless it is yielding.
        const uint64_t seq = (t == gRunning && !h->yielding) ? 0 : h->readySeq;
        if (best == nullptr || t->priority < best->priority || (t->priority == best->priority && seq < bestSeq)) {
            best = t;
            bestSeq = seq;
        }
    }
    return best;
}

void reschedule() {
    if (gSchedulerSuspend > 0 && gRunning != nullptr && runnable(gRunning)) {
        return;
    }
    OSThread* next = pickNext();
    OSThread* prev = gRunning;
    if (next == prev) {
        if (prev != nullptr) {
            host(prev)->yielding = false;
        }
        return;
    }
    if (prev != nullptr) {
        HostThread* hp = host(prev);
        hp->yielding = false;
        if (prev->state == kRunning) {
            prev->state = kReady;
            hp->readySeq = ++gSeq;
        }
    }
    gRunning = next;
    if (next != nullptr) {
        next->state = kRunning;
        if (!gInterruptActive) {
            host(next)->cv.notify_all();
        }
    }
}

[[noreturn]] void exitHostThread(Lock& lock) {
    HostThread* self = tSelf;
    self->exited = true;
    gZombies.push_back(self);
    lock.unlock();
    pthread_exit(nullptr);
}

// Blocks the calling host thread until its OSThread owns the CPU.
void waitForCpu(Lock& lock) {
    HostThread* self = tSelf;
    if (gRunning == nullptr && !gPending.empty()) {
        gInterruptCv.notify_one();
    }
    while (!(gRunning == self->thread && !gInterruptActive)) {
        if (self->cancelled) {
            exitHostThread(lock);
        }
        self->cv.wait(lock);
    }
    if (self->cancelled) {
        exitHostThread(lock);
    }
}

// After a scheduling decision made by a game thread: give up the CPU if
// another thread was chosen.
void yieldIfNeeded(Lock& lock) {
    if (tSelf != nullptr && !tInInterrupt && gRunning != tSelf->thread) {
        waitForCpu(lock);
    }
}

void runPendingLocked(Lock& lock) {
    while (!gPending.empty()) {
        auto fn = std::move(gPending.front());
        gPending.pop_front();
        gHasPending = !gPending.empty();
        gInterruptActive = true;
        const bool prevIn = tInInterrupt;
        tInInterrupt = true;
        lock.unlock();
        fn();
        lock.lock();
        tInInterrupt = prevIn;
        gInterruptActive = false;
    }
    reschedule();
    if (gRunning != nullptr && (tSelf == nullptr || gRunning != tSelf->thread)) {
        host(gRunning)->cv.notify_all();
    }
}

void reapZombies() {
    // Called with gLock held. Joins exited host threads so their stacks can
    // be reused.
    for (auto it = gZombies.begin(); it != gZombies.end();) {
        HostThread* z = *it;
        if (z == tSelf) {
            ++it;
            continue;
        }
        pthread_join(z->pthread, nullptr);
        port::mem::freeThreadStack(z->stack);
        delete z;
        it = gZombies.erase(it);
    }
}

void removeThread(OSThread* t) {
    for (auto it = gThreads.begin(); it != gThreads.end(); ++it) {
        if (*it == t) {
            gThreads.erase(it);
            break;
        }
    }
    gHost.erase(t);
}

// --- OSThreadQueue helpers (same linkage as the Wii OS) --------------------

void queueRemove(OSThreadQueue* q, OSThread* t) {
    OSThread* next = t->link.next;
    OSThread* prev = t->link.prev;
    if (next == nullptr) {
        q->tail = prev;
    } else {
        next->link.prev = prev;
    }
    if (prev == nullptr) {
        q->head = next;
    } else {
        prev->link.next = next;
    }
    t->link.next = t->link.prev = nullptr;
}

void queueAddPrio(OSThreadQueue* q, OSThread* t) {
    OSThread* next = q->head;
    while (next != nullptr && next->priority <= t->priority) {
        next = next->link.next;
    }
    if (next == nullptr) {
        OSThread* prev = q->tail;
        if (prev == nullptr) {
            q->head = t;
        } else {
            prev->link.next = t;
        }
        t->link.prev = prev;
        t->link.next = nullptr;
        q->tail = t;
    } else {
        t->link.next = next;
        OSThread* prev = next->link.prev;
        next->link.prev = t;
        t->link.prev = prev;
        if (prev == nullptr) {
            q->head = t;
        } else {
            prev->link.next = t;
        }
    }
}

void wakeupQueueLocked(OSThreadQueue* q) {
    while (OSThread* t = q->head) {
        queueRemove(q, t);
        t->queue = nullptr;
        if (t->state == kWaiting) {
            t->state = kReady;
            host(t)->readySeq = ++gSeq;
        }
    }
}

void sleepOnQueueLocked(Lock& lock, OSThreadQueue* q) {
    if (tInInterrupt || tSelf == nullptr) {
        PORT_FATAL("os", "blocking call from interrupt/host context");
        __builtin_trap();
    }
    OSThread* t = tSelf->thread;
    t->state = kWaiting;
    t->queue = q;
    queueAddPrio(q, t);
    reschedule();
    waitForCpu(lock);
}

void* hostEntry(void* arg) {
    auto* h = static_cast<HostThread*>(arg);
    tSelf = h;
    {
        char name[32];
        snprintf(name, sizeof(name), "OSThread %p", (void*)h->thread);
        pthread_setname_np(name);
    }
    {
        Lock lock(gLock);
        waitForCpu(lock);
    }
    void* ret = h->entry(h->arg);
    OSExitThread(ret);
    return nullptr;
}

// Watchdog: if interrupts stay pending for long while a game thread runs code
// without reaching a preemption point, service them anyway so the game can't
// deadlock waiting on them. Logged so missing preemption points can be added.
void watchdogMain() {
    pthread_setname_np("OS interrupt watchdog");
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!gHasPending.load(std::memory_order_relaxed) || gHostWork.load() > 0) {
            continue;
        }
        const int64_t age = nowNs() - gOldestPendingNs.load(std::memory_order_relaxed);
        if (age < 200'000'000) {
            continue;
        }
        Lock lock(gLock);
        if (gPending.empty() || gInterruptActive) {
            continue;
        }
        HostThread* running = gRunning ? host(gRunning) : nullptr;
        if (running != nullptr && !running->interruptsEnabled) {
            continue;
        }
        PORT_WARN("os", "servicing interrupts {} ms late; running thread {} lacks a preemption point", age / 1000000,
                  (void*)gRunning);
        gForceInterrupts = true;
        gInterruptCv.notify_one();
    }
}

void interruptThreadMain() {
    pthread_setname_np("OS interrupts");
    Lock lock(gLock);
    for (;;) {
        gInterruptCv.wait(lock, [] {
            return !gPending.empty() && !gInterruptActive && (gRunning == nullptr || gForceInterrupts);
        });
        gForceInterrupts = false;
        runPendingLocked(lock);
    }
}

void* interruptThreadEntry(void*) {
    interruptThreadMain();
    return nullptr;
}

void startInterruptThread() {
    size_t size = 0;
    void* stack = port::mem::allocThreadStack(size);
    mprotect(stack, 16384, PROT_NONE);
    pthread_attr_t pa;
    pthread_attr_init(&pa);
    pthread_attr_setstack(&pa, stack, size);
    pthread_t t;
    pthread_create(&t, &pa, interruptThreadEntry, nullptr);
    pthread_attr_destroy(&pa);
}

}  // namespace

// ---------------------------------------------------------------------------
// Scheduler API
// ---------------------------------------------------------------------------

void initScheduler(OSThread* defaultThread, void* stackBase, void* stackEnd) {
    Lock lock(gLock);
    auto* h = new HostThread();
    h->thread = defaultThread;
    h->pthread = pthread_self();
    std::memset(defaultThread, 0, sizeof(OSThread));
    defaultThread->state = kRunning;
    defaultThread->attr = kAttrDetach;
    defaultThread->priority = defaultThread->base = 16;
    defaultThread->value = reinterpret_cast<void*>(-1);
    defaultThread->stackBase = static_cast<u8*>(stackBase);
    defaultThread->stackEnd = static_cast<u32*>(stackEnd);
    gHost[defaultThread] = h;
    gThreads.push_back(defaultThread);
    gRunning = defaultThread;
    tSelf = h;
    startInterruptThread();
    std::thread(watchdogMain).detach();
}

void postInterrupt(std::function<void()> handler) {
    Lock lock(gLock);
    if (gPending.empty()) {
        gOldestPendingNs.store(nowNs(), std::memory_order_relaxed);
    }
    gPending.push_back(std::move(handler));
    gHasPending = true;
    if (gRunning == nullptr && !gInterruptActive) {
        gInterruptCv.notify_one();
    }
}

void beginHostWork() { gHostWork++; }

void endHostWork() {
    gHostWork--;
    gOldestPendingNs.store(nowNs(), std::memory_order_relaxed);
    preemptPoint();
}

void preemptPoint() {
    if (!gHasPending.load(std::memory_order_relaxed)) {
        return;
    }
    if (tSelf == nullptr || tInInterrupt || !tSelf->interruptsEnabled) {
        return;
    }
    Lock lock(gLock);
    if (gRunning != tSelf->thread || gPending.empty()) {
        return;
    }
    runPendingLocked(lock);
    yieldIfNeeded(lock);
}

bool disableInterrupts() {
    bool& flag = tSelf ? tSelf->interruptsEnabled : tHostInterruptsEnabled;
    const bool prev = flag;
    flag = false;
    return prev;
}

bool restoreInterrupts(bool enable) {
    bool& flag = tSelf ? tSelf->interruptsEnabled : tHostInterruptsEnabled;
    const bool prev = flag;
    flag = enable;
    if (enable && !prev) {
        preemptPoint();
    }
    return prev;
}

OSThread* currentThread() { return tSelf ? tSelf->thread : nullptr; }

void sleepOnQueue(OSThreadQueue* queue) {
    Lock lock(gLock);
    sleepOnQueueLocked(lock, queue);
}

void wakeupQueue(OSThreadQueue* queue) {
    Lock lock(gLock);
    wakeupQueueLocked(queue);
    reschedule();
    yieldIfNeeded(lock);
}

void waitUntil(const std::function<bool()>& pred) {
    // Poll-style wait that keeps the game scheduler live. Used for host-side
    // conditions (e.g. waiting for the next VI retrace).
    for (;;) {
        {
            Lock lock(gLock);
            if (pred()) {
                return;
            }
        }
        preemptPoint();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

}  // namespace port::os

// ---------------------------------------------------------------------------
// OS thread API
// ---------------------------------------------------------------------------

using namespace port::os;

extern "C" {

void OSInitThreadQueue(OSThreadQueue* queue) { queue->head = queue->tail = nullptr; }

OSThread* OSGetCurrentThread(void) { return currentThread(); }

BOOL OSIsThreadSuspended(OSThread* thread) { return thread->suspend > 0; }

BOOL OSIsThreadTerminated(OSThread* thread) {
    return thread->state == kMoribund || thread->state == 0;
}

s32 OSDisableScheduler(void) {
    Lock lock(gLock);
    return gSchedulerSuspend++;
}

s32 OSEnableScheduler(void) {
    Lock lock(gLock);
    const s32 prev = gSchedulerSuspend--;
    if (gSchedulerSuspend <= 0) {
        gSchedulerSuspend = 0;
        reschedule();
        yieldIfNeeded(lock);
    }
    return prev;
}

// For game code that busy-waits on interrupt-driven state.
void PortPreemptPoint(void) { preemptPoint(); }

void OSYieldThread(void) {
    Lock lock(gLock);
    if (tSelf == nullptr) {
        return;
    }
    tSelf->yielding = true;
    tSelf->readySeq = ++gSeq;
    reschedule();
    yieldIfNeeded(lock);
}

BOOL OSCreateThread(OSThread* thread, void* (*func)(void*), void* param, void* stack, u32 stackSize,
                    OSPriority priority, u16 attr) {
    if (priority < 0 || priority > 31) {
        return FALSE;
    }
    Lock lock(gLock);
    reapZombies();
    std::memset(thread, 0, sizeof(OSThread));
    thread->state = kReady;
    thread->attr = attr & kAttrDetach;
    thread->priority = thread->base = priority;
    thread->suspend = 1;
    thread->value = reinterpret_cast<void*>(-1);
    thread->stackBase = static_cast<u8*>(stack);
    thread->stackEnd = reinterpret_cast<u32*>(static_cast<u8*>(stack) - stackSize);
    *thread->stackEnd = kStackMagic;

    auto* h = new HostThread();
    h->thread = thread;
    h->entry = func;
    h->arg = param;
    size_t hostStackSize = 0;
    h->stack = port::mem::allocThreadStack(hostStackSize);
    if (h->stack == nullptr) {
        PORT_FATAL("os", "out of game thread stacks");
        __builtin_trap();
    }
    // Guard page at the bottom of the host stack.
    mprotect(h->stack, 16384, PROT_NONE);
    gHost[thread] = h;
    gThreads.push_back(thread);

    pthread_attr_t pa;
    pthread_attr_init(&pa);
    pthread_attr_setstack(&pa, h->stack, hostStackSize);
    const int rc = pthread_create(&h->pthread, &pa, hostEntry, h);
    pthread_attr_destroy(&pa);
    if (rc != 0) {
        PORT_FATAL("os", "pthread_create failed ({})", rc);
        __builtin_trap();
    }
    return TRUE;
}

void OSExitThread(void* val) {
    Lock lock(gLock);
    OSThread* t = tSelf->thread;
    t->state = kMoribund;
    t->value = val;
    wakeupQueueLocked(&t->queueJoin);
    if (t->attr & kAttrDetach) {
        removeThread(t);
    }
    if (gRunning == t) {
        gRunning = nullptr;
    }
    reschedule();
    if (gRunning == nullptr && !gPending.empty()) {
        gInterruptCv.notify_one();
    }
    exitHostThread(lock);
}

void OSCancelThread(OSThread* thread) {
    if (thread == currentThread()) {
        OSExitThread(nullptr);
    }
    Lock lock(gLock);
    HostThread* h = host(thread);
    if (h == nullptr) {
        return;
    }
    if (thread->queue != nullptr) {
        queueRemove(thread->queue, thread);
        thread->queue = nullptr;
    }
    thread->state = kMoribund;
    wakeupQueueLocked(&thread->queueJoin);
    h->cancelled = true;
    h->cv.notify_all();
    if (thread->attr & kAttrDetach) {
        removeThread(thread);
    }
    reschedule();
    yieldIfNeeded(lock);
}

BOOL OSJoinThread(OSThread* thread, void** val) {
    Lock lock(gLock);
    if (thread->attr & kAttrDetach) {
        return FALSE;
    }
    while (thread->state != kMoribund) {
        sleepOnQueueLocked(lock, &thread->queueJoin);
    }
    if (val != nullptr) {
        *val = thread->value;
    }
    removeThread(thread);
    thread->state = 0;
    return TRUE;
}

void OSDetachThread(OSThread* thread) {
    Lock lock(gLock);
    thread->attr |= kAttrDetach;
    if (thread->state == kMoribund) {
        removeThread(thread);
        thread->state = 0;
    }
}

s32 OSResumeThread(OSThread* thread) {
    Lock lock(gLock);
    const s32 prev = thread->suspend--;
    if (thread->suspend < 0) {
        thread->suspend = 0;
    } else if (thread->suspend == 0) {
        HostThread* h = host(thread);
        if (h != nullptr) {
            h->readySeq = ++gSeq;
        }
        reschedule();
        yieldIfNeeded(lock);
    }
    return prev;
}

s32 OSSuspendThread(OSThread* thread) {
    Lock lock(gLock);
    const s32 prev = thread->suspend++;
    if (prev == 0) {
        reschedule();
        yieldIfNeeded(lock);
    }
    return prev;
}

void OSSleepThread(OSThreadQueue* queue) { sleepOnQueue(queue); }

void OSWakeupThread(OSThreadQueue* queue) { wakeupQueue(queue); }

BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority) {
    if (priority < 0 || priority > 31) {
        return FALSE;
    }
    Lock lock(gLock);
    thread->base = thread->priority = priority;
    if (thread->queue != nullptr) {
        OSThreadQueue* q = thread->queue;
        queueRemove(q, thread);
        queueAddPrio(q, thread);
    }
    reschedule();
    yieldIfNeeded(lock);
    return TRUE;
}

OSPriority OSGetThreadPriority(OSThread* thread) { return thread->priority; }

BOOL OSDisableInterrupts(void) { return disableInterrupts(); }

BOOL OSEnableInterrupts(void) { return restoreInterrupts(true); }

BOOL OSRestoreInterrupts(BOOL level) { return restoreInterrupts(level != 0); }

}  // extern "C"
