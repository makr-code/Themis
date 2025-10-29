#pragma once

#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace themis {
namespace utils {

template<typename FormatString, typename... Args>
void Logger::trace(FormatString&& fmt, Args&&... args) {
    if (logger_) {
        logger_->trace(fmt::runtime(std::forward<FormatString>(fmt)), std::forward<Args>(args)...);
    }
}

template<typename FormatString, typename... Args>
void Logger::debug(FormatString&& fmt, Args&&... args) {
    if (logger_) {
        logger_->debug(fmt::runtime(std::forward<FormatString>(fmt)), std::forward<Args>(args)...);
    }
}

template<typename FormatString, typename... Args>
void Logger::info(FormatString&& fmt, Args&&... args) {
    if (logger_) {
        logger_->info(fmt::runtime(std::forward<FormatString>(fmt)), std::forward<Args>(args)...);
    }
}

template<typename FormatString, typename... Args>
void Logger::warn(FormatString&& fmt, Args&&... args) {
    if (logger_) {
        logger_->warn(fmt::runtime(std::forward<FormatString>(fmt)), std::forward<Args>(args)...);
    }
}

template<typename FormatString, typename... Args>
void Logger::error(FormatString&& fmt, Args&&... args) {
    if (logger_) {
        logger_->error(fmt::runtime(std::forward<FormatString>(fmt)), std::forward<Args>(args)...);
    }
}

template<typename FormatString, typename... Args>
void Logger::critical(FormatString&& fmt, Args&&... args) {
    if (logger_) {
        logger_->critical(fmt::runtime(std::forward<FormatString>(fmt)), std::forward<Args>(args)...);
    }
}

} // namespace utils
} // namespace themis
