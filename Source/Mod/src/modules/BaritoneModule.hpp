#pragma once
#include "../framework/Module.hpp"
#include "../integration/NativeNavigationAdapter.hpp"
#include "../integration/NavigationBridge.hpp"
#include <deque>

namespace utility::modules {
class BaritoneModule final : public Module, public integration::NavigationTickSink {
public:
    ~BaritoneModule() override;
    std::string_view name() const noexcept override { return "Baritone"; }
    ModuleCategory category() const noexcept override { return ModuleCategory::world; }
    bool available() const noexcept override { return ready_.load(); }
    void on_register(EventBus&) override;
    void on_disable() override;
    void on_tick() noexcept override;
    void native_tick(void*) noexcept override;
    void suspend_navigation() noexcept override;
    bool handle_command(const std::vector<std::string>&,std::vector<std::string>&) override;
    std::vector<std::string> command_help() const override;
    std::size_t boolean_setting_count() const noexcept override { return 6; }
    std::string_view boolean_setting_name(std::size_t) const noexcept override;
    bool boolean_setting(std::size_t) const noexcept override;
    void set_boolean_setting(std::size_t,bool) noexcept override;
    void draw_overlay(void*,int,int) noexcept override;
private:
    mutable std::mutex mutex_;
    integration::NativeNavigationAdapter adapter_;
    navigation::Controller controller_;
    navigation::Frame last_frame_;
    navigation::Settings settings_;
    std::deque<navigation::Command> commands_;
    std::atomic_bool ready_{}, stop_requested_{}, suspend_requested_{};
    std::atomic<unsigned long long> last_tick_{};
};
} // namespace utility::modules
