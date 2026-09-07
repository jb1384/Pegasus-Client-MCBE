#include "integration/Renderer.hpp"
#include "framework/ModuleManager.hpp"
#include <algorithm>
#include <iostream>

class Adjustable final : public utility::Module {
public:
    std::string_view name() const noexcept override { return "Test distance"; }
    utility::ModuleCategory category() const noexcept override { return utility::ModuleCategory::combat; }
    bool has_value() const noexcept override { return true; }
    float value() const noexcept override { return distance; }
    void adjust_value(int direction) noexcept override { distance = std::clamp(distance + direction * 0.5F, 3.0F, 7.0F); }
    float minimum_value() const noexcept override { return 3; }
    float maximum_value() const noexcept override { return 7; }
    void set_value(float value) noexcept override { distance = std::clamp(value, 3.0F, 7.0F); }
    float distance = 5;
};


HWND own_overlay() {
    HWND result{};
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        DWORD pid{}; GetWindowThreadProcessId(window, &pid);
        wchar_t name[100]{}; GetClassNameW(window, name, 100);
        if (pid == GetCurrentProcessId() && wcscmp(name, L"BedrockUtilityFrameworkOverlay") == 0) {
            *reinterpret_cast<HWND*>(context) = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW c{}; c.hInstance = instance; c.lpfnWndProc = DefWindowProcW; c.lpszClassName = L"MenuInteractionTest";
    RegisterClassW(&c);
    const auto host = CreateWindowW(c.lpszClassName, L"Menu interaction test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 1240, 600, nullptr, nullptr, instance, nullptr);
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    SetForegroundWindow(host);
    if (GetForegroundWindow() != host) {
        std::cout << "SKIP: menu interaction needs foreground access to its test window.\n";
        DestroyWindow(host); UnregisterClassW(c.lpszClassName, instance);
        return 77;
    }
    utility::ModuleManager manager;
    auto module = std::make_unique<Adjustable>(); auto* adjustable = module.get(); manager.add(std::move(module));
    utility::integration::Renderer renderer;
    if (!renderer.initialize(instance, manager)) return 1;
    const auto overlay = own_overlay();
    bool passed = overlay && !IsWindowVisible(overlay);
    SendMessageW(overlay, WM_APP + 41, 0, 0);
    SendMessageW(overlay, WM_PAINT, 0, 0);
    if (!(IsWindowVisible(overlay) != FALSE)) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    SendMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 70));
    if (!(adjustable->enabled())) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    SendMessageW(overlay, WM_PAINT, 0, 0);
    SendMessageW(overlay, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(40, 70));
    if (!(adjustable->enabled() && adjustable->distance == 5)) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    SendMessageW(overlay, WM_PAINT, 0, 0);
    SendMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 130));
    SendMessageW(overlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(999, 130));
    SendMessageW(overlay, WM_LBUTTONUP, 0, MAKELPARAM(999, 130));
    if (!(adjustable->distance == 7)) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    SendMessageW(overlay, WM_PAINT, 0, 0);
    SendMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 130));
    SendMessageW(overlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-100, 130));
    SendMessageW(overlay, WM_LBUTTONUP, 0, MAKELPARAM(-100, 130));
    if (!(adjustable->distance == 3)) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    SendMessageW(overlay, WM_PAINT, 0, 0);
    SendMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 70));
    if (!(!adjustable->enabled())) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    SendMessageW(overlay, WM_CLOSE, 0, 0);
    if (!(!IsWindowVisible(overlay))) { passed = false; std::cerr << "Check failed at line " << __LINE__ << "\n"; }
    renderer.shutdown();
    manager.shutdown(); DestroyWindow(host); UnregisterClassW(c.lpszClassName, instance);
    std::cout << (passed ? "Menu toggle, settings and close passed.\n" : "Menu interaction failed.\n");
    return passed ? 0 : 1;
}






