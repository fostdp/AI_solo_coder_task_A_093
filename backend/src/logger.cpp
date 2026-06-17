#include "logger.h"

#ifdef SEISMOGRAPH_USE_SPDLOG

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/ostr.h>

#include <iostream>
#include <memory>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace seismograph {

namespace {

struct LoggerState {
    bool initialized = false;
    std::mutex mutex;
    std::shared_ptr<spdlog::logger> logger;
    LogLevel current_level = LogLevel::INFO;
};

LoggerState& state() {
    static LoggerState s;
    return s;
}

spdlog::level::level_enum to_spdlog_level(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return spdlog::level::trace;
        case LogLevel::DEBUG:    return spdlog::level::debug;
        case LogLevel::INFO:     return spdlog::level::info;
        case LogLevel::WARN:     return spdlog::level::warn;
        case LogLevel::ERROR:    return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        default:                 return spdlog::level::info;
    }
}

}

bool Logger::init(const std::string& log_dir, const std::string& app_name,
                   LogLevel console_level, LogLevel file_level,
                   bool enable_rotate, size_t max_file_size_mb, size_t max_files) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.initialized) return true;

    try {
        std::vector<spdlog::sink_ptr> sinks;

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(to_spdlog_level(console_level));
        sinks.push_back(console_sink);

        std::string log_file = log_dir + "/" + app_name + ".log";
        if (enable_rotate) {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file,
                max_file_size_mb * 1024 * 1024,
                max_files
            );
            file_sink->set_level(to_spdlog_level(file_level));
            sinks.push_back(file_sink);
        } else {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
            file_sink->set_level(to_spdlog_level(file_level));
            sinks.push_back(file_sink);
        }

        s.logger = std::make_shared<spdlog::logger>(app_name, sinks.begin(), sinks.end());
        s.logger->set_level(spdlog::level::trace);
        s.logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%s:%#] %v");
        s.logger->flush_on(spdlog::level::err);

        spdlog::register_logger(s.logger);

        s.current_level = console_level;
        s.initialized = true;

        s.logger->info("Logger initialized (spdlog)");
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger init failed (spdlog): " << ex.what() << std::endl;
        return false;
    }
}

void Logger::set_level(LogLevel level) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.logger) {
        s.logger->set_level(to_spdlog_level(level));
        s.current_level = level;
    }
}

LogLevel Logger::get_level() {
    return state().current_level;
}

void Logger::flush() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.logger) {
        s.logger->flush();
    }
    spdlog::default_logger_raw()->flush();
}

bool Logger::is_initialized() {
    return state().initialized;
}

void Logger::log(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...) {
    auto& s = state();

    char msg_buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    if (!s.initialized) {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.initialized) {
            auto& out = (level >= LogLevel::ERROR) ? std::cerr : std::cout;
            out << "[" << static_cast<int>(level) << "] " << msg_buf << std::endl;
            return;
        }
    }

    spdlog::source_loc loc{file, line, func};
    auto sp_level = to_spdlog_level(level);

    if (s.logger) {
        s.logger->log(loc, sp_level, "{}", msg_buf);
    }
}

}

#else

#include <iostream>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <deque>
#include <algorithm>

namespace seismograph {

namespace {

struct LoggerState {
    bool initialized = false;
    LogLevel console_level = LogLevel::INFO;
    LogLevel file_level = LogLevel::DEBUG;
    std::mutex mutex;
    std::ofstream file_stream;
    std::string log_file_path;
    size_t max_file_size = 50 * 1024 * 1024;
    size_t max_files = 10;
    bool enable_rotate = true;
    size_t current_file_size = 0;
    std::deque<std::string> log_files;
    std::string app_name = "seismograph";
};

LoggerState& state() {
    static LoggerState s;
    return s;
}

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO ";
        case LogLevel::WARN:     return "WARN ";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT ";
        default:                 return "?????";
    }
}

const char* level_color(LogLevel level) {
#if defined(_WIN32) && !defined(ENABLE_VIRTUAL_TERMINAL)
    return "";
#else
    switch (level) {
        case LogLevel::TRACE:    return "\033[37m";
        case LogLevel::DEBUG:    return "\033[36m";
        case LogLevel::INFO:     return "\033[32m";
        case LogLevel::WARN:     return "\033[33m";
        case LogLevel::ERROR:    return "\033[31m";
        case LogLevel::CRITICAL: return "\033[35m";
        default:                 return "\033[0m";
    }
#endif
}

const char* level_color_reset() {
#if defined(_WIN32) && !defined(ENABLE_VIRTUAL_TERMINAL)
    return "";
#else
    return "\033[0m";
#endif
}

std::string format_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void rotate_file_if_needed() {
    auto& s = state();
    if (!s.enable_rotate || !s.file_stream.is_open()) return;
    if (s.current_file_size < s.max_file_size) return;

    s.file_stream.close();

    std::deque<std::string> new_files;
    new_files.push_back(s.log_file_path);

    for (size_t i = 0; i < s.log_files.size() && i < s.max_files - 1; ++i) {
        std::string old_path = s.log_files[i];
        std::string new_path = s.log_file_path + "." + std::to_string(i + 1);
        std::error_code ec;
        std::filesystem::rename(old_path, new_path, ec);
        if (!ec) new_files.push_back(new_path);
    }

    if (s.log_files.size() >= s.max_files) {
        for (size_t i = s.max_files - 1; i < s.log_files.size(); ++i) {
            std::error_code ec;
            std::filesystem::remove(s.log_files[i], ec);
        }
    }

    s.log_files = new_files;

    s.file_stream.open(s.log_file_path, std::ios::out | std::ios::trunc);
    s.current_file_size = 0;
}

}

bool Logger::init(const std::string& log_dir, const std::string& app_name,
                   LogLevel console_level, LogLevel file_level,
                   bool enable_rotate, size_t max_file_size_mb, size_t max_files) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (s.initialized) return true;

    s.app_name = app_name;
    s.console_level = console_level;
    s.file_level = file_level;
    s.enable_rotate = enable_rotate;
    s.max_file_size = max_file_size_mb * 1024 * 1024;
    s.max_files = max_files;

    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);

    s.log_file_path = log_dir + "/" + app_name + ".log";
    s.file_stream.open(s.log_file_path, std::ios::out | std::ios::app);
    if (!s.file_stream.is_open()) {
        std::cerr << "[Logger] Failed to open log file: " << s.log_file_path << std::endl;
    }
    s.current_file_size = std::filesystem::file_size(s.log_file_path, ec);
    if (ec) s.current_file_size = 0;

    s.initialized = true;

    log(LogLevel::INFO, __FILE__, __LINE__, __func__,
        "Logger initialized (console_level=%d, file_level=%d, file=%s)",
        static_cast<int>(console_level), static_cast<int>(file_level), s.log_file_path.c_str());

    return true;
}

void Logger::set_level(LogLevel level) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.console_level = level;
    s.file_level = level;
}

LogLevel Logger::get_level() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.console_level;
}

void Logger::flush() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.file_stream.is_open()) s.file_stream.flush();
    std::cout.flush();
    std::cerr.flush();
}

bool Logger::is_initialized() {
    return state().initialized;
}

void Logger::log(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...) {
    auto& s = state();

    if (!s.initialized) {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.initialized) {
            va_list args;
            va_start(args, fmt);
            char buf[1024];
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            std::cout << "[" << level_name(level) << "] " << buf << std::endl;
            return;
        }
    }

    if (level < s.console_level && level < s.file_level) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    char msg_buf[2048];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    const char* basename = file;
    const char* slash = strrchr(file, '/');
    if (slash) basename = slash + 1;
    const char* bslash = strrchr(file, '\\');
    if (bslash) basename = bslash + 1;

    std::ostringstream line_ss;
    line_ss << "[" << format_timestamp() << "]"
            << "[" << level_name(level) << "]"
            << "[" << basename << ":" << line << "]"
            << " " << msg_buf;
    std::string line = line_ss.str();

    std::lock_guard<std::mutex> lock(s.mutex);

    if (level >= s.console_level) {
        auto& out = (level >= LogLevel::ERROR) ? std::cerr : std::cout;
        out << level_color(level) << line << level_color_reset() << std::endl;
    }

    if (level >= s.file_level && s.file_stream.is_open()) {
        rotate_file_if_needed();
        s.file_stream << line << std::endl;
        s.current_file_size += line.size() + 1;
    }
}

}

#endif
