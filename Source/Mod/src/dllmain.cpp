#include <Windows.h>

#include "framework/Framework.hpp"

namespace {

DWORD WINAPI initialization_thread(void* module_handle) noexcept {
    auto& framework = utility::Framework::instance();
    framework.initialize(static_cast<HMODULE>(module_handle));
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        const HANDLE thread = CreateThread(
            nullptr,
            0,
            initialization_thread,
            module,
            0,
            nullptr);

        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }

    return TRUE;
}

