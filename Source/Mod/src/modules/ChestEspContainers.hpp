#pragma once
#include "../integration/GameContext.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace utility::modules::chest_esp {
enum class Kind : unsigned { barrel,chest,shulker,copper,trapped,count };
constexpr unsigned kind_count=static_cast<unsigned>(Kind::count);
constexpr unsigned all_types=(1U<<kind_count)-1;
constexpr std::array<std::string_view,kind_count> labels{"Barrels","Chests","Shulker boxes","Copper chests","Trapped chests"};
inline Kind classify(std::string_view name) {
    if(name=="minecraft:barrel")return Kind::barrel;
    if(name=="minecraft:chest")return Kind::chest;
    if(name=="minecraft:trapped_chest")return Kind::trapped;
    constexpr std::string_view copper[]{"copper_chest","exposed_copper_chest","weathered_copper_chest","oxidized_copper_chest",
        "waxed_copper_chest","waxed_exposed_copper_chest","waxed_weathered_copper_chest","waxed_oxidized_copper_chest"};
    if(!name.starts_with("minecraft:"))return Kind::count;
    name.remove_prefix(10);
    for(auto type:copper)if(name==type)return Kind::copper;
    if(name=="shulker_box"||name=="undyed_shulker_box")return Kind::shulker;
    constexpr std::string_view colors[]{"white","orange","magenta","light_blue","yellow","lime","pink","gray","light_gray","silver","cyan","purple","blue","brown","green","red","black"};
    for(auto color:colors)if(name.starts_with(color)&&name.substr(color.size())=="_shulker_box")return Kind::shulker;
    return Kind::count;
}
struct Position { int x{},y{},z{}; };
struct Storage { Position position; float bounds[6]{}; Kind kind{Kind::count}; };
using Observer=void(*)(Position,std::string_view,Kind);
template<class T> T read(const void* object,std::size_t offset=0) {
    T value{};
    if(object&&integration::readable_game_memory(static_cast<const unsigned char*>(object)+offset,sizeof(T)))
        std::memcpy(&value,static_cast<const unsigned char*>(object)+offset,sizeof(T));
    return value;
}
inline std::string identifier(const void* legacy) {
    if(!legacy)return {};
    const auto* field=static_cast<const unsigned char*>(legacy)+0xE8;
    const auto size=read<std::size_t>(field,16),capacity=read<std::size_t>(field,24);
    if(!size||size>128||capacity<size||capacity>65536)return {};
    const auto* chars=capacity>=16?read<const char*>(field):reinterpret_cast<const char*>(field);
    return integration::readable_game_memory(chars,size)?std::string(chars,size):std::string{};
}
inline bool belongs_to_chunk(Position p,int chunk_x,int chunk_z,int min_y,int max_y,std::uint32_t key) {
    // Floor division for negative coordinates, independent of compiler right-shift rules.
    const auto chunk=[](int value){return value/16-(value%16<0?1:0);};
    if(chunk(p.x)!=chunk_x||chunk(p.z)!=chunk_z||p.y<min_y||p.y>=max_y)return false;
    const auto packed=(static_cast<unsigned>(p.x)&15U)|((static_cast<unsigned>(p.z)&15U)<<8)|
        (static_cast<unsigned>(p.y-min_y)<<16);
    return packed==key;
}
// Run exclusively from the local player's native tick. BlockSource::getChunk
// is a loaded-chunk lookup, not a generation/load request. No world edits occur.
inline bool capture(void* player,unsigned char* image,std::vector<Storage>& output,Observer observer=nullptr) {
    output.clear();
    auto* dimension=read<void*>(player,0x1C8);
    auto* region=read<void*>(dimension,0xF0);
    auto* table=read<void*>(region);
    if(read<void*>(table,0x10)!=image+0x2486DC0||read<void*>(table,0x148)!=image+0x24860E0)return false;
    auto* source=read<void*>(region,0x28);
    if(read<void*>(read<void*>(source),0x18)!=image+0x598CE20)return false;
    const auto min_y=read<short>(region,0x3A),max_y=read<short>(region,0x38);
    const auto* head=read<void*>(source,0x78);const auto count=read<std::size_t>(source,0x80);
    if(!head||count>16384||min_y>=max_y)return false;
    using GetChunk=void*(__fastcall*)(void*,const void*);
    using GetBlock=void*(__fastcall*)(void*,const Position*);
    const auto get_chunk=reinterpret_cast<GetChunk>(image+0x24860E0);
    const auto get_block=reinterpret_cast<GetBlock>(image+0x2486DC0);
    auto* node=read<void*>(head);
    for(std::size_t index=0;index<count&&node&&node!=head;++index) {
        auto* next=read<void*>(node);
        const auto cached_chunk=read<void*>(node,0x18);
        // Empty chunks dominate; avoid a native lookup for those.
        const auto cached_count=read<std::size_t>(cached_chunk,0x13A8);
        if(cached_count&&cached_count<=65536) {
            const int coords[]{read<int>(node,0x10),read<int>(node,0x14)};
            auto* chunk=get_chunk(region,coords);
            if(chunk&&read<int>(chunk,0x78)==coords[0]&&read<int>(chunk,0x7C)==coords[1]) {
                const auto* actors_head=read<void*>(chunk,0x13A0);
                const auto actor_count=read<std::size_t>(chunk,0x13A8);
                auto* actor_node=read<void*>(actors_head);
                for(std::size_t actor_index=0;actor_index<actor_count&&actor_count<=65536&&actor_node&&actor_node!=actors_head;++actor_index) {
                    auto* actor=read<void*>(actor_node,0x18);
                    const auto key=read<std::uint32_t>(actor_node,0x10);
                    const auto position=read<Position>(actor,8);
                    if(actor&&belongs_to_chunk(position,coords[0],coords[1],min_y,max_y,key)) {
                        const auto* block=get_block(region,&position);
                        const auto name=identifier(read<void*>(block,0x68));
                        const auto kind=classify(name);
                        if(observer)observer(position,name,kind);
                        if(kind!=Kind::count) {
                            Storage storage{position,{},kind};
                            // Native BlockActor bounds represent the occupied block, including both
                            // halves of double containers as separate block records.
                            for(int axis=0;axis<6;++axis)storage.bounds[axis]=read<float>(actor,0x48+axis*4);
                            bool valid=true;
                            const int components[]{position.x,position.y,position.z};
                            for(int axis=0;axis<3;++axis)valid=valid&&std::isfinite(storage.bounds[axis])&&
                                std::isfinite(storage.bounds[axis+3])&&storage.bounds[axis+3]>storage.bounds[axis]&&
                                std::abs(storage.bounds[axis]-static_cast<float>(components[axis]))<2&&
                                std::abs(storage.bounds[axis+3]-static_cast<float>(components[axis]+1))<2;
                            if(valid)output.push_back(storage);
                        }
                    }
                    actor_node=read<void*>(actor_node);
                }
            }
        }
        node=next;
    }
    return true;
}
}
