#include "Logger.hpp"

#include <Windows.h>

#include <chrono>
#include <iomanip>
#include <system_error>

namespace utility {

Logger& Logger::instance() noexcept {
    static Logger logger;
    return logger;
}

bool Logger::initialize() noexcept {
    std::scoped_lock lock(mutex_);

    wchar_t local_app_data[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }

    const auto directory = std::filesystem::path(local_app_data) / L"BedrockUtilityFramework";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    path_ = directory / L"framework.log";
    stream_.open(path_, std::ios::out | std::ios::app);
    return stream_.is_open();
}

void Logger::shutdown() noexcept {
    std::scoped_lock lock(mutex_);
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

void Logger::info(std::string_view message) noexcept {
    write("INFO", message);
}

const std::filesystem::path& Logger::path() const noexcept {
    return path_;
}

void Logger::write(std::string_view level, std::string_view message) noexcept {
    std::scoped_lock lock(mutex_);
    if (!stream_.is_open()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_s(&local_time, &timestamp);

    stream_ << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] ["
            << level << "] " << message << '\n';
    stream_.flush();
}

} // namespace utility

