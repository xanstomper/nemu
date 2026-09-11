#pragma once

#include <string_view>
#include <format>
#include <iostream>
#include <mutex>

namespace nemu::platform {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    static Logger& Instance();

    void Log(LogLevel level, std::string_view channel, std::string_view message);

    template <typename... Args>
    void LogFormat(LogLevel level, std::string_view channel, std::format_string<Args...> fmt, Args&&... args) {
        std::string message = std::format(fmt, std::forward<Args>(args)...);
        Log(level, channel, message);
    }

    void SetMinLevel(LogLevel level) { min_level_ = level; }

private:
    Logger() = default;
    LogLevel min_level_{LogLevel::Info};
    std::mutex log_mutex_;
};

#define NEMU_LOG_TRACE(channel, ...) ::nemu::platform::Logger::Instance().LogFormat(::nemu::platform::LogLevel::Trace, channel, __VA_ARGS__)
#define NEMU_LOG_DEBUG(channel, ...) ::nemu::platform::Logger::Instance().LogFormat(::nemu::platform::LogLevel::Debug, channel, __VA_ARGS__)
#define NEMU_LOG_INFO(channel, ...)  ::nemu::platform::Logger::Instance().LogFormat(::nemu::platform::LogLevel::Info, channel, __VA_ARGS__)
#define NEMU_LOG_WARN(channel, ...)  ::nemu::platform::Logger::Instance().LogFormat(::nemu::platform::LogLevel::Warn, channel, __VA_ARGS__)
#define NEMU_LOG_ERROR(channel, ...) ::nemu::platform::Logger::Instance().LogFormat(::nemu::platform::LogLevel::Error, channel, __VA_ARGS__)
#define NEMU_LOG_FATAL(channel, ...) ::nemu::platform::Logger::Instance().LogFormat(::nemu::platform::LogLevel::Fatal, channel, __VA_ARGS__)

} // namespace nemu::platform
