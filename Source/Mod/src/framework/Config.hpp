#pragma once

#include <filesystem>

namespace utility {

struct FrameworkConfig {
    bool logging_enabled{true};
    bool overlay_enabled{false};
};

class Config final {
public:
    void load_defaults() noexcept;
    bool load(const std::filesystem::path& path) noexcept;
    bool save(const std::filesystem::path& path) const noexcept;

    [[nodiscard]] const FrameworkConfig& values() const noexcept;

private:
    FrameworkConfig values_{};
};

} // namespace utility

