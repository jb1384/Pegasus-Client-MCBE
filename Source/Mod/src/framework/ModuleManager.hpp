#pragma once

#include <memory>
#include <vector>

#include "Module.hpp"
#include "Commands.hpp"

namespace utility {

class EventBus;

class ModuleManager final {
public:
    Commands& commands() noexcept { return commands_; }
    void initialize(EventBus& events);
    void shutdown() noexcept;
    // Keep callback-owned objects alive while pinned native hooks wind down.
    void deactivate() noexcept;
    void tick() noexcept;
    void add(std::unique_ptr<Module> module);

    [[nodiscard]] const std::vector<std::unique_ptr<Module>>& modules() const noexcept;

private:
    Commands commands_;
    EventBus* events_{};
    std::vector<std::unique_ptr<Module>> modules_;
};

} // namespace utility
