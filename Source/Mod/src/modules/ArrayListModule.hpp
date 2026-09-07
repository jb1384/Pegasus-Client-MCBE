#pragma once
#include "../framework/Module.hpp"
namespace utility::modules {
class ArrayListModule final : public Module {
public:
    std::string_view name() const noexcept override { return "ArrayList"; }
    ModuleCategory category() const noexcept override { return ModuleCategory::gui; }
    bool shows_array_list() const noexcept override { return true; }
    std::string_view boolean_setting_name() const noexcept override { return "Left"; }
    bool boolean_setting() const noexcept override { return left_.load(); }
    void set_boolean_setting(bool left) noexcept override { left_=left; }
private:
    std::atomic_bool left_{false};
};
}
