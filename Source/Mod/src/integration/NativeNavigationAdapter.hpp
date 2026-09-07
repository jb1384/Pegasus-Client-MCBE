#pragma once
#include "../navigation/Navigation.hpp"
#include <string>
#include <unordered_set>
#include "NavigationInputPolicy.hpp"

namespace utility::integration {

// This capability report distinguishes identified layouts from verified callable
// contracts. Executable identity alone must never enable the automation.
struct NavigationCapabilities {
    bool executable{}, player_observation{}, block_identifiers{};
    bool world_snapshot{}, movement{}, aiming{}, mining{}, placement{}, interaction{};
    bool inventory{}, survival{}, food{}, drops{};
    bool complete() const noexcept;
    std::string missing() const;
};

class NativeNavigationAdapter final : public navigation::Actions {
public:
    void initialize();
    const NavigationCapabilities& capabilities() const noexcept { return capabilities_; }
    bool available() const noexcept { return capabilities_.complete(); }
    std::string unavailable_reason() const;
    navigation::Frame observe(void* player,const std::vector<std::string>& mining_targets = {});
    bool known_block(const std::string& id) const;
    void release() noexcept override;
    bool move(const navigation::Step&, bool sprint) override;
    bool mine(navigation::BlockPos, int tool_slot) override;
    bool place(navigation::BlockPos, int slot) override;
    bool open(navigation::BlockPos) override;
    bool eat(int slot) override;
    bool aim_at(navigation::Vec3 target);
    struct InputObservation {
        std::uint32_t flags{};
        float sideways{},forward{},pitch{},yaw{},expected_pitch{},expected_yaw{};
        bool physical{},owned{},aim_owned{},view_known{};
    };
    const InputObservation& input_observation() const noexcept {return input_observation_;}
private:
    InputObservation input_observation_{};
    NavigationCapabilities capabilities_;
    std::unordered_set<std::string> identifiers_;
    std::uintptr_t base_{};
    void* previous_player_{};
    void* previous_dimension_{};
    std::uint64_t session_{};
    void* action_player_{};
    navigation::Vec3 action_feet_{};
    navigation_native::Steering submitted_{};
    bool input_owned_{};
    bool mining_interfaces_{},mining_owned_{};
    navigation::BlockPos mining_target_{};
    void stop_mining() noexcept;
    bool write_movement(navigation_native::Steering input) noexcept;
    navigation::World observed_world_;
    std::unordered_map<navigation::BlockPos,std::uint64_t,navigation::PosHash> observed_hashes_;
    std::shared_ptr<const navigation::World> published_world_;
    std::uint64_t snapshot_time_{};
    unsigned scan_cursor_{};
    float last_view_[2]{};
    bool view_known_{},aim_owned_{};
    std::shared_ptr<const navigation::World> snapshot_world(void* player);
};
} // namespace utility::integration
