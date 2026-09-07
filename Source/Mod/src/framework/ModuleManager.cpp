#include "ModuleManager.hpp"

#include "EventBus.hpp"

namespace utility {

void ModuleManager::initialize(EventBus& events) {
    events_ = &events;
    for (const auto& module : modules_) {
        module->on_register(events);
    }
}

void ModuleManager::deactivate() noexcept {
    commands_.clear();
    for (const auto& module : modules_) {
        module->set_enabled(false);
    }
    if (events_ != nullptr) {
        events_->clear();
        events_ = nullptr;
    }
}

void ModuleManager::shutdown() noexcept {
    deactivate();
    modules_.clear();
}

void ModuleManager::tick() noexcept {
    for (const auto& module : modules_) {
        if (module->enabled()) {
            module->on_tick();
        }
    }
}

void ModuleManager::add(std::unique_ptr<Module> module) {
    if (events_ != nullptr) {
        module->on_register(*events_);
    }
    modules_.push_back(std::move(module));
}

const std::vector<std::unique_ptr<Module>>& ModuleManager::modules() const noexcept {
    return modules_;
}

} // namespace utility
