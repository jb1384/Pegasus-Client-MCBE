#pragma once

#include "../framework/Module.hpp"

namespace utility::modules {

class GhostHandModule final : public Module {
public:
    ~GhostHandModule() override;
    std::string_view name() const noexcept override { return "Ghost Hand"; }
    ModuleCategory category() const noexcept override { return ModuleCategory::combat; }
    bool available() const noexcept override { return ready_; }
    std::string_view boolean_setting_name() const noexcept override { return "Players only"; }
    bool boolean_setting() const noexcept override;
    void set_boolean_setting(bool value) noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;

private:
    unsigned char* image_{};
    bool ready_{};
    bool patched_{};
};

} // namespace utility::modules
