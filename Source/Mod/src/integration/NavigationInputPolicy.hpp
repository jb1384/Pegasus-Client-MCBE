#pragma once
#include <cmath>
#include <cstdint>
namespace utility::integration::navigation_native {
inline constexpr std::uint32_t direction_mask=0x1E000;
struct Steering {float sideways{},forward{};std::uint32_t buttons{};bool valid{};};
inline bool owned_axes(float sideways,float forward,const Steering& submitted) noexcept {
    if(!submitted.valid||!std::isfinite(sideways)||!std::isfinite(forward))return false;
    const auto matches=[](float a,float b){return std::abs(a-b)<0.00001f;};
    if(matches(sideways,submitted.sideways)&&matches(forward,submitted.forward))return true;
    // Bedrock rebuilds analog axes from the direction buttons between ticks.
    float x=((submitted.buttons&0x8000)?1.0f:0.0f)-((submitted.buttons&0x10000)?1.0f:0.0f);
    float z=((submitted.buttons&0x2000)?1.0f:0.0f)-((submitted.buttons&0x4000)?1.0f:0.0f);
    const float length=std::hypot(x,z);
    if(length>0){x/=length;z/=length;}
    return matches(sideways,x)&&matches(forward,z);
}
struct Look {float pitch{},yaw{};bool valid{};};
inline float angle_delta(float value) noexcept {
    if(!std::isfinite(value))return 0;
    return std::remainder(value,360.0f);
}
inline Look look_at(double dx,double dy,double dz,float pitch,float yaw) noexcept {
    Look result;
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz)||!std::isfinite(pitch)||!std::isfinite(yaw)||std::hypot(dx,dz)<0.0001)return result;
    const float desired_yaw=static_cast<float>(std::atan2(-dx,dz)*57.29577951308232);
    const float desired_pitch=static_cast<float>(-std::atan2(dy,std::hypot(dx,dz))*57.29577951308232);
    const auto limited=[](float v){return v>15?15.0f:(v< -15?-15.0f:v);};
    result.pitch=pitch+limited(desired_pitch-pitch);
    result.yaw=angle_delta(yaw+limited(angle_delta(desired_yaw-yaw)));
    result.valid=true;return result;
}
inline Steering steer(double dx,double dz,float yaw) noexcept {
    Steering result;
    const double length=std::hypot(dx,dz);
    if(!std::isfinite(length)||!std::isfinite(yaw)||length<0.01)return result;
    const double angle=yaw*0.017453292519943295;
    result.forward=static_cast<float>((-std::sin(angle)*dx+std::cos(angle)*dz)/length);
    result.sideways=static_cast<float>((std::cos(angle)*dx+std::sin(angle)*dz)/length);
    // Choose the nearest of eight keyboard directions. Tiny tracking corrections
    // must not become a full-strength strafe when Bedrock rebuilds the axes.
    const float threshold=0.41421356237f;
    const bool longitudinal=std::abs(result.forward)>=std::abs(result.sideways)*threshold;
    const bool lateral=std::abs(result.sideways)>std::abs(result.forward)*threshold;
    if(longitudinal&&result.forward>0)result.buttons|=0x2000;
    if(longitudinal&&result.forward<0)result.buttons|=0x4000;
    if(lateral&&result.sideways>0)result.buttons|=0x8000;
    if(lateral&&result.sideways<0)result.buttons|=0x10000;
    result.sideways=lateral?(result.sideways>0?1.0f:-1.0f):0.0f;
    result.forward=longitudinal?(result.forward>0?1.0f:-1.0f):0.0f;
    if(lateral&&longitudinal){result.sideways*=0.70710678118f;result.forward*=0.70710678118f;}
    result.valid=true;return result;
}
}
