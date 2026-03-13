#pragma once

namespace Log {

enum class LogLevel : u8 {
    Fatal = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
};

internal const char* LOG_LEVEL_COLORS[] = {
    "\033[1;41m",
    "\033[1;31m",
    "\033[1;33m",
    "\033[1;32m",
    "\033[1;36m",
    "\033[0;90m",
};

internal const char* LOG_LEVEL_TAGS[] = {
    "[FATAL]",
    "[ERROR]",
    "[WARN]",
    "[INFO]",
    "[DEBUG]",
    "[TRACE]",
};

internal const char* LOG_COLOR_RESET = "\033[0m";

inline void WriteV(
    LogLevel level,
    const char* file,
    int line,
    const char* fmt,
    va_list args
) __attribute__((format(printf, 4, 0)));

inline void Write(
    LogLevel level,
    const char* file,
    int line,
    const char* fmt,
    ...
) __attribute__((format(printf, 4, 5)));

inline void
WriteV(LogLevel level, const char* file, int line, const char* fmt, va_list args) {
    char msg[16384];
    va_list args_copy;
    va_copy(args_copy, args);
    (void)vsnprintf(msg, sizeof(msg), fmt, args_copy);
    va_end(args_copy);

    char out[16384];
    usize level_index = (usize)level;
    (void)snprintf(
        out,
        sizeof(out),
        "%s%s %s:%d: %s%s\n",
        LOG_LEVEL_COLORS[level_index],
        LOG_LEVEL_TAGS[level_index],
        file,
        line,
        msg,
        LOG_COLOR_RESET
    );

    FILE* stream = level_index <= (usize)LogLevel::Error ? stderr : stdout;
    (void)fputs(out, stream);
}

inline void Write(
    LogLevel level,
    const char* file,
    int line,
    const char* fmt,
    ...
) {
    va_list args;
    va_start(args, fmt);
    WriteV(level, file, line, fmt, args);
    va_end(args);
}

}

#define LOG_FATAL(...) Log::Write(Log::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Log::Write(Log::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) Log::Write(Log::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Log::Write(Log::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)

#ifndef NDEBUG
#define LOG_DEBUG(...) Log::Write(Log::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_TRACE(...) Log::Write(Log::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#define LOG_TRACE(...) ((void)0)
#endif
