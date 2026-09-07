#pragma once
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace utility {
class Module;
class ModuleManager;
class Commands final {
public:
    // true always means consume locally, including invalid comma commands.
    bool execute(std::string_view text, ModuleManager& modules, std::vector<std::string>& replies);
    // The handler only schedules shutdown; it must not destroy modules inline.
    void set_eject_handler(std::function<bool()> handler);
    void key(unsigned key, bool down, bool gameplay);
    void suspend();
    void clear();
private:
    struct Binding { Module* module; unsigned key; bool hold; bool active{}; };
    std::mutex mutex_;
    std::function<bool()> eject_handler_;
    bool eject_pending_{};
    std::vector<Binding> bindings_;
    std::array<bool, 256> pressed_{};
};
}
