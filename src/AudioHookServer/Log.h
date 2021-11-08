#pragma once
#include <format>

enum LogLevel
{
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_COUNT
};

extern LogLevel LogMinLevel;

void WriteLogRaw(std::string_view msg);

//Exclude comma with zero args (https://stackoverflow.com/a/5897216)
#define VA_ARGS(...) , ##__VA_ARGS__
#define LogTrace(fmt, ...) WriteLog(LOG_TRACE, fmt VA_ARGS(__VA_ARGS__))
#define LogDebug(fmt, ...) WriteLog(LOG_DEBUG, fmt VA_ARGS(__VA_ARGS__))
#define LogInfo(fmt, ...)  WriteLog(LOG_INFO,  fmt VA_ARGS(__VA_ARGS__))
#define LogWarn(fmt, ...)  WriteLog(LOG_WARN,  fmt VA_ARGS(__VA_ARGS__))
#define LogError(fmt, ...) WriteLog(LOG_ERROR, fmt VA_ARGS(__VA_ARGS__))

template <typename... Args>
void WriteLog(LogLevel level, std::string_view fmt, Args&&... args)
{
    constexpr const char* labels[] = { "[trc] ", "[dbg] ", "[inf] ", "[wrn] ", "[err] " };

    if (level < LogMinLevel || level >= LOG_COUNT) return;

    std::string str = std::vformat(fmt, std::make_format_args(args...));
    WriteLogRaw(labels[level] + str + "\n");
}