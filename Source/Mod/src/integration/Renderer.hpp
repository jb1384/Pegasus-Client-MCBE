#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <vector>

namespace utility {
class ModuleManager;
class Module;
}

namespace utility::integration {

class Renderer final {
public:
    bool initialize(HMODULE module, ModuleManager& modules) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] bool initialized() const noexcept;

private:
    static DWORD WINAPI thread_entry(void* context) noexcept;
    static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    DWORD run() noexcept;
    void tick() noexcept;
    void paint(HWND window) noexcept;
    [[nodiscard]] HWND find_game_window() const noexcept;
    [[nodiscard]] bool game_is_foreground() const noexcept;
    void set_menu_open(bool open, bool return_focus = true) noexcept;
    void legacy_key(int key) noexcept;
    void paint_legacy(HDC device) noexcept;
    int legacy_category_{};
    int legacy_module_{};
    bool legacy_expanded_{};
    std::array<bool, 4> arrow_held_{};
    Module* slider_module_{};
    RECT slider_rect_{};
    void drag_slider(int x) noexcept;
    void click_at(int x, int y, bool right) noexcept;
    static LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam) noexcept;
    struct Hit { RECT rect; Module* module; int action; std::size_t setting{}; };
    std::vector<Hit> hits_;
    Module* settings_module_{};
    HHOOK keyboard_hook_{};
    bool tab_held_{};
    bool world_ready_{};
    int cursor_increments_{};
    int scroll_offset_{};
    int content_height_{};
    int viewport_height_{};

    HMODULE module_{};
    ModuleManager* modules_{};
    HANDLE thread_{};
    HANDLE stop_event_{};
    HANDLE ready_event_{};
    HWND overlay_window_{};
    HWND game_window_{};
    HDC back_buffer_device_{};
    HBITMAP back_buffer_bitmap_{};
    HGDIOBJ back_buffer_previous_bitmap_{};
    int back_buffer_width_{};
    int back_buffer_height_{};
    std::atomic_bool initialized_{false};
    unsigned hidden_cursor_ticks_{};
    bool menu_visible_{};
};

} // namespace utility::integration
