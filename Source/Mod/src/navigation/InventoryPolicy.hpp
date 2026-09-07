#pragma once
#include "Navigation.hpp"
#include <algorithm>
#include <limits>
#include <cmath>

namespace utility::navigation {
// The adapter supplies game-derived stack/loot facts; policies do not guess item
// layouts, enchantment multipliers, stack sizes, or loot tables from block names.
struct Stack {
    std::string id;
    int slot{-1}, count{}, maximum_count{}, durability{};
    bool damageable{}, safe_food{}, special_food{};
    int nutrition{};
    double break_seconds{};
    bool can_harvest{};
};
struct DropRule { std::string block;std::vector<std::string> possible_items;bool verified{}; };
inline bool throwaway(const std::string& id) {
    return id=="minecraft:cobblestone"||id=="minecraft:dirt"||id=="minecraft:netherrack";
}
inline bool usable_food(const Stack& item) {
    return item.count>0&&item.nutrition>0&&item.safe_food&&!item.special_food&&
        item.id!="minecraft:chorus_fruit"&&item.id!="minecraft:golden_apple"&&item.id!="minecraft:enchanted_golden_apple";
}
inline int food_slot(const std::vector<Stack>& inventory,int hunger) {
    const Stack* best{};
    for(const auto& item:inventory)if(usable_food(item)) {
        const auto score=[&](const Stack& s){return std::abs(s.nutrition-std::max(0,20-hunger));};
        if(!best||score(item)<score(*best)||(score(item)==score(*best)&&item.slot<best->slot))best=&item;
    }
    return best?best->slot:-1;
}
inline int tool_slot(const std::vector<Stack>& tools) {
    const Stack* best{};
    for(const auto& item:tools)if(item.count>0&&item.can_harvest&&item.break_seconds>0&&
        std::isfinite(item.break_seconds)&&(!item.damageable||item.durability>1)) {
        if(!best||item.break_seconds<best->break_seconds||
           (item.break_seconds==best->break_seconds&&item.slot<best->slot))best=&item;
    }
    return best?best->slot:-1;
}
struct DropAccounting { bool verified{}, capacity{};int count{}; };
inline DropAccounting count_drops(const std::vector<std::string>& targets,
    const std::vector<DropRule>& rules,const std::vector<Stack>& inventory) {
    DropAccounting out;out.verified=true;std::unordered_set<std::string> matching;
    for(const auto& id:targets) {
        const auto rule=std::find_if(rules.begin(),rules.end(),[&](const DropRule& r){return r.block==id;});
        if(rule==rules.end()||!rule->verified||rule->possible_items.empty()){out.verified=false;return out;}
        matching.insert(rule->possible_items.begin(),rule->possible_items.end());
    }
    std::unordered_set<std::string> capacity;bool empty{};
    for(const auto& stack:inventory) {
        if(stack.count==0){empty=true;continue;}
        if(!matching.contains(stack.id))continue;
        if(stack.count>0)out.count=static_cast<int>(std::min<std::int64_t>(
            static_cast<std::int64_t>(out.count)+stack.count,std::numeric_limits<int>::max()));
        if(stack.count<stack.maximum_count)capacity.insert(stack.id);
    }
    out.capacity=empty||std::all_of(matching.begin(),matching.end(),[&](const auto& id){return capacity.contains(id);});
    return out;
}
} // namespace utility::navigation
