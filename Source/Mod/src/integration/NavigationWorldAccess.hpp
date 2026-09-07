#pragma once
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace utility::integration::navigation_native {
struct NativePosition {int x{},y{},z{};};
struct NativeBox {float bounds[6]{};};
struct NativeCell {
    char identifier[128]{};
    NativeBox boxes[16]{};
    std::uint32_t count{},valid{};
};
inline bool world_copy(std::uintptr_t at,void* out,std::size_t size) noexcept {
    __try {if(!at)return false;std::memcpy(out,reinterpret_cast<void*>(at),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> T world_read(std::uintptr_t at) noexcept {T value{};world_copy(at,&value,sizeof(value));return value;}
struct NativeBoxVector {std::uintptr_t first{},last{},end{};};
inline bool destroy_boxes(std::uintptr_t image,NativeBoxVector& vector) noexcept {
    if(!vector.first)return true;
    if(vector.end<vector.first||vector.end-vector.first>1024*1024)return false;
    auto pointer=vector.first;auto bytes=vector.end-vector.first;
    if(bytes>=4096) {
        const auto allocation=world_read<std::uintptr_t>(pointer-8);
        if(!allocation||pointer<allocation+8||pointer-allocation>39)return false;
        pointer=allocation;bytes+=39;
    }
    __try {reinterpret_cast<void(__fastcall*)(std::uintptr_t,std::size_t)>(image+0xDF384D0)(pointer,bytes);vector={};return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Caller must validate executable identity. All native access occurs on game tick.
// The native collector owns its vector allocation; never supply STL debug vectors.
inline bool capture_cell(std::uintptr_t image,std::uintptr_t region,NativePosition position,NativeCell& result) noexcept {
    result={};NativeBoxVector vector{};bool success=false;
    __try {
        const auto source_table=world_read<std::uintptr_t>(region);
        if(world_read<std::uintptr_t>(source_table+0x10)!=image+0x2486DC0||
           world_read<std::uintptr_t>(source_table+0x148)!=image+0x24860E0)return false;
        // An unloaded chunk must never be misrepresented by getBlock's air fallback.
        const int chunk[]{position.x/16-(position.x%16<0),position.z/16-(position.z%16<0)};
        const auto loaded=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,const int*)>(image+0x24860E0)(region,chunk);
        if(!loaded)return false;
        const auto min_y=world_read<short>(region+0x3A),max_y=world_read<short>(region+0x38);
        if(position.y<min_y||position.y>=max_y)return false;
        const auto block=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,const NativePosition*)>(image+0x2486DC0)(region,&position);
        const auto legacy=world_read<std::uintptr_t>(block+0x68);if(!legacy)return false;
        const auto name=legacy+0xE8;const auto size=world_read<std::size_t>(name+16),capacity=world_read<std::size_t>(name+24);
        if(!size||size>=sizeof(result.identifier)||capacity<size||capacity>65536||
           !world_copy(capacity<16?name:world_read<std::uintptr_t>(name),result.identifier,size))return false;
        const auto function=world_read<std::uintptr_t>(world_read<std::uintptr_t>(legacy)+0x38);
        if(function<image||function>=image+0x12888000)return false;
        using Collect=void(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,const NativePosition*,const NativeBox*,NativeBoxVector*);
        reinterpret_cast<Collect>(function)(legacy,block,region,&position,nullptr,&vector);
        if(vector.last>=vector.first&&vector.last<=vector.end&&(vector.last-vector.first)%24==0&&
           (vector.last-vector.first)/24<=16) {
            result.count=static_cast<std::uint32_t>((vector.last-vector.first)/24);
            success=!result.count||world_copy(vector.first,result.boxes,result.count*sizeof(NativeBox));
            const int origin[]{position.x,position.y,position.z};
            for(std::uint32_t i=0;i<result.count&&success;++i)for(int axis=0;axis<3;++axis) {
                const float low=result.boxes[i].bounds[axis],high=result.boxes[i].bounds[axis+3];
                // Retain over-height collision such as fences, reject implausible results.
                success=success&&std::isfinite(low)&&std::isfinite(high)&&high>low&&
                    low>=origin[axis]-1.01&&high<=origin[axis]+2.01;
            }
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){success=false;}
    const bool freed=destroy_boxes(image,vector);
    result.valid=success&&freed?1u:0u;return result.valid!=0;
}
}
