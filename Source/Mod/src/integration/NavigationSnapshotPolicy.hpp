#pragma once
#include <array>
namespace utility::integration::navigation_native {
inline const std::array<std::array<int,3>,25*25*17>& snapshot_offsets() {
    static const auto offsets=[] {
        std::array<std::array<int,3>,25*25*17> result{};unsigned index{};
        for(int pass=0;pass<2;++pass)for(int radius=0;radius<=12;++radius)
            for(int x=-radius;x<=radius;++x)for(int z=-radius;z<=radius;++z) {
                if(x!= -radius&&x!=radius&&z!= -radius&&z!=radius)continue;
                for(int y=-8;y<=8;++y) {
                    const bool walking_layer=y>=-1&&y<=2;
                    if(walking_layer==(pass==0))result[index++]={x,y,z};
                }
            }
        return result;
    }();
    return offsets;
}
}
