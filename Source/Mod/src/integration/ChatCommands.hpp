#pragma once
namespace utility { class ModuleManager; }
namespace utility::integration {
// Installed callbacks pin the DLL, like the existing native gameplay hooks.
bool install_chat_commands(ModuleManager& modules) noexcept;
// pass_through restores ordinary comma chat after a deliberate eject.
void stop_chat_commands(bool pass_through = false) noexcept;
}
