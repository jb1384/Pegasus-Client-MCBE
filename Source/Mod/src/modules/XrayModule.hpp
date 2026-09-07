#pragma once

#include "../framework/Module.hpp"

#include <array>
#include <string_view>

namespace utility::modules {

// Visibility policy and version-gated native terrain integration.
class XrayModule final : public Module {
public:
    enum class MeshDecision { vanilla, omit, all_faces };

    [[nodiscard]] std::string_view name() const noexcept override { return "x-ray"; }
    [[nodiscard]] ModuleCategory category() const noexcept override { return ModuleCategory::visual; }
    enum class Visibility : unsigned { liquids, gravel, sand, bedrock, spawners, storage, count };
    static constexpr unsigned setting_count = static_cast<unsigned>(Visibility::count);
    static constexpr unsigned default_visibility = 1U << static_cast<unsigned>(Visibility::liquids);
    static constexpr unsigned visibility_bit(Visibility option) noexcept { return 1U << static_cast<unsigned>(option); }
    std::size_t boolean_setting_count() const noexcept override { return setting_count; }
    std::string_view boolean_setting_name(std::size_t index) const noexcept override {
        constexpr std::array<std::string_view, setting_count> labels{
            "Liquids (water/lava)", "Gravel", "Sand", "Bedrock", "Monster spawners", "Storage containers"};
        return index < labels.size() ? labels[index] : std::string_view{};
    }
    bool boolean_setting(std::size_t index) const noexcept override {
        return index < setting_count && (visibility_.load() & (1U << index)) != 0;
    }
    void set_boolean_setting(std::size_t index, bool value) noexcept override;
    ~XrayModule() override;
    [[nodiscard]] bool available() const noexcept override;
    void on_register(EventBus&) override;
    void on_enable() override;
    void on_disable() override;

    [[nodiscard]] static constexpr bool valuable(std::string_view identifier) noexcept {
        for (const auto ore : ores) {
            if (identifier == ore) return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr bool retained(std::string_view id, unsigned visibility) noexcept {
        if (valuable(id)) return true;
        const auto selected = [visibility](Visibility option) { return (visibility & visibility_bit(option)) != 0; };
        if (id == "minecraft:water" || id == "minecraft:flowing_water" ||
            id == "minecraft:lava" || id == "minecraft:flowing_lava") return selected(Visibility::liquids);
        if (id == "minecraft:gravel" || id == "minecraft:suspicious_gravel") return selected(Visibility::gravel);
        if (id == "minecraft:sand" || id == "minecraft:red_sand" || id == "minecraft:suspicious_sand") return selected(Visibility::sand);
        if (id == "minecraft:bedrock") return selected(Visibility::bedrock);
        if (id == "minecraft:mob_spawner" || id == "minecraft:trial_spawner") return selected(Visibility::spawners);
        if (!selected(Visibility::storage) || !id.starts_with("minecraft:")) return false;
        id.remove_prefix(10);
        constexpr std::string_view containers[]{
            "chest", "trapped_chest", "ender_chest", "barrel", "hopper", "dispenser", "dropper",
            "furnace", "lit_furnace", "blast_furnace", "lit_blast_furnace", "smoker", "lit_smoker", "brewing_stand", "crafter",
            "copper_chest", "exposed_copper_chest", "weathered_copper_chest", "oxidized_copper_chest",
            "waxed_copper_chest", "waxed_exposed_copper_chest", "waxed_weathered_copper_chest", "waxed_oxidized_copper_chest",
            "shulker_box", "undyed_shulker_box"};
        for (const auto container : containers) if (id == container) return true;
        constexpr std::string_view colors[]{"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink",
            "gray", "light_gray", "silver", "cyan", "purple", "blue", "brown", "green", "red", "black"};
        for (const auto color : colors)
            if (id.starts_with(color) && id.substr(color.size()) == "_shulker_box") return true;
        return false;
    }

    // Unknown identities retain vanilla meshing. The native adapter preserves
    // selected render shapes and rebuilds loaded chunks after every policy change.
    [[nodiscard]] static constexpr MeshDecision classify(
        bool active, std::string_view identifier, unsigned visibility = default_visibility) noexcept {
        if (!active || identifier.empty()) return MeshDecision::vanilla;
        return retained(identifier, visibility) ? MeshDecision::all_faces : MeshDecision::omit;
    }

    [[nodiscard]] MeshDecision mesh_decision(std::string_view identifier) const noexcept {
        return classify(enabled(), identifier, visibility_.load());
    }

private:
    std::atomic<unsigned> visibility_{default_visibility};
    static constexpr std::array<std::string_view, 21> ores{
        "minecraft:coal_ore", "minecraft:deepslate_coal_ore",
        "minecraft:copper_ore", "minecraft:deepslate_copper_ore",
        "minecraft:iron_ore", "minecraft:deepslate_iron_ore",
        "minecraft:gold_ore", "minecraft:deepslate_gold_ore",
        "minecraft:redstone_ore", "minecraft:lit_redstone_ore",
        "minecraft:deepslate_redstone_ore", "minecraft:lit_deepslate_redstone_ore",
        "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore",
        "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore",
        "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore",
        "minecraft:nether_gold_ore", "minecraft:quartz_ore", "minecraft:ancient_debris"
    };
};

} // namespace utility::modules
