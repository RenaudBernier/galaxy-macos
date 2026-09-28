// OSMutex, OSCond and OSMessageQueue on top of the uniprocessor scheduler.
// Only one game thread runs at a time and interrupt handlers only run at
// preemption points, so these follow the Wii OS algorithms directly.

#include "os/scheduler.hpp"

using namespace port::os;

namespace {

void mutexListAdd(OSThread* t, OSMutex* m) {
    OSMutex* prev = t->queueMutex.tail;
    if (prev == nullptr) {
        t->queueMutex.head = m;
    } else {
        prev->link.next = m;
    }
    m->link.prev = prev;
    m->link.next = nullptr;
    t->queueMutex.tail = m;
}

void mutexListRemove(OSThread* t, OSMutex* m) {
    OSMutex* next = m->link.next;
    OSMutex* prev = m->link.prev;
    if (next == nullptr) {
        t->queueMutex.tail = prev;
    } else {
        next->link.prev = prev;
    }
    if (prev == nullptr) {
        t->queueMutex.head = next;
    } else {
        prev->link.next = next;
    }
    m->link.next = m->link.prev = nullptr;
}

}  // namespace

extern "C" {

void OSInitMutex(OSMutex* mutex) {
    OSInitThreadQueue(&mutex->queue);
    mutex->thread = nullptr;
    mutex->count = 0;
    mutex->link.next = mutex->link.prev = nullptr;
}

void OSLockMutex(OSMutex* mutex) {
    OSThread* cur = currentThread();
    for (;;) {
        if (mutex->thread == nullptr) {
            mutex->thread = cur;
            mutex->count++;
            if (cur != nullptr) {
                mutexListAdd(cur, mutex);
            }
            return;
        }
        if (mutex->thread == cur) {
            mutex->count++;
            return;
        }
        cur->mutex = mutex;
        sleepOnQueue(&mutex->queue);
        cur->mutex = nullptr;
    }
}

BOOL OSTryLockMutex(OSMutex* mutex) {
    OSThread* cur = currentThread();
    if (mutex->thread == nullptr) {
        mutex->thread = cur;
        mutex->count++;
        if (cur != nullptr) {
            mutexListAdd(cur, mutex);
        }
        return TRUE;
    }
    if (mutex->thread == cur) {
        mutex->count++;
        return TRUE;
    }
    return FALSE;
}

void OSUnlockMutex(OSMutex* mutex) {
    OSThread* cur = currentThread();
    if (mutex->thread != cur || mutex->count <= 0) {
        return;
    }
    if (--mutex->count == 0) {
        if (cur != nullptr) {
            mutexListRemove(cur, mutex);
        }
        mutex->thread = nullptr;
        wakeupQueue(&mutex->queue);
    }
}

void OSInitCond(OSCond* cond) { OSInitThreadQueue(&cond->queue); }

void OSWaitCond(OSCond* cond, OSMutex* mutex) {
    OSThread* cur = currentThread();
    if (mutex->thread != cur) {
        return;
    }
    const s32 count = mutex->count;
    mutex->count = 0;
    mutexListRemove(cur, mutex);
    mutex->thread = nullptr;
    wakeupQueue(&mutex->queue);
    sleepOnQueue(&cond->queue);
    OSLockMutex(mutex);
    mutex->count = count;
}

void OSSignalCond(OSCond* cond) { wakeupQueue(&cond->queue); }

void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* msgArray, s32 msgCount) {
    OSInitThreadQueue(&mq->queueSend);
    OSInitThreadQueue(&mq->queueReceive);
    mq->msgArray = msgArray;
    mq->msgCount = msgCount;
    mq->firstIndex = 0;
    mq->usedCount = 0;
}

BOOL OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
    while (mq->msgCount <= mq->usedCount) {
        if (!(flags & OS_MESSAGE_BLOCK)) {
            return FALSE;
        }
        sleepOnQueue(&mq->queueSend);
    }
    const s32 last = (mq->firstIndex + mq->usedCount) % mq->msgCount;
    mq->msgArray[last] = msg;
    mq->usedCount++;
    wakeupQueue(&mq->queueReceive);
    return TRUE;
}

BOOL OSJamMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
    while (mq->msgCount <= mq->usedCount) {
        if (!(flags & OS_MESSAGE_BLOCK)) {
            return FALSE;
        }
        sleepOnQueue(&mq->queueSend);
    }
    mq->firstIndex = (mq->firstIndex + mq->msgCount - 1) % mq->msgCount;
    mq->msgArray[mq->firstIndex] = msg;
    mq->usedCount++;
    wakeupQueue(&mq->queueReceive);
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, s32 flags) {
    while (mq->usedCount == 0) {
        if (!(flags & OS_MESSAGE_BLOCK)) {
            return FALSE;
        }
        sleepOnQueue(&mq->queueReceive);
    }
    if (msg != nullptr) {
        *msg = mq->msgArray[mq->firstIndex];
    }
    mq->firstIndex = (mq->firstIndex + 1) % mq->msgCount;
    mq->usedCount--;
    wakeupQueue(&mq->queueSend);
    return TRUE;
}

}  // extern "C"
