#pragma once

#include "../framework/Module.hpp"

#include <atomic>

namespace utility::modules {

class SpeedModule final : public Module {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] ModuleCategory category() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;
    void on_tick() noexcept override;

private:
    std::atomic_bool supported_{};
};

} // namespace utility::modules
