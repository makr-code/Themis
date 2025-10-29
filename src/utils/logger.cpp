#include "utils/logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <iostream>

// Windows defines ERROR as a macro; undef it
#ifdef ERROR
#undef ERROR
#endif

namespace themis {
namespace utils {

std::shared_ptr<spdlog::logger> Logger::logger_;

void Logger::init(const std::string& log_file, Level level) {
    try {
        // Create console and file sinks
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
        
        // Create logger with both sinks
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        logger_ = std::make_shared<spdlog::logger>("themis", sinks.begin(), sinks.end());
        
        // Set level
        spdlog::level::level_enum spdlog_level;
        switch (level) {
            case Level::TRACE: spdlog_level = spdlog::level::trace; break;
            case Level::DEBUG: spdlog_level = spdlog::level::debug; break;
            case Level::INFO: spdlog_level = spdlog::level::info; break;
            case Level::WARN: spdlog_level = spdlog::level::warn; break;
            case Level::ERROR: spdlog_level = spdlog::level::err; break;
            case Level::CRITICAL: spdlog_level = spdlog::level::critical; break;
            default: spdlog_level = spdlog::level::info;
        }
        logger_->set_level(spdlog_level);
        
        // Set pattern
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [thread %t] %v");
        
        // Register as default logger
        spdlog::set_default_logger(logger_);
        
        logger_->info("Logger initialized");
    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

void Logger::shutdown() {
    if (logger_) {
        logger_->flush();
        spdlog::shutdown();
        logger_.reset();
    }
}

std::shared_ptr<spdlog::logger> Logger::get() {
    if (!logger_) {
        init(); // Auto-initialize with defaults
    }
    return logger_;
}

} // namespace utils
} // namespace themis
