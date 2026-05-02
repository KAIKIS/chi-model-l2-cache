#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

namespace chi {

// Log levels
enum class LogLevel {
    NONE = 0,    // No logging
    ERROR = 1,   // Errors only
    WARN = 2,    // Warnings
    INFO = 3,    // Info (default)
    DEBUG = 4,   // Debug
    TRACE = 5    // Verbose trace
};

// Global log level - can be changed at runtime
// Initialized from CHI_LOG_LEVEL environment variable (default: INFO)
inline LogLevel& globalLogLevel() {
    static LogLevel level = []() {
        const char* env = std::getenv("CHI_LOG_LEVEL");
        if (env) {
            if (strcmp(env, "NONE") == 0 || strcmp(env, "0") == 0) return LogLevel::NONE;
            if (strcmp(env, "ERROR") == 0 || strcmp(env, "1") == 0) return LogLevel::ERROR;
            if (strcmp(env, "WARN") == 0 || strcmp(env, "2") == 0) return LogLevel::WARN;
            if (strcmp(env, "INFO") == 0 || strcmp(env, "3") == 0) return LogLevel::INFO;
            if (strcmp(env, "DEBUG") == 0 || strcmp(env, "4") == 0) return LogLevel::DEBUG;
            if (strcmp(env, "TRACE") == 0 || strcmp(env, "5") == 0) return LogLevel::TRACE;
        }
        return LogLevel::INFO;
    }();
    return level;
}

// Set log level
inline void setLogLevel(LogLevel level) {
    globalLogLevel() = level;
}

// Get current log level
inline LogLevel getLogLevel() {
    return globalLogLevel();
}

// Log function
inline void log(LogLevel level, const char* fmt, ...) {
    if (level <= globalLogLevel()) {
        const char* prefix = "";
        switch (level) {
            case LogLevel::ERROR: prefix = "[CHI ERROR] "; break;
            case LogLevel::WARN:  prefix = "[CHI WARN]  "; break;
            case LogLevel::INFO:  prefix = "[CHI INFO]  "; break;
            case LogLevel::DEBUG: prefix = "[CHI DEBUG] "; break;
            case LogLevel::TRACE: prefix = "[CHI TRACE] "; break;
            default: break;
        }
        fprintf(stderr, "%s", prefix);
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
}

// Convenience macros
#define CHI_LOG_ERROR(...) chi::log(chi::LogLevel::ERROR, __VA_ARGS__)
#define CHI_LOG_WARN(...)  chi::log(chi::LogLevel::WARN, __VA_ARGS__)
#define CHI_LOG_INFO(...)  chi::log(chi::LogLevel::INFO, __VA_ARGS__)
#define CHI_LOG_DEBUG(...) chi::log(chi::LogLevel::DEBUG, __VA_ARGS__)
#define CHI_LOG_TRACE(...) chi::log(chi::LogLevel::TRACE, __VA_ARGS__)

} // namespace chi
