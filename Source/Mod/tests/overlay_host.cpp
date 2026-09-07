#include <Windows.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace {

constexpr wchar_t host_class_name[] = L"BedrockUtilityOverlaySmokeHost";
constexpr wchar_t overlay_class_name[] = L"BedrockUtilityFrameworkOverlay";

LRESULT CALLBACK host_window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace


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

int wmain(int argument_count, wchar_t* arguments[]) {
    if (argument_count != 2) {
        std::wcerr << L"Usage: BedrockUtilityOverlayHost.exe <dll-path>\n";
        return 2;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = host_window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = host_class_name;
    if (RegisterClassExW(&window_class) == 0) {
        std::wcerr << L"Could not register test window class.\n";
        return 1;
    }

    const HWND host = CreateWindowExW(
        0,
        host_class_name,
        L"Bedrock Utility Overlay Test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100,
        100,
        1100,
        760,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (host == nullptr) {
        std::wcerr << L"Could not create test window.\n";
        return 1;
    }

    ShowWindow(host, SW_SHOW);
    SetForegroundWindow(host);
    ShowCursor(FALSE);

    const HMODULE library = LoadLibraryW(arguments[1]);
    if (library == nullptr) {
        ShowCursor(TRUE);
        std::wcerr << L"LoadLibraryW failed with error " << GetLastError() << L".\n";
        return 1;
    }

    bool requested_open = false;
    bool overlay_visible = false;
    RECT overlay_rectangle{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const HWND overlay = own_overlay();
        if (overlay && !requested_open) {
            if (IsWindowVisible(overlay)) return 1; // Closed by default.
            PostMessageW(overlay, WM_APP + 41, 0, 0);
            requested_open = true;
        }
        if (overlay != nullptr && IsWindowVisible(overlay) != FALSE &&
            GetWindowRect(overlay, &overlay_rectangle) != FALSE) {
            overlay_visible = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    ShowCursor(TRUE);
    if (!overlay_visible) {
        std::wcerr << L"Overlay did not become visible.\n";
        return 1;
    }

    const int width = overlay_rectangle.right - overlay_rectangle.left;
    const int height = overlay_rectangle.bottom - overlay_rectangle.top;
    std::wcout << L"Overlay visible and tracking host: " << width << L"x" << height << L".\n";
    return width > 0 && height > 0 ? 0 : 1;
}
