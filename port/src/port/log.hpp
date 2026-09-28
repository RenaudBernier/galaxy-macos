// Minimal logging for the port layer.
#pragma once

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>

namespace port::log {

enum class Level { Debug, Info, Warn, Error, Fatal };

void write(Level level, const char* module, const std::string& message);
void setMinLevel(Level level);
Level minLevel();

}  // namespace port::log

#define PORT_LOG_IMPL(lvl, module, ...)                                                                                \
    do {                                                                                                               \
        if ((lvl) >= ::port::log::minLevel()) {                                                                        \
            ::port::log::write((lvl), (module), ::fmt::format(__VA_ARGS__));                                           \
        }                                                                                                              \
    } while (0)

#define PORT_DEBUG(module, ...) PORT_LOG_IMPL(::port::log::Level::Debug, module, __VA_ARGS__)
#define PORT_INFO(module, ...) PORT_LOG_IMPL(::port::log::Level::Info, module, __VA_ARGS__)
#define PORT_WARN(module, ...) PORT_LOG_IMPL(::port::log::Level::Warn, module, __VA_ARGS__)
#define PORT_ERROR(module, ...) PORT_LOG_IMPL(::port::log::Level::Error, module, __VA_ARGS__)
#define PORT_FATAL(module, ...) PORT_LOG_IMPL(::port::log::Level::Fatal, module, __VA_ARGS__)
