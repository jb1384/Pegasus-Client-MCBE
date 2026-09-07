#include "integration/NavigationNativeLayout.hpp"
#include "integration/NavigationInputPolicy.hpp"
#include "integration/NavigationSnapshotPolicy.hpp"
#include <set>
#include "integration/NavigationInventoryAccess.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
int main() {
    using namespace utility::integration::navigation_native;
    std::array<unsigned char,0x10000> memory{};
    auto put=[&]<class T>(std::size_t at,T value){std::memcpy(memory.data()+at,&value,sizeof(value));};
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at>memory.size()||size>memory.size()-at)return false;
        std::memcpy(out,memory.data()+at,size);return true;
    };
    auto ptr=[&](std::size_t at,std::uintptr_t value){put(at,value);};
    constexpr std::uint32_t entity=0xC0005;
    ptr(0x168,0x200);ptr(0x170,0x220);put(0x208,move_input_hash);ptr(0x210,0x300);
    ptr(0x308,0x400);ptr(0x310,0x408);ptr(0x400,0x500);
    put(0x514,std::uint32_t{0xC0001});ptr(0x320,0x3000);ptr(0x328,0x3008);
    put(0x3004,entity);ptr(0x350,0x3100);ptr(0x3100,0x4000);
    auto resolve=[&]{return component(0x100,entity,move_input_hash,move_input_size,read);};
    auto check=[](bool ok){if(!ok)throw std::runtime_error("component resolver assertion failed");};
    check(resolve()==0x4064);
    check(component(0x100,entity,move_input_hash,0,read)==1);
    put(0x514,std::uint32_t{0x80001});check(resolve()==0); // stale generation
    put(0x514,std::uint32_t{0xFFFFFFFF});check(resolve()==0); // tombstone
    put(0x514,std::uint32_t{0xC0002});check(resolve()==0); // outside packed array
    put(0x514,std::uint32_t{0xC0001});put(0x3004,std::uint32_t{entity+1});check(resolve()==0);
    put(0x3004,entity);ptr(0x310,0x400);check(resolve()==0); // absent sparse page
    ptr(0x310,0x408);ptr(0x3100,~std::uintptr_t{}-20);check(resolve()==0);
    ptr(0x3100,0x4000);ptr(0x170,0x201);check(resolve()==0); // corrupt registry bounds
    std::array<unsigned char,move_input_size> input{};
    check(decode_input(input).valid&&!decode_input(input).active);
    auto axes=[&](float sideways,float forward) {
        std::memcpy(input.data()+0x24,&sideways,4);std::memcpy(input.data()+0x28,&forward,4);
    };
    for(const auto axis : {-1.0f,-0.3f,0.3f,1.0f}) {
        axes(axis,0);check(decode_input(input).valid&&decode_input(input).active&&decode_input(input).sideways==axis);
        axes(0,axis);check(decode_input(input).valid&&decode_input(input).active&&decode_input(input).forward==axis);
    }
    axes(0,0);input[0]=0x80;check(decode_input(input).active); // observed jump activity
    input[0]=0;input[2]=0x20;check(decode_input(input).active); // observed sneak activity
    axes(2,0);check(!decode_input(input).valid);
    axes(std::numeric_limits<float>::quiet_NaN(),0);check(!decode_input(input).valid);
    axes(0,std::numeric_limits<float>::infinity());check(!decode_input(input).valid);
    const auto forward=steer(0,10,0);check(forward.valid&&forward.buttons==0x2000&&forward.forward==1);
    const auto backward=steer(0,-10,0);check(backward.buttons==0x4000&&backward.forward== -1);
    const auto left=steer(10,0,0);check(left.buttons==0x8000&&left.sideways==1);
    const auto east=steer(10,0,-90);check(east.buttons==0x2000&&east.forward==1);
    const auto diagonal=steer(10,10,0);check(diagonal.buttons==0xA000&&std::abs(diagonal.forward-0.70710678f)<0.00001f);
    check(!steer(0,0,0).valid);check(!steer(0,1,std::numeric_limits<float>::infinity()).valid);
    const auto look=look_at(10,0,0,0,0);check(look.valid&&look.yaw== -15&&look.pitch==0);
    const auto wrapped=look_at(0,0,-10,0,179);check(wrapped.valid&&std::abs(angle_delta(wrapped.yaw-180))<0.001f);
    check(!look_at(0,0,0,0,0).valid);
    const Steering captured{-0.0613523088f,0.9981161952f,0x12000,true};
    check(owned_axes(captured.sideways,captured.forward,captured));
    check(owned_axes(-0.7071067691f,0.7071067691f,captured));
    check(!owned_axes(0.7071067691f,0.7071067691f,captured));
    check(!owned_axes(0,1,captured));
    check(!owned_axes(std::numeric_limits<float>::quiet_NaN(),0,captured));
    const auto almost_forward=steer(-0.0613523088,0.9981161952,0);
    check(almost_forward.buttons==0x2000&&almost_forward.sideways==0&&almost_forward.forward==1);
    check(owned_axes(0,1,almost_forward));
    check(steer(0.4,1,0).buttons==0x2000);
    check(steer(0.42,1,0).buttons==0xA000);
    check(steer(-1,0.4,0).buttons==0x10000);
    const auto& offsets=snapshot_offsets();
    std::set<std::array<int,3>> unique(offsets.begin(),offsets.end());
    check(unique.size()==offsets.size());
    for(unsigned i=0;i<25*25*4;++i)check(offsets[i][1]>=-1&&offsets[i][1]<=2);
    for(unsigned i=0;i<36;++i)check(std::abs(offsets[i][0])<=1&&std::abs(offsets[i][2])<=1);
    ptr(0x800,0x900);ptr(0x808,0x908);ptr(0x818,0xA00);ptr(0x820,0xB00);
    put(0x900,std::uint32_t{3});put(0x904,std::uint32_t{7});put(0xA7C,8.5f);put(0xAFC,14.0f);
    float value{};check(attribute(0x800,3,value,read)&&value==8.5f);
    check(attribute(0x800,7,value,read)&&value==14.0f);check(!attribute(0x800,8,value,read));
    ptr(0x820,0xA80);check(!attribute(0x800,7,value,read));ptr(0x820,0xB00);
    put(0xAFC,std::numeric_limits<float>::quiet_NaN());check(!attribute(0x800,7,value,read));
    ptr(0xC08,0xD00);ptr(0xD08,0xE00);put(0xD19,static_cast<unsigned char>(1));
    ptr(0xE00,0xD00);ptr(0xE10,0xD00);ptr(0xE30,6);ptr(0xE38,15);
    std::memcpy(memory.data()+0xE20,"Damage",6);put(0xE68,static_cast<unsigned char>(3));put(0xE48,14);
    bool present{};int damage{};check(nbt_integer(0xC00,"Damage",damage,present,read)&&present&&damage==14);
    check(nbt_integer(0xC00,"Absent",damage,present,read)&&!present&&damage==0);
    put(0xE68,static_cast<unsigned char>(2));put(0xE48,static_cast<short>(7));
    check(nbt_integer(0xC00,"Damage",damage,present,read)&&damage==7);
    put(0xE68,static_cast<unsigned char>(8));check(!nbt_integer(0xC00,"Damage",damage,present,read));
    ptr(0xE00,0xE00);check(!nbt_integer(0xC00,"Absent",damage,present,read));
    std::cout<<"Native component membership checks passed (synthetic memory).\n";
}
