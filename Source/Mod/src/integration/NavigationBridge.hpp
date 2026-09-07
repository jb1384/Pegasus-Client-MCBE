#pragma once
#include <atomic>

namespace utility::integration {
// Callback objects live until process exit, matching the pinned gameplay hooks.
struct NavigationTickSink {
    virtual ~NavigationTickSink() = default;
    virtual void native_tick(void* player) noexcept = 0;
    virtual void suspend_navigation() noexcept = 0;
};
inline std::atomic<NavigationTickSink*> navigation_sink{};
inline std::atomic_bool navigation_owns_controls{};
inline void navigation_tick(void* player) noexcept {
    if(auto* sink=navigation_sink.load(std::memory_order_acquire))sink->native_tick(player);
}
inline void navigation_suspend() noexcept {
    if(auto* sink=navigation_sink.load(std::memory_order_acquire))sink->suspend_navigation();
}
} // namespace utility::integration
