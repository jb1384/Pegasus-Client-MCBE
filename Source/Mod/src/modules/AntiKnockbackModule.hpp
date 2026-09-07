#pragma once

#include "../framework/Module.hpp"

#include <atomic>
#include <cstddef>

namespace utility::modules {

class AntiKnockbackModule final : public Module {
public:
    ~AntiKnockbackModule() override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] ModuleCategory category() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;

private:
    using ApplyKnockbackFunction = void(__fastcall*)(
        void*, void*, float, float, float, void*);

    [[nodiscard]] bool install_hook() noexcept;
    void uninstall_hook() noexcept;
    static void __fastcall apply_knockback_hook(
        void*, void*, float, float, float, void*) noexcept;

    std::atomic_bool active_{};
    ApplyKnockbackFunction original_{};
    void* target_{};
    void* trampoline_{};
    unsigned char original_bytes_[15]{};
    bool hook_installed_{};
};

} // namespace utility::modules
