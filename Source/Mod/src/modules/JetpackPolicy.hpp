#pragma once
#include <algorithm>
#include <cmath>

namespace utility::modules::jetpack {
constexpr float minimum_speed = 4.3F;
constexpr float maximum_speed = 100.0F;
constexpr float default_speed = 10.0F;
struct Velocity { float x{}, y{}, z{}; };
inline bool finite(Velocity v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
// Velocity is in blocks per simulation tick (20 Hz). Keep our own filtered
// command so native drag/gravity do not accumulate into steering drift.
class Controller {
    Velocity command_{};
    bool active_{};
public:
    void reset() noexcept { active_ = false; }
    bool update(float pitch, float yaw, float speed, Velocity current, Velocity& result) noexcept {
        if (!std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(speed) || !finite(current)) {
            reset(); return false;
        }
        constexpr float radians = 0.01745329252F;
        pitch = std::clamp(pitch, -90.0F, 90.0F) * radians;
        yaw = std::remainder(yaw, 360.0F) * radians;
        const float magnitude = std::clamp(speed, minimum_speed, maximum_speed) / 20.0F;
        const Velocity target{-std::sin(yaw)*std::cos(pitch)*magnitude,
            -std::sin(pitch)*magnitude, std::cos(yaw)*std::cos(pitch)*magnitude};
        if (!active_) { command_ = current; active_ = true; }
        // Exponential response: ~90% of a new speed/direction in 0.35 seconds.
        constexpr float blend = 0.28F;
        command_.x += (target.x-command_.x)*blend;
        command_.y += (target.y-command_.y)*blend;
        command_.z += (target.z-command_.z)*blend;
        result = command_;
        return true;
    }
};
}
