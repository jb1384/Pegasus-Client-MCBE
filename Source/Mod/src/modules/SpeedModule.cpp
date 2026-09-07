#include "SpeedModule.hpp"

#include "../framework/Logger.hpp"
#include "../integration/GameContext.hpp"
#include "../integration/NavigationBridge.hpp"

#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace utility::modules {
namespace {

constexpr std::uint32_t supported_timestamp = 0x6A8378BA;
constexpr std::uint32_t supported_image_size = 0x12888000;
constexpr std::uintptr_t state_vector_offset = 0x218;
constexpr std::uintptr_t velocity_offset = 0x18;
constexpr float maximum_horizontal_speed = 0.22F;
constexpr float acceleration_scale = 1.35F;

[[nodiscard]] bool supported_image() noexcept {
    HMODULE image = GetModuleHandleW(nullptr);
    if (image == nullptr) {
        return false;
    }
    const auto* base = reinterpret_cast<const std::byte*>(image);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.TimeDateStamp == supported_timestamp &&
        nt->OptionalHeader.SizeOfImage == supported_image_size;
}

[[nodiscard]] bool movement_key_down() noexcept {
    return (GetAsyncKeyState('W') & 0x8000) != 0 ||
        (GetAsyncKeyState('A') & 0x8000) != 0 ||
        (GetAsyncKeyState('S') & 0x8000) != 0 ||
        (GetAsyncKeyState('D') & 0x8000) != 0;
}

} // namespace

std::string_view SpeedModule::name() const noexcept { return "Speed"; }
ModuleCategory SpeedModule::category() const noexcept { return ModuleCategory::movement; }
bool SpeedModule::available() const noexcept { return supported_.load(std::memory_order_acquire); }

void SpeedModule::on_register(EventBus&) {
    supported_.store(supported_image(), std::memory_order_release);
    Logger::instance().info(supported_.load(std::memory_order_acquire)
        ? "Speed ready for Minecraft 1.26.4501.0."
        : "Speed unavailable: the exact 1.26.4501.0 player layout was not present.");
}

void SpeedModule::on_enable() { Logger::instance().info("Speed enabled."); }
void SpeedModule::on_disable() { Logger::instance().info("Speed disabled."); }

void SpeedModule::on_tick() noexcept {
    if (integration::navigation_owns_controls.load()) return;
    if (!supported_.load(std::memory_order_acquire) || !movement_key_down()) {
        return;
    }
    auto* player = static_cast<std::byte*>(integration::current_player());
    if (!integration::readable_game_memory(player, state_vector_offset + sizeof(void*))) {
        return;
    }
    auto* state = *reinterpret_cast<std::byte**>(player + state_vector_offset);
    if (!integration::readable_game_memory(state, velocity_offset + sizeof(float) * 3)) {
        return;
    }
    auto* velocity = reinterpret_cast<float*>(state + velocity_offset);
    const float x = velocity[0];
    const float z = velocity[2];
    if (!std::isfinite(x) || !std::isfinite(z)) {
        return;
    }
    const float magnitude_squared = x * x + z * z;
    if (magnitude_squared < 0.000025F || magnitude_squared >=
        maximum_horizontal_speed * maximum_horizontal_speed) {
        return;
    }
    const float magnitude = std::sqrt(magnitude_squared);
    const float next = (std::min)(maximum_horizontal_speed, magnitude * acceleration_scale);
    const float scale = next / magnitude;
    velocity[0] = x * scale;
    velocity[2] = z * scale;
}

} // namespace utility::modules
