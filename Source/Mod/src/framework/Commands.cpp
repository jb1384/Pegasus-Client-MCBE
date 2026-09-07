#include "Commands.hpp"
#include "ChatStyle.hpp"
#include "ModuleManager.hpp"
#include "../integration/NavigationBridge.hpp"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <charconv>

namespace utility {
namespace {
std::string lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
bool tokenize(std::string_view input, std::vector<std::string>& args) {
    while (!input.empty()) {
        const auto start = input.find_first_not_of(" \t\r\n");
        if (start == input.npos) break;
        input.remove_prefix(start);
        if (input.front() == '"') {
            input.remove_prefix(1);
            const auto end = input.find('"');
            if (end == input.npos) return false;
            args.emplace_back(input.substr(0, end)); input.remove_prefix(end + 1);
            if (!input.empty() && input.find_first_of(" \t\r\n") != 0) return false;
        } else {
            const auto end = input.find_first_of(" \t\r\n");
            args.emplace_back(input.substr(0, end));
            input.remove_prefix(end == input.npos ? input.size() : end);
        }
    }
    return true;
}
unsigned key_code(std::string_view text) {
    const auto name = lower(text);
    if (name.size() == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
        return static_cast<unsigned>(std::toupper(static_cast<unsigned char>(name[0])));
    if (name.size() > 1 && name[0] == 'f') {
        unsigned number{};
        const auto [end, error] = std::from_chars(name.data()+1, name.data()+name.size(), number);
        if (error == std::errc{} && end == name.data()+name.size() && number >= 1 && number <= 24) return VK_F1+number-1;
    }
    struct NamedKey { std::string_view name; unsigned code; };
    constexpr NamedKey keys[]{ {"space",VK_SPACE},{"shift",VK_SHIFT},{"ctrl",VK_CONTROL},{"control",VK_CONTROL},
        {"alt",VK_MENU},{"lshift",VK_LSHIFT},{"rshift",VK_RSHIFT},{"lctrl",VK_LCONTROL},{"rctrl",VK_RCONTROL},
        {"lalt",VK_LMENU},{"ralt",VK_RMENU},{"capslock",VK_CAPITAL},{"insert",VK_INSERT},{"delete",VK_DELETE},
        {"home",VK_HOME},{"end",VK_END},{"pageup",VK_PRIOR},{"pagedown",VK_NEXT},{"backspace",VK_BACK},
        {"minus",VK_OEM_MINUS},{"plus",VK_OEM_PLUS},{"period",VK_OEM_PERIOD},{"comma",VK_OEM_COMMA},
        {"slash",VK_OEM_2},{"semicolon",VK_OEM_1},{"quote",VK_OEM_7},{"backslash",VK_OEM_5},
        {"lbracket",VK_OEM_4},{"rbracket",VK_OEM_6},{"grave",VK_OEM_3} };
    for (const auto& key : keys) if (key.name == name) return key.code;
    if (name.size()==7 && name.starts_with("numpad") && name[6]>='0' && name[6]<='9') return VK_NUMPAD0+name[6]-'0';
    return 0;
}
bool matches(unsigned binding, const std::array<bool,256>& pressed) {
    if (binding == VK_SHIFT) return pressed[VK_SHIFT] || pressed[VK_LSHIFT] || pressed[VK_RSHIFT];
    if (binding == VK_CONTROL) return pressed[VK_CONTROL] || pressed[VK_LCONTROL] || pressed[VK_RCONTROL];
    if (binding == VK_MENU) return pressed[VK_MENU] || pressed[VK_LMENU] || pressed[VK_RMENU];
    return pressed[binding];
}
constexpr auto usage = "Usage: ,keybind <module> <key> <toggle|keyhold> (quote names containing spaces).";
}
bool Commands::execute(std::string_view text, ModuleManager& modules, std::vector<std::string>& replies) {
    if (text.empty() || text.front() != ',') return false;
    // Apply a consistent error/usage color to every early-return response.
    struct StyleReplies {
        std::vector<std::string>& lines;
        std::size_t first;
        ~StyleReplies() {
            for (auto i=first; i<lines.size(); ++i) {
                auto& line=lines[i];
                if (!line.starts_with("\xC2\xA7"))
                    line.insert(0,line.starts_with("Usage:") ? chat_style::yellow : chat_style::red);
                line += chat_style::reset;
            }
        }
    } style{replies,replies.size()};
    std::vector<std::string> args;
    if (!tokenize(text.substr(1), args)) { replies.emplace_back("Unclosed or misplaced quote. Quote the complete module name; type ,help for usage."); return true; }
    const auto name = args.empty() ? std::string{} : lower(args[0]);
    if (name == "help") {
        if (args.size() != 1) replies.emplace_back("Usage: ,help (no arguments).");
        else {
            replies.emplace_back(std::string(chat_style::aqua)+",help"+chat_style::gray+" - Show commands.");
            replies.emplace_back(std::string(chat_style::aqua)+",keybind "+chat_style::white+"<module> <key> <toggle|keyhold>"+chat_style::gray+" - Bind a module; quote spaced names.");
            replies.emplace_back(std::string(chat_style::aqua)+",unbind "+chat_style::white+"<module>"+chat_style::gray+" - Remove all key bindings for a module.");
            replies.emplace_back(std::string(chat_style::aqua)+",eject"+chat_style::gray+" - Disable the mod for this session.");
            for (const auto& module : modules.modules())
                for (const auto& line : module->command_help())
                    replies.emplace_back(std::string(chat_style::aqua)+line);
        }
        return true;
    }
    if (name == "eject") {
        if (args.size()!=1) { replies.emplace_back("Usage: ,eject (no arguments)."); return true; }
        std::lock_guard lock(mutex_);
        if (eject_pending_) replies.emplace_back(std::string(chat_style::yellow)+"Ejection is already in progress.");
        else if (!eject_handler_ || !eject_handler_()) replies.emplace_back("Could not start ejection. Try again.");
        else {
            eject_pending_=true;
            replies.emplace_back(std::string(chat_style::green)+"Ejecting utility mod...");
        }
        return true;
    }
    if (name != "keybind" && name != "unbind") {
        for (const auto& candidate : modules.modules())
            if (candidate->handle_command(args,replies)) return true;
        replies.emplace_back("Unrecognized utility command. Type ,help for available commands."); return true;
    }
    if (args.size() != (name == "unbind" ? 2U : 4U)) {
        replies.emplace_back(name == "unbind" ? "Usage: ,unbind <module> (quote names containing spaces)." : usage);
        return true;
    }
    Module* module{};
    for (const auto& candidate : modules.modules()) if (lower(candidate->name()) == lower(args[1])) { module=candidate.get(); break; }
    if (!module) { replies.emplace_back("Unknown module: " + args[1] + ". Use the name shown in the module menu."); return true; }
    if (name == "unbind") {
        std::lock_guard lock(mutex_);
        // Erase every binding for this module, preserving other modules on shared keys.
        // Only an active keyhold owns the module's enabled state; toggles keep theirs.
        const auto removed = std::erase_if(bindings_, [module](const Binding& binding) {
            if (binding.module != module) return false;
            if (binding.hold && binding.active) module->set_enabled(false);
            return true;
        });
        if (removed) replies.emplace_back(std::string(chat_style::green)+"Unbound "+chat_style::aqua+std::string(module->name())+chat_style::green+" from all keys.");
        else replies.emplace_back(std::string(chat_style::yellow)+std::string(module->name())+" has no key bindings.");
        return true;
    }
    const unsigned code = key_code(args[2]);
    if (!code) { replies.emplace_back("Unknown or reserved key: " + args[2] + ". Use A-Z, 0-9, F1-F24 or a named key (e.g. space, lshift)."); return true; }
    const auto mode = lower(args[3]);
    if (mode != "toggle" && mode != "keyhold") { replies.emplace_back(usage); return true; }
    if (!module->available()) { replies.emplace_back("Module unavailable in this game build: " + std::string(module->name())); return true; }
    std::lock_guard lock(mutex_);
    auto found = std::find_if(bindings_.begin(), bindings_.end(), [module](const Binding& b){return b.module==module;});
    if (found != bindings_.end()) {
        if (found->hold && found->active) module->set_enabled(false);
        *found = {module,code,mode=="keyhold"};
    } else bindings_.push_back({module,code,mode=="keyhold"});
    if (mode=="keyhold") module->set_enabled(false);
    replies.emplace_back(std::string(chat_style::green)+"Bound "+chat_style::aqua+std::string(module->name())+chat_style::green+" to "+chat_style::white+args[2]+chat_style::gray+" ("+mode+").");
    return true;
}
void Commands::set_eject_handler(std::function<bool()> handler) {
    std::lock_guard lock(mutex_); eject_handler_=std::move(handler); eject_pending_=false;
}
void Commands::key(unsigned code, bool down, bool gameplay) {
    if (code >= pressed_.size()) return;
    std::lock_guard lock(mutex_);
    const auto before = pressed_;
    pressed_[code] = down;
    for (auto& b : bindings_) {
        const bool held = matches(b.key, pressed_);
        if (b.hold && b.active && (!held || !gameplay)) { b.module->set_enabled(false); b.active=false; }
        if (gameplay && held && !matches(b.key,before)) {
            if (b.hold) { b.module->set_enabled(true); b.active=true; }
            else b.module->set_enabled(!b.module->enabled());
        }
    }
}
void Commands::suspend() {
    integration::navigation_suspend();
    std::lock_guard lock(mutex_);
    // Preserve physical edge state: resuming while a key is held must not activate it.
    for (auto& b : bindings_) if (b.hold && b.active) { b.module->set_enabled(false); b.active=false; }
}
void Commands::clear() {
    suspend();
    std::lock_guard lock(mutex_); bindings_.clear(); pressed_.fill(false); eject_handler_={}; eject_pending_=false;
}
}
