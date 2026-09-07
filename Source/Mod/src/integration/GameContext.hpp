#pragma once

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace utility::integration {

namespace game_context_detail {
inline std::atomic<void*> player{};
inline std::atomic<void*> crosshair_player{}, crosshair_hit{};
inline std::atomic<ULONGLONG> crosshair_time{};
inline std::atomic_bool picker_ready{false};

inline bool readable(const void* address, std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) == 0 ||
        information.State != MEM_COMMIT || (information.Protect & PAGE_GUARD) != 0 ||
        information.Protect == PAGE_NOACCESS) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = begin + size;
    const auto region_end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
        information.RegionSize;
    return end >= begin && end <= region_end;
}
} // namespace game_context_detail

inline void observe_game_mode(void* game_mode) noexcept {
    if (!game_context_detail::readable(game_mode, 16)) {
        return;
    }
    void* candidate = *reinterpret_cast<void**>(
        static_cast<std::byte*>(game_mode) + sizeof(void*));
    if (!game_context_detail::readable(candidate, sizeof(void*))) {
        return;
    }
    HMODULE image = GetModuleHandleW(nullptr);
    if (image == nullptr) {
        return;
    }
    const auto table = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(candidate));
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    if (table != base + 0xE820EC0 && table != base + 0xE833530) {
        return;
    }
    game_context_detail::player.store(candidate, std::memory_order_release);
}

[[nodiscard]] inline void* current_player() noexcept {
    return game_context_detail::player.load(std::memory_order_acquire);
}

// Called only after the native picker has completed its range validation.
inline void observe_crosshair(void* player, void* hit) noexcept {
    game_context_detail::crosshair_time.store(0);
    game_context_detail::crosshair_player.store(player);
    game_context_detail::crosshair_hit.store(hit);
    game_context_detail::crosshair_time.store(GetTickCount64());
}

inline void clear_game_context() noexcept {
    game_context_detail::crosshair_time.store(0);
    game_context_detail::player.store(nullptr, std::memory_order_release);
}

[[nodiscard]] inline bool readable_game_memory(const void* address, std::size_t size) noexcept {
    return game_context_detail::readable(address, size);
}

} // namespace utility::integration
