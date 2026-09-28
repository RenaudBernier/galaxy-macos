// OS arenas, reporting and panics.

#include "os/scheduler.hpp"
#include "port/log.hpp"
#include "port/memory.hpp"

#include <iconv.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace port::os {

namespace {
void* sArenaLo = nullptr;
void* sArenaHi = nullptr;
void* sMem2ArenaLo = nullptr;
void* sMem2ArenaHi = nullptr;
bool sReportEnabled = true;
}  // namespace

void initArenas() {
    const auto& l = port::mem::layout();
    sArenaLo = reinterpret_cast<void*>(l.mem1Start);
    sArenaHi = reinterpret_cast<void*>(l.mem1End);
    sMem2ArenaLo = reinterpret_cast<void*>(l.mem2Start);
    sMem2ArenaHi = reinterpret_cast<void*>(l.mem2End);
    sReportEnabled = getenv("SMG_QUIET") == nullptr;
}

}  // namespace port::os

using namespace port::os;

extern "C" {

void* OSGetArenaLo(void) { return sArenaLo; }
void* OSGetArenaHi(void) { return sArenaHi; }
void OSSetArenaLo(void* lo) { sArenaLo = lo; }
void OSSetArenaHi(void* hi) { sArenaHi = hi; }
void* OSGetMEM1ArenaLo(void) { return sArenaLo; }
void* OSGetMEM1ArenaHi(void) { return sArenaHi; }
void* OSGetMEM2ArenaLo(void) { return sMem2ArenaLo; }
void* OSGetMEM2ArenaHi(void) { return sMem2ArenaHi; }
void OSSetMEM2ArenaLo(void* lo) { sMem2ArenaLo = lo; }
void OSSetMEM2ArenaHi(void* hi) { sMem2ArenaHi = hi; }

// OS heap bookkeeping: only the descriptor reservation is used by the game.
void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps) {
    (void)arenaEnd;
    const uintptr_t descBytes = (uintptr_t)maxHeaps * 12;
    const uintptr_t start = ((uintptr_t)arenaStart + descBytes + 31) & ~(uintptr_t)31;
    return reinterpret_cast<void*>(start);
}

void OSVReport(const char* fmt, va_list args) {
    if (!sReportEnabled) {
        return;
    }
    // The game's messages are Shift-JIS; print them as UTF-8.
    char buf[2048];
    const int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len <= 0) {
        return;
    }
    const size_t n = std::min<size_t>(static_cast<size_t>(len), sizeof(buf) - 1);
    bool ascii = true;
    for (size_t i = 0; i < n && ascii; i++) {
        ascii = static_cast<unsigned char>(buf[i]) < 0x80;
    }
    if (!ascii) {
        static iconv_t sConv = iconv_open("UTF-8", "CP932");
        if (sConv != reinterpret_cast<iconv_t>(-1)) {
            char out[sizeof(buf) * 3];
            char* in = buf;
            size_t inLeft = n;
            char* dst = out;
            size_t outLeft = sizeof(out);
            iconv(sConv, nullptr, nullptr, nullptr, nullptr);
            if (iconv(sConv, &in, &inLeft, &dst, &outLeft) != static_cast<size_t>(-1)) {
                fwrite(out, 1, sizeof(out) - outLeft, stderr);
                return;
            }
        }
    }
    fwrite(buf, 1, n, stderr);
}

void OSReport(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    OSVReport(fmt, args);
    va_end(args);
}

void OSPanic(const char* file, int line, const char* fmt, ...) {
    fprintf(stderr, "\n*** OSPanic at %s:%d: ", file, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
    __builtin_trap();
}

}  // extern "C"
