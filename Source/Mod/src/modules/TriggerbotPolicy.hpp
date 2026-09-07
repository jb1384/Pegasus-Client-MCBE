#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace utility::modules::triggerbot {
enum class Target { none, mob, player };
constexpr unsigned mobs = 1, players = 2, all_targets = mobs | players;
// The adapter supplies a positive native health check, not just an actor name.
constexpr Target classify(std::string_view name, bool living) noexcept {
    if (!living || name.empty()) return Target::none;
    if (name == "minecraft:player" || name.starts_with("minecraft:player.")) return Target::player;
    if (name == "minecraft:armor_stand" || name == "minecraft:npc" || name == "minecraft:agent") return Target::none;
    return name.find(':') != std::string_view::npos ? Target::mob : Target::none;
}
constexpr bool selected(Target target, unsigned options) noexcept {
    return target == Target::mob ? (options & mobs) != 0 : target == Target::player && (options & players) != 0;
}
struct TargetKey {
    std::uintptr_t actor{}, world{};
    std::uint32_t generation{};
    bool operator==(const TargetKey&) const = default;
};
// Game-thread scheduler. The seed can be fixed by tests; production seeds once
// per process. No sleeps, input synthesis, queued clicks, or retained Actor*.
class Cadence {
public:
    explicit Cadence(std::uint32_t seed = 1) : random_(seed ? seed : 1) {}
    void reset() noexcept { target_ = {}; due_ = 0; last_ = 0; previous_ = 0; burst_ = 0; }
    bool update(double now, TargetKey target, bool eligible) noexcept {
        if (!std::isfinite(now) || now < 0 || !eligible || !target.actor) { reset(); return false; }
        if (now < previous_ || (previous_ > 0 && now - previous_ > 0.30)) reset();
        previous_ = now;
        if (!(target == target_)) {
            target_ = target; due_ = now + between(0.130, 0.220); last_ = now;
            pace_ = between(0.072, 0.090); burst_ = 18 + next() % 18;
            return false;
        }
        if (now < due_ || now - last_ < 0.049) return false;
        last_ = now;
        pace_ = std::clamp(pace_ + between(-0.004, 0.004), 0.070, 0.092);
        double interval = std::clamp(pace_ + between(-0.011, 0.011), 0.060, 0.105);
        if (--burst_ == 0) { interval += between(0.100, 0.180); burst_ = 18 + next() % 18; }
        // Preserve fractional timing at 20 Hz without catching up after a stall.
        due_ = (std::max)(due_ + interval, now + 0.035);
        return true;
    }
private:
    std::uint32_t next() noexcept { random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5; return random_; }
    double between(double low, double high) noexcept { return low + (high - low) * (next() / 4294967295.0); }
    std::uint32_t random_, burst_{};
    TargetKey target_{};
    double due_{}, last_{}, previous_{}, pace_{};
};
}
