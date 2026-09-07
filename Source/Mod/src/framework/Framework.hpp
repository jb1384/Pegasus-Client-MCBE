#pragma once

#include <Windows.h>

#include <atomic>

#include "Config.hpp"
#include "EventBus.hpp"
#include "ModuleManager.hpp"
#include "../integration/GameSdk.hpp"
#include "../integration/Renderer.hpp"
#include "../integration/SplashTextHook.hpp"

namespace utility {

class Framework final {
public:
    static Framework& instance() noexcept;

    Framework(const Framework&) = delete;
    Framework& operator=(const Framework&) = delete;

    bool initialize(HMODULE module) noexcept;
    void shutdown() noexcept;
    bool request_eject() noexcept;
    [[nodiscard]] bool initialized() const noexcept;

private:
    Framework() = default;

    std::atomic_bool initialized_{false};
    std::atomic_bool eject_requested_{false};
    HMODULE module_{};
    Config config_{};
    EventBus events_{};
    ModuleManager modules_{};
    integration::GameSdk game_sdk_{};
    integration::Renderer renderer_{};
    integration::SplashTextHook splash_text_hook_{};
};

} // namespace utility
