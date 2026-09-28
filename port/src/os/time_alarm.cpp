// OS time base and alarms.
//
// OSGetTime counts timer ticks (OS_TIMER_CLOCK = 60.75 MHz on the Wii) since
// 2000-01-01, like the Wii's time base after boot. Alarms are kept in a host
// timer thread and delivered as interrupts through the scheduler.

#include "os/scheduler.hpp"
#include "port/log.hpp"

#include <pthread.h>

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

using namespace port::os;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int64_t kTicksPerSec = OS_TIMER_CLOCK;
constexpr int64_t kUnixTo2000 = 946684800;

Clock::time_point sBootSteady;
int64_t sBootTicks = 0;  // ticks since 2000 at boot
std::once_flag sInitOnce;

void initTimeBase() {
    std::call_once(sInitOnce, [] {
        sBootSteady = Clock::now();
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        const int64_t secs = ts.tv_sec - kUnixTo2000;
        sBootTicks = secs * kTicksPerSec + (int64_t)ts.tv_nsec * kTicksPerSec / 1000000000;
    });
}

int64_t ticksNow() {
    initTimeBase();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - sBootSteady).count();
    return sBootTicks + (int64_t)((__int128)ns * kTicksPerSec / 1000000000);
}

Clock::time_point ticksToSteady(int64_t ticks) {
    const int64_t delta = ticks - sBootTicks;
    return sBootSteady + std::chrono::nanoseconds((int64_t)((__int128)delta * 1000000000 / kTicksPerSec));
}

// --- Alarms -----------------------------------------------------------------

struct AlarmEntry {
    OSAlarmHandler handler;
    uint64_t generation;
};

std::mutex sAlarmLock;
std::condition_variable sAlarmCv;
std::multimap<int64_t, OSAlarm*> sQueue;  // fire tick -> alarm
std::unordered_map<OSAlarm*, uint64_t> sGeneration;
bool sThreadStarted = false;

void removeFromQueue(OSAlarm* alarm) {
    for (auto it = sQueue.begin(); it != sQueue.end(); ++it) {
        if (it->second == alarm) {
            sQueue.erase(it);
            return;
        }
    }
}

void alarmThreadMain() {
    pthread_setname_np("OS alarm timer");
    std::unique_lock lock(sAlarmLock);
    for (;;) {
        if (sQueue.empty()) {
            sAlarmCv.wait(lock);
            continue;
        }
        auto it = sQueue.begin();
        const int64_t fire = it->first;
        if (ticksNow() < fire) {
            sAlarmCv.wait_until(lock, ticksToSteady(fire));
            continue;
        }
        OSAlarm* alarm = it->second;
        sQueue.erase(it);
        const uint64_t gen = sGeneration[alarm];
        OSAlarmHandler handler = alarm->handler;
        if (alarm->period > 0) {
            // Next occurrence relative to the start, as the SDK does.
            int64_t next = fire + alarm->period;
            const int64_t now = ticksNow();
            if (next <= now) {
                next = now + alarm->period - (now - alarm->start) % alarm->period;
            }
            alarm->fire = next;
            sQueue.emplace(next, alarm);
        }
        lock.unlock();
        postInterrupt([alarm, handler, gen] {
            {
                std::lock_guard g(sAlarmLock);
                auto git = sGeneration.find(alarm);
                if (git == sGeneration.end() || git->second != gen) {
                    return;  // cancelled or re-armed after being queued
                }
                if (alarm->period <= 0) {
                    alarm->handler = nullptr;
                }
            }
            if (handler != nullptr) {
                handler(alarm, OSGetCurrentContext());
            }
        });
        lock.lock();
    }
}

void armLocked(OSAlarm* alarm, int64_t fire, OSAlarmHandler handler) {
    if (!sThreadStarted) {
        sThreadStarted = true;
        std::thread(alarmThreadMain).detach();
    }
    removeFromQueue(alarm);
    sGeneration[alarm]++;
    alarm->handler = handler;
    alarm->fire = fire;
    sQueue.emplace(fire, alarm);
    sAlarmCv.notify_all();
}

}  // namespace

extern "C" {

OSTime OSGetTime(void) {
    preemptPoint();
    return ticksNow();
}

OSTick OSGetTick(void) {
    preemptPoint();
    return (OSTick)ticksNow();
}

OSTime __OSGetSystemTime(void) { return ticksNow(); }

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
    int64_t secs = ticks / kTicksPerSec;
    int64_t rem = ticks % kTicksPerSec;
    if (rem < 0) {
        rem += kTicksPerSec;
        secs--;
    }
    const time_t t = (time_t)(secs + kUnixTo2000);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    td->sec = tmv.tm_sec;
    td->min = tmv.tm_min;
    td->hour = tmv.tm_hour;
    td->mday = tmv.tm_mday;
    td->mon = tmv.tm_mon;
    td->year = tmv.tm_year + 1900;
    td->wday = tmv.tm_wday;
    td->yday = tmv.tm_yday;
    const int64_t usecTotal = rem * 1000000 / kTicksPerSec;
    td->msec = (int)(usecTotal / 1000);
    td->usec = (int)(usecTotal % 1000);
}

void OSCreateAlarm(OSAlarm* alarm) {
    std::lock_guard g(sAlarmLock);
    alarm->handler = nullptr;
    alarm->tag = 0;
    alarm->period = 0;
    alarm->start = 0;
    alarm->fire = 0;
    alarm->prev = alarm->next = nullptr;
    removeFromQueue(alarm);
    sGeneration[alarm]++;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
    std::lock_guard g(sAlarmLock);
    alarm->period = 0;
    armLocked(alarm, ticksNow() + tick, handler);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
    std::lock_guard g(sAlarmLock);
    alarm->period = period;
    alarm->start = start;
    const int64_t now = ticksNow();
    int64_t fire = start;
    if (fire <= now && period > 0) {
        fire = now + period - (now - start) % period;
    }
    armLocked(alarm, fire, handler);
}

void OSCancelAlarm(OSAlarm* alarm) {
    std::lock_guard g(sAlarmLock);
    removeFromQueue(alarm);
    sGeneration[alarm]++;
    alarm->handler = nullptr;
}

void OSSetAlarmTag(OSAlarm* alarm, u32 tag) { alarm->tag = tag; }

void OSSetAlarmUserData(OSAlarm* alarm, void* data) { alarm->userData = data; }

void* OSGetAlarmUserData(const OSAlarm* alarm) { return alarm->userData; }

void OSSleepTicks(OSTime ticks) {
    OSThreadQueue queue;
    OSInitThreadQueue(&queue);
    OSAlarm alarm;
    OSCreateAlarm(&alarm);
    OSSetAlarmUserData(&alarm, &queue);
    OSSetAlarm(&alarm, ticks, [](OSAlarm* a, OSContext*) {
        auto* q = static_cast<OSThreadQueue*>(OSGetAlarmUserData(a));
        OSWakeupThread(q);
    });
    OSSleepThread(&queue);
    OSCancelAlarm(&alarm);
}

}  // extern "C"
