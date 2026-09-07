#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <array>
#include <cmath>
#include <cstring>

namespace utility::integration::navigation_native {
inline constexpr std::uint32_t timestamp = 0x6A8378BA;
inline constexpr std::uint32_t image_size = 0x12888000;
inline constexpr std::uintptr_t player_vtable = 0xE820EC0;
inline constexpr std::uintptr_t tick_rva = 0x2F2C3E0;
inline constexpr std::uint32_t move_input_hash = 0x018B1887;
inline constexpr std::size_t move_input_size = 0x64;
inline constexpr std::uint32_t raw_input_hash = 0x3613E513;
inline constexpr std::size_t raw_input_size = 24;
inline constexpr std::uint32_t attributes_hash = 0xFD3B0613;
inline constexpr std::size_t attributes_size = 80;
inline constexpr std::uintptr_t health_definition = 0x11ADC9C8;
inline constexpr std::uintptr_t hunger_definition = 0x11ACAFF0;

// AttributesComponent stores sorted uint32 keys and parallel 128-byte instances.
// Missing attributes return failure, never the native default-instance fallback.
template<class Read>
bool attribute(std::uintptr_t attributes,std::uint32_t key,float& value,Read read) {
    std::uintptr_t first{},last{},values{},values_end{};
    if(!attributes||!read(attributes,&first,8)||!read(attributes+8,&last,8)||
       !read(attributes+0x18,&values,8)||!read(attributes+0x20,&values_end,8)||
       !first||last<first||(last-first)%4||(last-first)/4>128||!values||
       values_end<values||(values_end-values)%128||
       (values_end-values)/128!=(last-first)/4)return false;
    for(std::size_t i=0;i<(last-first)/4;++i) {
        std::uint32_t candidate{};
        if(!read(first+i*4,&candidate,4))return false;
        if(candidate!=key)continue;
        float observed{};
        if(!read(values+i*128+0x7C,&observed,4)||!std::isfinite(observed))return false;
        value=observed;return true;
    }
    return false;
}

struct ObservedInput {
    float sideways{}, forward{};
    bool valid{}, active{};
};
// Read-only decoding supported by the 2026-09-07 manual-input capture.
// Input flags remain opaque: they are not a verified contract for writes.
inline ObservedInput decode_input(const std::array<unsigned char,move_input_size>& bytes) noexcept {
    ObservedInput result;
    std::memcpy(&result.sideways,bytes.data()+0x24,4);
    std::memcpy(&result.forward,bytes.data()+0x28,4);
    result.valid=std::isfinite(result.sideways)&&std::isfinite(result.forward)&&
        std::abs(result.sideways)<=1.001f&&std::abs(result.forward)<=1.001f;
    if(!result.valid)return result;
    result.active=std::abs(result.sideways)>0.001f||std::abs(result.forward)>0.001f;
    // Conservatively treat any of the first observed button flags as activity.
    // This intentionally does not claim which bit is attack, sprint, or jump.
    for(std::size_t i=0;i<4;++i)result.active=result.active||bytes[i]!=0;
    return result;
}

// Read is bool(address, destination, bytes). No native calls or retained pointers.
// Layout verified against the supported executable's sparse-set accessors.
template<class Read>
std::uintptr_t component(std::uintptr_t registry, std::uint32_t entity,
                         std::uint32_t hash, std::size_t stride, Read read) {
    if (!registry || stride > 4096) return 0;
    auto get = [&]<class T>(std::uintptr_t at, T& value) { return read(at, &value, sizeof(T)); };
    std::uintptr_t first{}, last{};
    if (!get(registry + 0x68, first) || !get(registry + 0x70, last) || !first ||
        last < first || (last-first)%32 || (last-first)/32 > 4096) return 0;
    for (auto at=first; at<last; at+=32) {
        std::uint32_t key{}; std::uintptr_t pool{};
        if (!get(at+8,key) || !get(at+16,pool)) return 0;
        if (key != hash) continue;
        if (!pool) return 0;
        std::uintptr_t sparse{}, sparse_end{}, page{}, packed_first{}, packed_end{}, pages{}, data{};
        const auto index = entity & 0x3FFFFu;
        if (!get(pool+8,sparse) || !get(pool+16,sparse_end) || !sparse || sparse_end<sparse ||
            (sparse_end-sparse)%8 || (sparse_end-sparse)/8 > 128 ||
            index/2048 >= (sparse_end-sparse)/8 || !get(sparse+(index/2048)*8,page) || !page) return 0;
        std::uint32_t packed{}, stored{};
        if (!get(page+(index%2048)*4,packed) || (packed^(entity & 0xFFFC0000u))>=0x3FFFFu) return 0;
        const auto slot = packed & 0x3FFFFu;
        if (!get(pool+0x20,packed_first) || !get(pool+0x28,packed_end) || !packed_first ||
            packed_end<packed_first || (packed_end-packed_first)%4 ||
            (packed_end-packed_first)/4>0x3FFFF || slot >= (packed_end-packed_first)/4 ||
            !get(packed_first+slot*4,stored) || stored != entity) return 0;
        if(!stride)return 1; // Membership-only query for empty flag components.
        if(!get(pool+0x50,pages) || !pages || !get(pages+(slot/128)*8,data) || !data) return 0;
        const auto offset=(slot%128)*stride;
        if (data > std::numeric_limits<std::uintptr_t>::max()-offset-stride) return 0;
        return data+offset;
    }
    return 0;
}
}
