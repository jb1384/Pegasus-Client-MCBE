#pragma once

#include <string_view>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace utility {

class EventBus;

enum class ModuleCategory {
    combat,
    visual,
    movement,
    player,
    world,
    misc,
    gui,
};

class Module {
public:
    virtual ~Module() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual ModuleCategory category() const noexcept { return ModuleCategory::misc; }
    [[nodiscard]] virtual bool available() const noexcept { return true; }
    [[nodiscard]] virtual bool has_value() const noexcept { return false; }
    [[nodiscard]] virtual std::string_view value_label() const noexcept { return "Distance"; }
    [[nodiscard]] virtual std::string_view value_suffix() const noexcept { return " blocks"; }
    [[nodiscard]] virtual int value_decimals() const noexcept { return 1; }
    [[nodiscard]] virtual float value() const noexcept { return 0.0F; }
    [[nodiscard]] virtual float minimum_value() const noexcept { return 0.0F; }
    [[nodiscard]] virtual float maximum_value() const noexcept { return 0.0F; }
    [[nodiscard]] virtual bool shows_array_list() const noexcept { return false; }
    [[nodiscard]] virtual std::string_view boolean_setting_name() const noexcept { return {}; }
    [[nodiscard]] virtual bool boolean_setting() const noexcept { return false; }
    virtual void set_boolean_setting(bool) noexcept {}
    [[nodiscard]] virtual std::size_t boolean_setting_count() const noexcept { return boolean_setting_name().empty()?0:1; }
    [[nodiscard]] virtual std::string_view boolean_setting_name(std::size_t index) const noexcept { return index==0?boolean_setting_name():std::string_view{}; }
    [[nodiscard]] virtual bool boolean_setting(std::size_t index) const noexcept { return index==0&&boolean_setting(); }
    virtual void set_boolean_setting(std::size_t index,bool value) noexcept { if(index==0)set_boolean_setting(value); }

    virtual void adjust_value(int) noexcept {}
    virtual void set_value(float) noexcept {}
    virtual void on_register(EventBus&) {}
    virtual void on_enable() {}
    virtual void on_disable() {}
    virtual void on_tick() noexcept {}
    virtual void on_key_down(unsigned) noexcept {}
    virtual void on_key_up(unsigned) noexcept {}
    virtual void draw_overlay(void*, int, int) noexcept {}
    // Local commands are dispatched after built-in commands, before unknown-command feedback.
    virtual bool handle_command(const std::vector<std::string>&, std::vector<std::string>&) { return false; }
    virtual std::vector<std::string> command_help() const { return {}; }

    void set_enabled(bool enabled) {
        if (enabled && !available()) {
            return;
        }
        if (enabled_ == enabled) {
            return;
        }
        enabled_ = enabled;
        enabled_ ? on_enable() : on_disable();
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
    std::atomic_bool enabled_{false};
};

} // namespace utility
