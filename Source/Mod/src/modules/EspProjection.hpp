#pragma once
#include <cmath>

namespace utility::modules::esp_projection {
// Native side planes are stored consecutively as four (normal.xyz, distance)
// records. Minecraft alternates them between camera and world space in-place.
struct Frustum { float planes[16]{}; };

inline bool scales(const Frustum& frustum,float& scale_x,float& scale_y) noexcept {
    float normals[4][3]{};
    for(int plane=0;plane<4;++plane) {
        float length_squared{};
        for(int axis=0;axis<3;++axis) {
            const float v=frustum.planes[plane*4+axis];
            if(!std::isfinite(v))return false;
            normals[plane][axis]=v;length_squared+=v*v;
        }
        if(!std::isfinite(length_squared)||length_squared<1e-8F)return false;
        const float length=std::sqrt(length_squared);
        for(float& value:normals[plane])value/=length;
    }
    float sums[2][3]{},differences[2][3]{},sum_squared[2]{},difference_squared[2]{};
    for(int pair=0;pair<2;++pair)for(int axis=0;axis<3;++axis) {
        sums[pair][axis]=normals[pair*2][axis]+normals[pair*2+1][axis];
        differences[pair][axis]=normals[pair*2][axis]-normals[pair*2+1][axis];
        sum_squared[pair]+=sums[pair][axis]*sums[pair][axis];
        difference_squared[pair]+=differences[pair][axis]*differences[pair][axis];
    }
    if(sum_squared[0]<1e-8F||sum_squared[1]<1e-8F||
       difference_squared[0]<1e-8F||difference_squared[1]<1e-8F)return false;
    float forward_dot{},axes_dot{};
    for(int axis=0;axis<3;++axis) {
        forward_dot+=sums[0][axis]*sums[1][axis];
        axes_dot+=differences[0][axis]*differences[1][axis];
    }
    // Reject a torn copy spanning the native in-place transform. Both pairs
    // must describe the same forward direction and perpendicular screen axes.
    if(forward_dot/std::sqrt(sum_squared[0]*sum_squared[1])<0.999F ||
       std::abs(axes_dot/std::sqrt(difference_squared[0]*difference_squared[1]))>0.005F)return false;
    // For opposing normals, |L-R|/|L+R| = cot(horizontal half-FOV).
    // Lengths/dot products are invariant under the camera's rotation.
    const float x=std::sqrt(difference_squared[0]/sum_squared[0]);
    const float y=std::sqrt(difference_squared[1]/sum_squared[1]);
    if(!std::isfinite(x)||!std::isfinite(y)||x<0.1F||x>10||y<0.1F||y>10)return false;
    scale_x=x;scale_y=y;return true;
}
}
