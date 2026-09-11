#include "logger.hpp"
#include <chrono>
#include <ctime>

namespace nemu::platform {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

static constexpr const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "UNK  ";
    }
}

void Logger::Log(LogLevel level, std::string_view channel, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
        return;
    }

    std::lock_guard lock(log_mutex_);
    const auto now = std::chrono::system_clock::now();
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    std::cout << "[" << time_str << "][" << LevelToString(level) << "][" << channel << "] " 
              << message << "\n" << std::flush;
}

} // namespace nemu::platform
