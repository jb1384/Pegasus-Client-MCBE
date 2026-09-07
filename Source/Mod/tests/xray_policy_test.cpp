#include "modules/XrayModule.hpp"
#include <cstdio>

int main() {
    using Xray = utility::modules::XrayModule;
    using Decision = Xray::MeshDecision;
    unsigned failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    for (const auto id : {"minecraft:diamond_ore", "minecraft:deepslate_diamond_ore",
            "minecraft:lit_redstone_ore", "minecraft:lit_deepslate_redstone_ore",
            "minecraft:quartz_ore", "minecraft:nether_gold_ore", "minecraft:ancient_debris"}) {
        check(Xray::classify(true, id) == Decision::all_faces, "Buried ore must emit all faces");
        check(Xray::classify(false, id) == Decision::vanilla, "Disabled must preserve vanilla meshing");
    }
    for (const auto id : {"minecraft:stone", "minecraft:bedrock",
            "custom:diamond_ore", "minecraft:diamond_ore_extra"}) {
        check(Xray::classify(true, id) == Decision::omit, "Only exact configured ores are retained");
        check(Xray::classify(false, id) == Decision::vanilla, "Disabled must preserve terrain");
    }
    check(Xray::classify(true, {}) == Decision::vanilla, "Unknown identity must preserve meshing");
    struct Example { const char* id; Xray::Visibility option; };
    constexpr Example examples[]{
        {"minecraft:water", Xray::Visibility::liquids}, {"minecraft:flowing_water", Xray::Visibility::liquids},
        {"minecraft:lava", Xray::Visibility::liquids}, {"minecraft:flowing_lava", Xray::Visibility::liquids},
        {"minecraft:gravel", Xray::Visibility::gravel}, {"minecraft:suspicious_gravel", Xray::Visibility::gravel},
        {"minecraft:sand", Xray::Visibility::sand}, {"minecraft:red_sand", Xray::Visibility::sand},
        {"minecraft:suspicious_sand", Xray::Visibility::sand}, {"minecraft:bedrock", Xray::Visibility::bedrock},
        {"minecraft:mob_spawner", Xray::Visibility::spawners}, {"minecraft:trial_spawner", Xray::Visibility::spawners},
        {"minecraft:chest", Xray::Visibility::storage}, {"minecraft:trapped_chest", Xray::Visibility::storage},
        {"minecraft:ender_chest", Xray::Visibility::storage}, {"minecraft:barrel", Xray::Visibility::storage},
        {"minecraft:copper_chest", Xray::Visibility::storage}, {"minecraft:waxed_oxidized_copper_chest", Xray::Visibility::storage},
        {"minecraft:undyed_shulker_box", Xray::Visibility::storage}, {"minecraft:light_blue_shulker_box", Xray::Visibility::storage},
        {"minecraft:black_shulker_box", Xray::Visibility::storage}, {"minecraft:hopper", Xray::Visibility::storage},
        {"minecraft:dispenser", Xray::Visibility::storage}, {"minecraft:dropper", Xray::Visibility::storage},
        {"minecraft:furnace", Xray::Visibility::storage}, {"minecraft:lit_blast_furnace", Xray::Visibility::storage},
        {"minecraft:lit_smoker", Xray::Visibility::storage}, {"minecraft:brewing_stand", Xray::Visibility::storage},
        {"minecraft:crafter", Xray::Visibility::storage}};
    for(unsigned mask=0;mask<64;++mask) {
        for(const auto& example:examples) {
            const auto expected=(mask & Xray::visibility_bit(example.option))?Decision::all_faces:Decision::omit;
            check(Xray::classify(true,example.id,mask)==expected,"Category visibility must be independent for every combination");
            check(Xray::classify(false,example.id,mask)==Decision::vanilla,"Disabled x-ray must preserve all selected types");
        }
        check(Xray::classify(true,"minecraft:diamond_ore",mask)==Decision::all_faces,"Options must never hide ores");
        for(const auto id:{"minecraft:sandstone", "minecraft:soul_sand", "custom:chest", "minecraft:chest_extra",
                "minecraft:fake_shulker_box", "minecraft:invisible_bedrock", "minecraft:waterlily"})
            check(Xray::classify(true,id,mask)==Decision::omit,"Similar names must not match categories");
    }
    for(const auto& example:examples)
        check(Xray::classify(true,example.id)==(example.option==Xray::Visibility::liquids?Decision::all_faces:Decision::omit),
            "Only liquids should be selected by default");
    Xray module;
    utility::Module& settings=module;
    check(settings.boolean_setting_count()==6,"Menu must expose six options");
    for(std::size_t i=0;i<6;++i) {
        check(!settings.boolean_setting_name(i).empty(),"Each option needs a menu label");
        check(settings.boolean_setting(i)==(i==0),"Menu defaults must match visibility defaults");
        settings.set_boolean_setting(i,i!=0);
        check(settings.boolean_setting(i)==(i!=0),"Menu option must toggle while module is disabled");
    }
    settings.set_boolean_setting(6,true);
    settings.set_boolean_setting(static_cast<std::size_t>(-1),true);
    check(!settings.boolean_setting(6)&&settings.boolean_setting_name(6).empty(),"Out-of-range settings are ignored");
    for(std::size_t i=0;i<6;++i) {
        check(settings.boolean_setting(i)==(i!=0),"Editing another option must preserve existing choices");
        settings.set_boolean_setting(i,i==0);
    }
    module.set_enabled(true);
    check(!module.available() && !module.enabled(), "Unverified integration cannot enable");
    check(module.mesh_decision("minecraft:stone") == Decision::vanilla, "Unavailable module leaves terrain unchanged");
    return failures ? 1 : 0;
}
