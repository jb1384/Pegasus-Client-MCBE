#include "Config.hpp"

namespace utility {

void Config::load_defaults() noexcept {
    values_ = FrameworkConfig{};
}

bool Config::load(const std::filesystem::path&) noexcept {
    // Placeholder: add a versioned parser when settings are introduced.
    return false;
}

bool Config::save(const std::filesystem::path&) const noexcept {
    // Placeholder: add atomic file replacement with the parser implementation.
    return false;
}

const FrameworkConfig& Config::values() const noexcept {
    return values_;
}

} // namespace utility

