#pragma once

#include "../framework/Module.hpp"

#include <Windows.h>

#include <atomic>
#include <cstddef>

namespace utility::modules {

class ReachModule final : public Module {
public:
    ~ReachModule() override;
    void set_block_enabled(bool enabled) noexcept { block_active_.store(enabled); }
    float block_value() const noexcept { return block_distance_.load(); }
    void set_block_value(float distance) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] ModuleCategory category() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] bool has_value() const noexcept override;
    [[nodiscard]] float value() const noexcept override;
    [[nodiscard]] float minimum_value() const noexcept override;
    [[nodiscard]] float maximum_value() const noexcept override;
    void adjust_value(int direction) noexcept override;
    void set_value(float distance) noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;

private:
    [[nodiscard]] bool install_hooks() noexcept;
    void uninstall_hooks() noexcept;

    using PickRangeFunction = float(__fastcall*)(void*, const int*, bool);
    using MaxPickRangeFunction = float(__fastcall*)(void*);
    static float __fastcall pick_range_hook(void*, const int*, bool) noexcept;
    static float __fastcall max_pick_range_hook(void*) noexcept;

    static void __fastcall final_range_hook(void*, void*, float, bool) noexcept;
    using FinalRangeFunction = void(__fastcall*)(void*, void*, float, bool);
    FinalRangeFunction original_final_range_{};
    void* final_target_{};
    unsigned char final_original_[18]{};
    std::atomic_bool block_active_{false};
    std::atomic<float> block_distance_{7.0F};
    std::atomic<float> distance_{7.0F};
    std::atomic_bool active_{false};
    PickRangeFunction original_pick_range_{};
    MaxPickRangeFunction original_max_pick_range_{};
    void** pick_slots_[2]{};
    void* max_target_{};
    void* max_trampoline_{};
    unsigned char max_original_[14]{};
    bool hooks_installed_{};
};

class BlockReachModule final : public Module {
public:
    explicit BlockReachModule(ReachModule& entity) : entity_(entity) {}
    std::string_view name() const noexcept override { return "BlockReach"; }
    ModuleCategory category() const noexcept override { return ModuleCategory::world; }
    bool available() const noexcept override { return entity_.available(); }
    bool has_value() const noexcept override { return true; }
    float value() const noexcept override { return entity_.block_value(); }
    float minimum_value() const noexcept override { return 3.0F; }
    float maximum_value() const noexcept override { return 7.0F; }
    void set_value(float distance) noexcept override { entity_.set_block_value(distance); }
    void adjust_value(int direction) noexcept override { set_value(value() + (direction < 0 ? -0.5F : 0.5F)); }
    void on_enable() override { entity_.set_block_enabled(true); }
    void on_disable() override { entity_.set_block_enabled(false); }
private:
    ReachModule& entity_;
};
} // namespace utility::modules
