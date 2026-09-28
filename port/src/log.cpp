#include "port/log.hpp"

#include <atomic>
#include <mutex>

namespace port::log {
namespace {
std::atomic<Level> sMinLevel{Level::Info};
std::mutex sMutex;
const char* kNames[] = {"debug", "info", "warn", "error", "FATAL"};
}  // namespace

void setMinLevel(Level level) { sMinLevel = level; }
Level minLevel() { return sMinLevel; }

void write(Level level, const char* module, const std::string& message) {
    std::lock_guard lock(sMutex);
    fprintf(stderr, "[%s %s] %s\n", kNames[static_cast<int>(level)], module, message.c_str());
    fflush(stderr);
}

}  // namespace port::log
