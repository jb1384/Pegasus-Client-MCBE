#pragma once

#include "../framework/Module.hpp"

#include <string_view>

namespace utility::modules {

// A fail-closed menu entry for a native feature whose exact hook has not yet
// been verified for the running Minecraft build.  Keeping unavailable features
// visible makes the menu structure testable without pretending that a visual
// toggle changes authoritative gameplay.
class NativeFeatureModule final : public Module {
public:
    NativeFeatureModule(std::string_view module_name, ModuleCategory module_category) noexcept
        : name_(module_name), category_(module_category) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] ModuleCategory category() const noexcept override { return category_; }
    [[nodiscard]] bool available() const noexcept override { return false; }

private:
    std::string_view name_;
    ModuleCategory category_;
};

} // namespace utility::modules
