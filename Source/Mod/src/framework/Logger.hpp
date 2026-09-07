#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace utility {

class Logger final {
public:
    static Logger& instance() noexcept;

    bool initialize() noexcept;
    void shutdown() noexcept;
    void info(std::string_view message) noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    Logger() = default;
    void write(std::string_view level, std::string_view message) noexcept;

    std::mutex mutex_;
    std::ofstream stream_;
    std::filesystem::path path_;
};

} // namespace utility

