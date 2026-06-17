#pragma once

#include <string>
#include <memory>

namespace seismograph {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5
};

class Logger {
public:
    static bool init(const std::string& log_dir = "logs",
                     const std::string& app_name = "seismograph",
                     LogLevel console_level = LogLevel::INFO,
                     LogLevel file_level = LogLevel::DEBUG,
                     bool enable_rotate = true,
                     size_t max_file_size_mb = 50,
                     size_t max_files = 10);

    static void set_level(LogLevel level);
    static LogLevel get_level();
    static void flush();

    static void log(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...);

    static bool is_initialized();
};

}

#ifdef SEISMOGRAPH_USE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/ostr.h>

#define LOG_TRACE(...)    ::seismograph::Logger::log(::seismograph::LogLevel::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(...)    ::seismograph::Logger::log(::seismograph::LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)     ::seismograph::Logger::log(::seismograph::LogLevel::INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)     ::seismograph::Logger::log(::seismograph::LogLevel::WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...)    ::seismograph::Logger::log(::seismograph::LogLevel::ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_CRITICAL(...) ::seismograph::Logger::log(::seismograph::LogLevel::CRITICAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

#else

#include <iostream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <cstdarg>
#include <cstdio>

#define LOG_TRACE(...)    ::seismograph::Logger::log(::seismograph::LogLevel::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(...)    ::seismograph::Logger::log(::seismograph::LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)     ::seismograph::Logger::log(::seismograph::LogLevel::INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)     ::seismograph::Logger::log(::seismograph::LogLevel::WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...)    ::seismograph::Logger::log(::seismograph::LogLevel::ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_CRITICAL(...) ::seismograph::Logger::log(::seismograph::LogLevel::CRITICAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif
