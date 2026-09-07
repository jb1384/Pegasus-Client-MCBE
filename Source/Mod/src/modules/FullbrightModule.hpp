#pragma once
#include "../framework/Module.hpp"
namespace utility::modules {
class FullbrightModule final : public Module {
public:
    ~FullbrightModule() override;
    std::string_view name() const noexcept override { return "fullbright"; }
    ModuleCategory category() const noexcept override { return ModuleCategory::visual; }
    bool has_value() const noexcept override { return true; }
    std::string_view value_label() const noexcept override { return "Light level"; }
    std::string_view value_suffix() const noexcept override { return {}; }
    int value_decimals() const noexcept override { return 0; }
    float value() const noexcept override { return static_cast<float>(level_.load()); }
    float minimum_value() const noexcept override { return 9.0F; }
    float maximum_value() const noexcept override { return 15.0F; }
    void set_value(float level) noexcept override;
    void adjust_value(int direction) noexcept override { set_value(value()+(direction<0?-1.0F:1.0F)); }
    bool available() const noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;
private:
    std::atomic<int> level_{15};
};
}
