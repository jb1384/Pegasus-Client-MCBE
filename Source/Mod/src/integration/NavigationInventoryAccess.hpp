#pragma once
#include "NavigationWorldAccess.hpp"
#include <climits>
#include <string_view>
namespace utility::integration::navigation_native {
template<class Read>
bool nbt_integer(std::uintptr_t compound,std::string_view key,int& value,bool& present,Read read) {
    present=false;
    if(!compound){value=0;return true;}
    std::uintptr_t head{};
    if(!read(compound+8,&head,8)||!head)return false;
    std::uintptr_t node{};if(!read(head+8,&node,8))return false;
    for(unsigned depth=0;depth<128;++depth) {
        unsigned char sentinel{};if(!node||!read(node+0x19,&sentinel,1))return false;
        if(sentinel){value=0;return true;}
        std::size_t length{},capacity{};std::uintptr_t pointer=node+0x20;
        if(!read(node+0x30,&length,8)||!read(node+0x38,&capacity,8)||length>128||capacity<length)return false;
        if(capacity>=16&&!read(node+0x20,&pointer,8))return false;
        char name[128]{};if(length&&!read(pointer,name,length))return false;
        const auto compare=key.compare(std::string_view(name,length));
        if(!compare) {
            unsigned char type{};if(!read(node+0x68,&type,1))return false;
            if(type==3){if(!read(node+0x48,&value,4))return false;}
            else if(type==2){short number{};if(!read(node+0x48,&number,2))return false;value=number;}
            else return false;
            present=true;return true;
        }
        if(!read(node+(compare<0?0:0x10),&node,8))return false;
    }
    return false;
}
struct NativeStack {
    char identifier[128]{};
    int count{},maximum_count{},damage{},maximum_damage{},durability{};
    bool valid{};
};
inline bool capture_stack(std::uintptr_t stack,NativeStack& result) noexcept {
    result={};
    unsigned char count{};if(!world_copy(stack+0x22,&count,1))return false;
    result.count=count;
    if(!count){result.valid=true;return true;}
    const auto item=world_read<std::uintptr_t>(world_read<std::uintptr_t>(stack+8));
    if(!item)return false;
    const auto name=item+0x128;const auto length=world_read<std::size_t>(name+16),capacity=world_read<std::size_t>(name+24);
    if(!length||length>=128||capacity<length||capacity>65536||
       !world_copy(capacity<16?name:world_read<std::uintptr_t>(name),result.identifier,length))return false;
    result.maximum_count=world_read<unsigned char>(item+0xA8);
    result.maximum_damage=world_read<unsigned short>(item+0x150);
    if(result.maximum_count<1||result.maximum_count>255||count>result.maximum_count)return false;
    bool present{};
    if(!nbt_integer(world_read<std::uintptr_t>(stack+0x10),"Damage",result.damage,present,world_copy)||
       result.damage<0||(result.maximum_damage&&result.damage>result.maximum_damage))return false;
    result.durability=result.maximum_damage?result.maximum_damage-result.damage:INT_MAX;
    result.valid=true;return true;
}
struct NativeInventory {NativeStack slots[36]{};unsigned selected{},count{};bool valid{};};
inline bool capture_inventory(std::uintptr_t image,std::uintptr_t player,NativeInventory& output) noexcept {
    output={};
    __try {
        const auto supplies=world_read<std::uintptr_t>(player+0x5B8);
        const auto inventory=world_read<std::uintptr_t>(supplies+0xB8);
        const auto table=world_read<std::uintptr_t>(inventory);
        const auto getter=world_read<std::uintptr_t>(table+0x38),size=world_read<std::uintptr_t>(table+0xA0);
        output.selected=world_read<unsigned>(supplies+0x10);
        if(output.selected>8||getter<image||getter>=image+0x12888000||size<image||size>=image+0x12888000)return false;
        const auto count=reinterpret_cast<int(__fastcall*)(std::uintptr_t)>(size)(inventory);
        if(count<36||count>256)return false;
        for(unsigned slot=0;slot<36;++slot) {
            const auto stack=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,int)>(getter)(inventory,slot);
            if(!capture_stack(stack,output.slots[slot]))return false;
        }
        output.count=36;output.valid=true;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
