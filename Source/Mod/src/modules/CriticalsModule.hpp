#pragma once

#include "../framework/Module.hpp"

namespace utility::modules {

class CriticalsModule final : public Module {
public:
    ~CriticalsModule() override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] ModuleCategory category() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;

private:
    [[nodiscard]] bool verify() noexcept;
    [[nodiscard]] bool apply() noexcept;
    void restore() noexcept;

    bool signatures_valid_{};
    bool patched_{};
};

} // namespace utility::modules
