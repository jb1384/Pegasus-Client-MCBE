#pragma once
#include "../framework/Module.hpp"
#include <Windows.h>

namespace utility::modules {
enum class GameplayFeature { esp, autotool, phase, airjump, deathposition, autosprint, chest_esp, triggerbot, jetpack, count };
class GameplayModule final : public Module {
public:
    explicit GameplayModule(GameplayFeature feature) : feature_(feature) {}
    std::string_view name() const noexcept override;
    ModuleCategory category() const noexcept override;
    bool available() const noexcept override;
    bool has_value() const noexcept override;
    std::string_view value_label() const noexcept override;
    std::string_view value_suffix() const noexcept override;
    float value() const noexcept override;
    float minimum_value() const noexcept override;
    float maximum_value() const noexcept override;
    void set_value(float value) noexcept override;
    void adjust_value(int direction) noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;
    std::string_view boolean_setting_name() const noexcept override;
    bool boolean_setting() const noexcept override;
    void set_boolean_setting(bool value) noexcept override;
    std::size_t boolean_setting_count() const noexcept override;
    std::string_view boolean_setting_name(std::size_t index) const noexcept override;
    bool boolean_setting(std::size_t index) const noexcept override;
    void set_boolean_setting(std::size_t index,bool value) noexcept override;
    void on_key_down(unsigned key) noexcept override;
    void on_key_up(unsigned key) noexcept override;
    void draw_overlay(void* device, int width, int height) noexcept override;
private:
    GameplayFeature feature_;
};
}
