#include "Framework.hpp"

#include "Logger.hpp"
#include "../integration/ChatCommands.hpp"
#include "../modules/AntiKnockbackModule.hpp"
#include "../modules/CriticalsModule.hpp"
#include "../modules/XrayModule.hpp"
#include "../modules/FullbrightModule.hpp"
#include "../modules/ReachModule.hpp"
#include "../modules/GhostHandModule.hpp"
#include "../modules/SpeedModule.hpp"
#include "../modules/ArrayListModule.hpp"
#include "../modules/GameplayModules.hpp"
#include "../modules/BaritoneModule.hpp"

#include <memory>

namespace utility {

Framework& Framework::instance() noexcept {
    static Framework framework;
    return framework;
}

bool Framework::initialize(HMODULE module) noexcept {
    if (eject_requested_.load()) return false;
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) {
        return true;
    }

    module_ = module;

    if (!Logger::instance().initialize()) {
        initialized_.store(false);
        return false;
    }

    Logger::instance().info("BedrockUtilityFramework loaded successfully.");
    if (splash_text_hook_.install()) {
        Logger::instance().info("Native splash-text hook installed; replacement is 'made by Roundomegaboi'.");
    } else {
        Logger::instance().info("Native splash-text hook not installed: host/build/signature is unsupported.");
    }

    config_.load_defaults();
    auto entity_reach = std::make_unique<modules::ReachModule>();
    auto* reach = entity_reach.get();
    modules_.add(std::move(entity_reach));
    modules_.add(std::make_unique<modules::BlockReachModule>(*reach));
    modules_.add(std::make_unique<modules::GhostHandModule>());
    modules_.add(std::make_unique<modules::AntiKnockbackModule>());
    modules_.add(std::make_unique<modules::CriticalsModule>());
    modules_.add(std::make_unique<modules::XrayModule>());
    modules_.add(std::make_unique<modules::FullbrightModule>());
    modules_.add(std::make_unique<modules::SpeedModule>());
    modules_.add(std::make_unique<modules::ArrayListModule>());
    for (unsigned i=0; i<static_cast<unsigned>(modules::GameplayFeature::count); ++i)
        modules_.add(std::make_unique<modules::GameplayModule>(static_cast<modules::GameplayFeature>(i)));
    modules_.add(std::make_unique<modules::BaritoneModule>());
    modules_.initialize(events_);
    if (renderer_.initialize(module_, modules_)) {
        Logger::instance().info("Click menu initialized; Tab opens, left click toggles, right click opens settings.");
    } else {
        Logger::instance().info("Menu overlay initialization failed.");
    }
    modules_.commands().set_eject_handler([this]{return request_eject();});
    Logger::instance().info(integration::install_chat_commands(modules_)
        ? "Local comma commands installed: ,help, ,keybind, ,unbind and ,eject."
        : "Local commands unavailable: unsupported chat signature or hook installation failed.");
    return true;
}

void Framework::shutdown() noexcept {
    if (!initialized_.exchange(false)) {
        return;
    }

    integration::stop_chat_commands(true);
    renderer_.shutdown();
    splash_text_hook_.uninstall();
    modules_.deactivate();
    Logger::instance().info("BedrockUtilityFramework shutting down.");
    Logger::instance().shutdown();
    module_ = nullptr;
}

bool Framework::request_eject() noexcept {
    bool expected=false;
    if (!initialized_.load() || !eject_requested_.compare_exchange_strong(expected,true)) return false;
    // Shutdown waits for the current chat callback, then joins the overlay thread.
    // Never run it inline from either of those threads.
    const auto thread=CreateThread(nullptr,0,[](void* context)->DWORD {
        static_cast<Framework*>(context)->shutdown();
        return 0;
    },this,0,nullptr);
    if (!thread) {eject_requested_=false;return false;}
    CloseHandle(thread);
    return true;
}

bool Framework::initialized() const noexcept {
    return initialized_.load();
}

} // namespace utility
