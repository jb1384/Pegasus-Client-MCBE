#include "Renderer.hpp"
#include "../framework/ModuleManager.hpp"
#include <algorithm>

#include <string>
#include <string_view>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace utility::integration {
namespace {
thread_local Renderer* active_renderer = nullptr;
constexpr UINT toggle_menu_message = WM_APP + 41;
constexpr UINT legacy_key_message = WM_APP + 42;
constexpr COLORREF transparent_key = RGB(255, 0, 255);
constexpr unsigned enter_world_ticks = 8;
constexpr wchar_t overlay_class_name[] = L"BedrockUtilityFrameworkOverlay";
struct Category { ModuleCategory category; const wchar_t* name; };
constexpr Category categories[] = {
 {ModuleCategory::combat,L"Combat"},{ModuleCategory::visual,L"Visual"},
 {ModuleCategory::movement,L"Movement"},{ModuleCategory::player,L"Player"},
 {ModuleCategory::world,L"World"},{ModuleCategory::misc,L"Misc"},{ModuleCategory::gui,L"Gui"}};
struct Row { Module* module; };
bool has_settings(const Module* module) noexcept {
    return module && (module->has_value() || module->boolean_setting_count()!=0);
}
std::vector<Row> rows_for_category(ModuleManager& manager, ModuleCategory category) {
 std::vector<Row> result;
 for (const auto& m:manager.modules()) if(m->category()==category) result.push_back({m.get()});
 return result;
}
std::wstring widen(std::string_view text) {
 int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
 std::wstring result(n,L'\0');
 MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),n);return result;
}
void fill_rectangle(HDC dc, const RECT& r, COLORREF color) {
 auto b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);
}
struct Search { DWORD pid; HWND overlay; HWND best{}; long long area{}; };
BOOL CALLBACK child_match(HWND w, LPARAM p) {
 auto* pid=reinterpret_cast<DWORD*>(p);DWORD found{};GetWindowThreadProcessId(w,&found);
 if(found==*pid){*pid=0;return FALSE;}return TRUE;
}
BOOL CALLBACK window_match(HWND w, LPARAM p) {
 auto& s=*reinterpret_cast<Search*>(p);if(w==s.overlay || !IsWindowVisible(w))return TRUE;
 DWORD pid{};GetWindowThreadProcessId(w,&pid);
 if(pid!=s.pid){DWORD child=s.pid;EnumChildWindows(w,child_match,reinterpret_cast<LPARAM>(&child));if(child)return TRUE;}
 RECT r{};GetClientRect(w,&r);long long area=static_cast<long long>(r.right)*r.bottom;
 if(area>s.area){s.area=area;s.best=w;}return TRUE;
}
}
bool Renderer::initialize(HMODULE module, ModuleManager& modules) noexcept {
 module_=module;modules_=&modules;
 stop_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);ready_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 if(!stop_event_ || !ready_event_){shutdown();return false;}
 thread_=CreateThread(nullptr,0,thread_entry,this,0,nullptr);
 if(!thread_ || WaitForSingleObject(ready_event_,2000)!=WAIT_OBJECT_0 || !initialized_){shutdown();return false;}
 return true;
}
void Renderer::shutdown() noexcept {
 if(stop_event_)SetEvent(stop_event_);
 if(thread_){WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);thread_=nullptr;}
 if(stop_event_){CloseHandle(stop_event_);stop_event_=nullptr;}
 if(ready_event_){CloseHandle(ready_event_);ready_event_=nullptr;}
 initialized_=false;
}
bool Renderer::initialized() const noexcept {return initialized_.load();}
DWORD WINAPI Renderer::thread_entry(void* p) noexcept {return static_cast<Renderer*>(p)->run();}
DWORD Renderer::run() noexcept {
 WNDCLASSEXW c{sizeof(c)};c.lpfnWndProc=window_procedure;c.hInstance=module_;
 c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.lpszClassName=overlay_class_name;
 if(!RegisterClassExW(&c) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){SetEvent(ready_event_);return 1;}
 overlay_window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,overlay_class_name,L"clickGUI",WS_POPUP,
  0,0,1,1,nullptr,nullptr,module_,this);
 if(!overlay_window_){SetEvent(ready_event_);return 1;}
 SetLayeredWindowAttributes(overlay_window_,transparent_key,255,LWA_COLORKEY);
 active_renderer=this;keyboard_hook_=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard_hook,module_,0);
 if(!keyboard_hook_){DestroyWindow(overlay_window_);active_renderer=nullptr;SetEvent(ready_event_);return 1;}
 initialized_=true;SetEvent(ready_event_);
 while(WaitForSingleObject(stop_event_,0)==WAIT_TIMEOUT){
  MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
  tick();
  if(IsWindowVisible(overlay_window_)) {
   // Paint this iteration, then pace to desktop presentation. The old 16 ms
   // sleep drifted against refresh and deferred each paint to the next loop.
   UpdateWindow(overlay_window_);GdiFlush();
   if(FAILED(DwmFlush()))WaitForSingleObject(stop_event_,16);
  } else WaitForSingleObject(stop_event_,16);
 }
 if(modules_)modules_->commands().suspend();
 set_menu_open(false,false);UnhookWindowsHookEx(keyboard_hook_);keyboard_hook_=nullptr;active_renderer=nullptr;
 if(back_buffer_device_){SelectObject(back_buffer_device_,back_buffer_previous_bitmap_);DeleteObject(back_buffer_bitmap_);DeleteDC(back_buffer_device_);back_buffer_device_=nullptr;}
 DestroyWindow(overlay_window_);overlay_window_=nullptr;UnregisterClassW(overlay_class_name,module_);initialized_=false;return 0;
}
LRESULT CALLBACK Renderer::window_procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
 auto* renderer=reinterpret_cast<Renderer*>(GetWindowLongPtrW(window,GWLP_USERDATA));
 if(message==WM_NCCREATE){renderer=static_cast<Renderer*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(renderer));}
 switch(message){
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:if(renderer)renderer->paint(window);return 0;
 case WM_NCHITTEST:return renderer && renderer->menu_visible_ ? HTCLIENT : HTTRANSPARENT;
 case legacy_key_message:if(renderer)renderer->legacy_key(static_cast<int>(wparam));return 0;
 case WM_SETCURSOR:SetCursor(LoadCursorW(nullptr,IDC_ARROW));return TRUE;
 case toggle_menu_message:if(renderer)renderer->set_menu_open(!renderer->menu_visible_);return 0;
 case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:
  if(renderer)renderer->click_at(GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam),message==WM_RBUTTONDOWN);return 0;
 case WM_MOUSEMOVE:
  if(renderer && renderer->slider_module_) renderer->drag_slider(GET_X_LPARAM(lparam));return 0;
 case WM_LBUTTONUP:
  if(renderer && renderer->slider_module_){renderer->drag_slider(GET_X_LPARAM(lparam));renderer->slider_module_=nullptr;ReleaseCapture();}return 0;
 case WM_CAPTURECHANGED:if(renderer)renderer->slider_module_=nullptr;return 0;
 case WM_MOUSEWHEEL:
  if(renderer){renderer->scroll_offset_=std::clamp(renderer->scroll_offset_-GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*48,0,(std::max)(0,renderer->content_height_-renderer->viewport_height_));renderer->hits_.clear();InvalidateRect(window,nullptr,FALSE);}return 0;
 case WM_CLOSE:if(renderer)renderer->set_menu_open(false);return 0;
 }
 return DefWindowProcW(window,message,wparam,lparam);
}
HWND Renderer::find_game_window() const noexcept {Search s{GetCurrentProcessId(),overlay_window_};EnumWindows(window_match,reinterpret_cast<LPARAM>(&s));return s.best;}
bool Renderer::game_is_foreground() const noexcept {
 HWND fg=GetForegroundWindow();return fg && game_window_ && (fg==overlay_window_ || fg==game_window_ || GetAncestor(fg,GA_ROOT)==GetAncestor(game_window_,GA_ROOT));
}
LRESULT CALLBACK Renderer::keyboard_hook(int code, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = active_renderer;
    if (code == HC_ACTION && self) {
        const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        const bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        CURSORINFO input_cursor{sizeof(input_cursor)};
        const bool gameplay = !self->menu_visible_ && self->world_ready_ && self->game_is_foreground() &&
            GetCursorInfo(&input_cursor) && !(input_cursor.flags & CURSOR_SHOWING);
        if (self->modules_) self->modules_->commands().key(key.vkCode, down, gameplay);
        // Releases must clear module edge state even after focus/menu changes.
        if (!down && self->modules_)
            for (const auto& module : self->modules_->modules()) module->on_key_up(key.vkCode);
        // Preserve short gameplay taps until the next native player tick.
        // Do not queue typing from chat, inventory, or clickGUI.
        if (down && !self->menu_visible_ && self->world_ready_ && self->game_is_foreground() && self->modules_) {
            CURSORINFO cursor{sizeof(cursor)};
            if (GetCursorInfo(&cursor) && !(cursor.flags & CURSOR_SHOWING))
                for (const auto& module : self->modules_->modules())
                    if (module->enabled()) module->on_key_down(key.vkCode);
        }
        if (key.vkCode >= VK_LEFT && key.vkCode <= VK_DOWN) {
            const auto index = key.vkCode - VK_LEFT;
            const bool held = self->arrow_held_[index];
            self->arrow_held_[index] = down;
            if (gameplay) {
                if (down && !held) PostMessageW(self->overlay_window_, legacy_key_message, key.vkCode, 0);
                return 1;
            }
        }
        if (key.vkCode == VK_TAB) {
            if (!down) { self->tab_held_ = false; }
            if (self->game_is_foreground() && (gameplay || self->menu_visible_)) {
                if (down && !self->tab_held_) PostMessageW(self->overlay_window_, toggle_menu_message, 0, 0);
                self->tab_held_ = down;
                return 1;
            }
        }
        if (key.vkCode == VK_ESCAPE && self->menu_visible_ && self->game_is_foreground()) {
            if (down) PostMessageW(self->overlay_window_, WM_CLOSE, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

void Renderer::set_menu_open(bool open, bool return_focus) noexcept {
    if (open == menu_visible_) return;
    menu_visible_ = open;
    if (open && modules_) modules_->commands().suspend();
    slider_module_ = nullptr;
    if (GetCapture() == overlay_window_) ReleaseCapture();
    hits_.clear();
    SetWindowLongPtrW(overlay_window_, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TOPMOST |
        (open ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT));
    SetWindowPos(overlay_window_, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (open) {
        settings_module_ = nullptr;
        scroll_offset_ = 0;
        ClipCursor(nullptr);
        do { ++cursor_increments_; } while (ShowCursor(TRUE) < 0);
        // Layered windows need a populated surface before they can take focus.
        if (!IsWindow(game_window_)) game_window_ = find_game_window();
        RECT client{}; POINT origin{};
        if (game_window_ && GetClientRect(game_window_, &client) && ClientToScreen(game_window_, &origin)) {
            SetWindowPos(overlay_window_, HWND_TOPMOST, origin.x, origin.y, client.right, client.bottom,
                SWP_NOACTIVATE);
        }
        InvalidateRect(overlay_window_, nullptr, FALSE);
        UpdateWindow(overlay_window_);
        ShowWindow(overlay_window_, SW_SHOW);
        SetForegroundWindow(overlay_window_);
        SetFocus(overlay_window_);
        tick();
    } else {
        ShowWindow(overlay_window_, SW_HIDE);
        while (cursor_increments_ > 0) { ShowCursor(FALSE); --cursor_increments_; }
        if (return_focus && IsWindow(game_window_)) SetForegroundWindow(game_window_);
    }
}

void Renderer::tick() noexcept {
    CURSORINFO bind_cursor{sizeof(bind_cursor)};
    if (modules_ && (menu_visible_ || !world_ready_ || !game_is_foreground() ||
        !GetCursorInfo(&bind_cursor) || (bind_cursor.flags & CURSOR_SHOWING))) modules_->commands().suspend();
    if (!IsWindow(game_window_)) game_window_ = find_game_window();
    if (!game_window_) { set_menu_open(false, false); ShowWindow(overlay_window_, SW_HIDE); return; }
    const bool foreground = game_is_foreground();
    if (!foreground) {
        set_menu_open(false, false);
        ShowWindow(overlay_window_, SW_HIDE);
        world_ready_ = false;
        hidden_cursor_ticks_ = 0;
        return;
    }
    if (!menu_visible_) {
        CURSORINFO cursor{sizeof(CURSORINFO)};
        if (GetCursorInfo(&cursor)) {
            if (!(cursor.flags & CURSOR_SHOWING)) {
                hidden_cursor_ticks_ = (std::min)(hidden_cursor_ticks_ + 1, enter_world_ticks);
                world_ready_ = hidden_cursor_ticks_ >= enter_world_ticks;
            } else { hidden_cursor_ticks_ = 0; world_ready_ = false; }
        }
    }
    if (modules_ && world_ready_ && !menu_visible_) modules_->tick();
    if (!menu_visible_ && !world_ready_) { ShowWindow(overlay_window_, SW_HIDE); return; }
    if (menu_visible_) ClipCursor(nullptr);
    RECT client{}; POINT origin{};
    if (!GetClientRect(game_window_, &client) || !ClientToScreen(game_window_, &origin) || IsIconic(game_window_)) {
        set_menu_open(false, false); ShowWindow(overlay_window_, SW_HIDE); return;
    }
    SetWindowPos(overlay_window_, HWND_TOPMOST, origin.x, origin.y, client.right, client.bottom,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(overlay_window_, nullptr, FALSE);
}

void Renderer::click_at(int x, int y, bool right) noexcept {
    if (!menu_visible_) return;
    const POINT point{x, y};
    for (const auto& hit : hits_) {
        if (!PtInRect(&hit.rect, point)) continue;
        if (hit.action == 0) {
            if (right) {
                if (has_settings(hit.module))
                    settings_module_ = settings_module_ == hit.module ? nullptr : hit.module;
            }
            else hit.module->set_enabled(!hit.module->enabled());
        } else if (hit.action == 2 && !right && hit.module->available()) {
            hit.module->set_boolean_setting(hit.setting,!hit.module->boolean_setting(hit.setting));
        } else if (!right && hit.module->available()) {
            slider_module_ = hit.module;
            slider_rect_ = hit.rect;
            SetCapture(overlay_window_);
            drag_slider(x);
        }
        hits_.clear();
        InvalidateRect(overlay_window_, nullptr, FALSE);
        return;
    }
}


void Renderer::drag_slider(int x) noexcept {
    if (!slider_module_ || !menu_visible_) return;
    const float fraction = std::clamp(static_cast<float>(x - slider_rect_.left) /
        (std::max)(1L, slider_rect_.right - slider_rect_.left - 1), 0.0F, 1.0F);
    slider_module_->set_value(slider_module_->minimum_value() + fraction *
        (slider_module_->maximum_value() - slider_module_->minimum_value()));
    InvalidateRect(overlay_window_, nullptr, FALSE);
}

void Renderer::legacy_key(int key) noexcept {
    if (menu_visible_ || !world_ready_) return;
    const auto rows = rows_for_category(*modules_, categories[legacy_category_].category);
    if (key == VK_LEFT) legacy_expanded_ = false;
    if (key == VK_RIGHT) {
        if (!legacy_expanded_) { legacy_expanded_ = true; legacy_module_ = 0; }
        else if (!rows.empty()) {
            auto* module = rows[legacy_module_].module;
            module->set_enabled(!module->enabled());
        }
    }
    if (key == VK_UP || key == VK_DOWN) {
        const int direction = key == VK_UP ? -1 : 1;
        if (!legacy_expanded_) legacy_category_ = (legacy_category_ + direction + 7) % 7;
        else if (!rows.empty()) legacy_module_ = (legacy_module_ + direction + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
    }
    InvalidateRect(overlay_window_, nullptr, FALSE);
}

void Renderer::paint_legacy(HDC device) noexcept {
    hits_.clear();
    const auto rows = rows_for_category(*modules_, categories[legacy_category_].category);
    const int count = legacy_expanded_ ? (std::max)(1, static_cast<int>(rows.size())) : 7;
    fill_rectangle(device, {12, 12, 266, 64 + count * 26}, RGB(25, 29, 34));
    const auto font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const auto previous = SelectObject(device, font);
    SetBkMode(device, TRANSPARENT);
    auto line = [&](std::wstring text, int y, COLORREF color) {
        SetTextColor(device, color); RECT rect{22, y, 256, y + 26};
        DrawTextW(device, text.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    line(legacy_expanded_ ? categories[legacy_category_].name : L"Utility Modules", 18, RGB(86, 213, 234));
    for (int i = 0; i < count; ++i) {
        const bool selected = i == (legacy_expanded_ ? legacy_module_ : legacy_category_);
        std::wstring text = selected ? L"> " : L"  ";
        COLORREF color = selected ? RGB(86, 213, 234) : RGB(205, 211, 216);
        if (!legacy_expanded_) text += categories[i].name;
        else if (rows.empty()) text = L"No modules";
        else {
            const auto* module = rows[i].module;
            text += widen(module->name());
            text += !module->available() ? L"  N/A" : module->enabled() ? L"  ON" : L"  OFF";
            if (!module->available()) color = RGB(111, 119, 129);
        }
        line(text, 48 + i * 26, color);
    }
    SelectObject(device, previous); DeleteObject(font);
}

void Renderer::paint(HWND window) noexcept {
 PAINTSTRUCT ps{};HDC target_device=BeginPaint(window,&ps);if(!target_device)return;
 RECT client{};GetClientRect(window,&client);const int client_width=client.right,client_height=client.bottom;
 if(client_width<=0 || client_height<=0){EndPaint(window,&ps);return;}
 if(!back_buffer_device_ || back_buffer_width_!=client_width || back_buffer_height_!=client_height){
  if(back_buffer_device_){SelectObject(back_buffer_device_,back_buffer_previous_bitmap_);DeleteObject(back_buffer_bitmap_);DeleteDC(back_buffer_device_);back_buffer_device_=nullptr;}
  back_buffer_device_=CreateCompatibleDC(target_device);back_buffer_bitmap_=CreateCompatibleBitmap(target_device,client_width,client_height);
  if(back_buffer_device_ && back_buffer_bitmap_){back_buffer_previous_bitmap_=SelectObject(back_buffer_device_,back_buffer_bitmap_);back_buffer_width_=client_width;back_buffer_height_=client_height;}
  else {if(back_buffer_device_)DeleteDC(back_buffer_device_);if(back_buffer_bitmap_)DeleteObject(back_buffer_bitmap_);back_buffer_device_=nullptr;}
 }
 HDC device=back_buffer_device_?back_buffer_device_:target_device;
    // Color-key only the backdrop; category panels remain readable and opaque.
    fill_rectangle(device, client, transparent_key);
    for (const auto& module : modules_->modules()) if (module->enabled()) module->draw_overlay(device, client_width, client_height);
    if (!menu_visible_) {
        paint_legacy(device);
        const auto array_list = std::find_if(modules_->modules().begin(), modules_->modules().end(),
            [](const auto& module) { return module->enabled() && module->shows_array_list(); });
        if (array_list != modules_->modules().end()) {
            const bool left=(*array_list)->boolean_setting();
            const UINT alignment=left?DT_LEFT:DT_RIGHT;
            std::vector<Module*> enabled;
            for (const auto& module : modules_->modules()) if (module->enabled()) enabled.push_back(module.get());
            std::sort(enabled.begin(), enabled.end(), [](const auto* a, const auto* b) {
                return a->name().size() == b->name().size() ? a->name() < b->name() : a->name().size() > b->name().size();
            });
            const auto font = CreateFontW(-17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            const auto old_font = SelectObject(device, font);
            SetBkMode(device, TRANSPARENT);
            const int count = legacy_expanded_ ? (std::max)(1, static_cast<int>(rows_for_category(*modules_, categories[legacy_category_].category).size())) : 7;
            int y = left ? 74 + count * 26 : 14;
            for (const auto* module : enabled) {
                auto text = widen(module->name());
                RECT shadow{15,y+1,client_width-13,y+24}; SetTextColor(device,RGB(0,0,0));
                DrawTextW(device,text.c_str(),-1,&shadow,alignment|DT_SINGLELINE|DT_NOPREFIX);
                RECT line{14,y,client_width-14,y+23}; SetTextColor(device,RGB(86,213,234));
                DrawTextW(device,text.c_str(),-1,&line,alignment|DT_SINGLELINE|DT_NOPREFIX);
                y += 23;
            }
            SelectObject(device,old_font); DeleteObject(font);
        }
        if (back_buffer_device_) BitBlt(target_device, 0, 0, client_width, client_height, device, 0, 0, SRCCOPY);
        EndPaint(window, &ps);
        return;
    }
    SetBkMode(device, TRANSPARENT);
    const int gap = 12;
    const int columns = (std::max)(1, (std::min)(7, (client_width - gap) / 180));
    const int width = (client_width - gap * (columns + 1)) / columns;
    const HFONT body_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const HGDIOBJ previous_font = SelectObject(device, body_font);
    auto label = [&](std::wstring_view text, RECT rect, COLORREF color, UINT align = DT_LEFT) {
        SetTextColor(device, color);
        DrawTextW(device, text.data(), static_cast<int>(text.size()), &rect,
            align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    hits_.clear();
    if (settings_module_ && !has_settings(settings_module_)) settings_module_ = nullptr;
    viewport_height_ = (std::max)(1, client_height - 45);
    int y = 16 - scroll_offset_;
    int group_height = 0;
    // Include Gui, wrapping categories on narrower game windows.
    for (int category = 0; category < 7; ++category) {
        if (category && category % columns == 0) { y += group_height + gap; group_height = 0; }
        const int x = gap + (category % columns) * (width + gap);
        const auto rows = rows_for_category(*modules_, categories[category].category);
        int height = 42 + static_cast<int>((std::max)(size_t{1}, rows.size())) * 30 + 8;
        const auto settings_height=[](const Module* module){
            return module->has_value()?66:(std::max)(66,static_cast<int>(module->boolean_setting_count())*30+12);
        };
        for (const auto& row : rows) if (row.module == settings_module_) height += settings_height(row.module);
        group_height = (std::max)(group_height, height);
        HBRUSH brush = CreateSolidBrush(RGB(25, 29, 34));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(52, 183, 204));
        const auto old_brush = SelectObject(device, brush);
        const auto old_pen = SelectObject(device, pen);
        RoundRect(device, x, y, x + width, y + height, 12, 12);
        SelectObject(device, old_pen); SelectObject(device, old_brush);
        DeleteObject(pen); DeleteObject(brush);
        label(categories[category].name, {x + 8, y + 3, x + width - 8, y + 38}, RGB(238, 242, 245), DT_CENTER);
        int row_y = y + 42;
        if (rows.empty()) label(L"No modules", {x + 12, row_y, x + width - 10, row_y + 30}, RGB(117, 125, 135));
        for (const auto& row : rows) {
            auto* module = row.module;
            RECT rect{x + 5, row_y, x + width - 5, row_y + 30};
            if (module->enabled()) fill_rectangle(device, rect, RGB(28, 58, 66));
            label(widen(module->name()), {x + 12, row_y, x + width - 42, row_y + 30},
                !module->available() ? RGB(111, 119, 129) : module->enabled() ? RGB(86, 213, 234) : RGB(205, 211, 216));
            label(module->available() ? (has_settings(module) ? (settings_module_ == module ? L"-" : L"+") : L"") : L"N/A",
                {x + width - 42, row_y, x + width - 12, row_y + 30}, RGB(170, 185, 195), DT_RIGHT);
            if (rect.top >= 0 && rect.bottom <= viewport_height_) hits_.push_back({rect, module, 0});
            row_y += 30;
            if (settings_module_ == module) {
                const int expanded_height=settings_height(module);
                fill_rectangle(device, {x + 6, row_y, x + width - 6, row_y + expanded_height - 4}, RGB(18, 22, 27));
                if (module->has_value()) {
                    wchar_t value[80]{};
                    swprintf_s(value, L"%ls: %.*f%ls", widen(module->value_label()).c_str(),
                        module->value_decimals(), static_cast<double>(module->value()), widen(module->value_suffix()).c_str());
                    label(value, {x + 12, row_y, x + width - 12, row_y + 28}, RGB(203, 215, 225));
                    RECT slider{x + 12, row_y + 30, x + width - 12, row_y + 58};
                    const float range = module->maximum_value() - module->minimum_value();
                    const float fraction = range > 0 ? std::clamp((module->value() - module->minimum_value()) / range, 0.0F, 1.0F) : 0;
                    const int thumb = slider.left + static_cast<int>(fraction * (slider.right - slider.left - 1));
                    const int middle = (slider.top + slider.bottom) / 2;
                    fill_rectangle(device, {slider.left, middle - 2, slider.right, middle + 3}, RGB(60, 72, 82));
                    fill_rectangle(device, {slider.left, middle - 2, thumb, middle + 3}, RGB(52, 183, 204));
                    fill_rectangle(device, {(std::max)(slider.left, static_cast<LONG>(thumb - 4)), middle - 8,
                        (std::min)(slider.right, static_cast<LONG>(thumb + 5)), middle + 9}, RGB(86, 213, 234));
                    if (slider.top >= 0 && slider.bottom <= viewport_height_) hits_.push_back({slider, module, 1});
                } else if (module->boolean_setting_count()) {
                    for(std::size_t setting=0;setting<module->boolean_setting_count();++setting){
                        RECT toggle{x + 12, row_y + 6 + static_cast<int>(setting)*30,
                            x + width - 12, row_y + 36 + static_cast<int>(setting)*30};
                        auto text = std::wstring(module->boolean_setting(setting) ? L"[x] " : L"[ ] ") + widen(module->boolean_setting_name(setting));
                        label(text, toggle, RGB(203,215,225));
                        if (toggle.top >= 0 && toggle.bottom <= viewport_height_) hits_.push_back({toggle, module, 2, setting});
                    }
                }
                row_y += expanded_height;
            }
        }
    }
    content_height_ = y + scroll_offset_ + group_height + 12;
    const int maximum_scroll = (std::max)(0, content_height_ - viewport_height_);
    if (scroll_offset_ > maximum_scroll) {
        scroll_offset_ = maximum_scroll;
        hits_.clear();
        InvalidateRect(window, nullptr, FALSE);
    }
    fill_rectangle(device, {0, viewport_height_, client_width, client_height}, transparent_key);
    label(L"TAB / ESC  Close     Left click  Toggle     Right click  Settings     Scroll  More",
        {16, viewport_height_, client_width - 16, client_height}, RGB(146, 163, 175));

    SelectObject(device,previous_font);DeleteObject(body_font);
    if(back_buffer_device_)BitBlt(target_device,0,0,client_width,client_height,back_buffer_device_,0,0,SRCCOPY);
    EndPaint(window,&ps);
}
} // namespace utility::integration







