#include "../src/modules/EspProjection.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
using utility::modules::esp_projection::Frustum;
using utility::modules::esp_projection::scales;
void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(int argc,char** argv){
    const Frustum camera{{-.622975826F,0,-.782241046F,0,.622975826F,0,-.782241046F,0,
        0,.838670611F,-.544639111F,0,0,-.838670611F,-.544639111F,0}};
    // Exact sample captured while the native frustum was in world space.
    const Frustum world{{-.554446280F,-.013291242F,.832113385F,0,
        -.935043097F,-.013282727F,-.354285300F,0,
        -.532094896F,.829298437F,.170702040F,0,
        -.504969239F,-.847800672F,.161988094F,0}};
    float sx{},sy{};
    const auto verify=[&](const Frustum& f){check(scales(f,sx,sy),"valid frustum accepted");
        check(std::abs(sx-.7963988F)<.00001F&&std::abs(sy-1.5398648F)<.00001F,"FOV remains correct across coordinate spaces");};
    verify(camera);verify(world);
    // Rotate all four planes through yaw/pitch, with unequal normal lengths.
    for(int yaw=-180;yaw<=180;yaw+=5)for(int pitch=-85;pitch<=85;pitch+=5){
        Frustum rotated{};const float a=yaw*.01745329252F,b=pitch*.01745329252F;
        for(int i=0;i<4;++i){const float x=camera.planes[i*4],y=camera.planes[i*4+1],z=camera.planes[i*4+2];
            const float yy=y*std::cos(b)-z*std::sin(b),zz=y*std::sin(b)+z*std::cos(b),length=1+i*.4F;
            rotated.planes[i*4]=(x*std::cos(a)+zz*std::sin(a))*length;
            rotated.planes[i*4+1]=yy*length;
            rotated.planes[i*4+2]=(-x*std::sin(a)+zz*std::cos(a))*length;
        }verify(rotated);
    }
    Frustum torn=camera;for(int i=0;i<4;++i)torn.planes[i]=world.planes[i];
    check(!scales(torn,sx,sy),"mixed coordinate spaces rejected");
    check(!scales(Frustum{},sx,sy),"zero planes rejected");
    torn=camera;torn.planes[0]=NAN;check(!scales(torn,sx,sy),"invalid normal rejected");
    int count{};
    if(argc>1){std::ifstream input(argv[1],std::ios::binary);check(input.good(),"recording opens");
        Frustum frame{};while(input.read(reinterpret_cast<char*>(&frame),sizeof(frame))){verify(frame);++count;}
        check(input.eof()&&input.gcount()==0&&count>0,"complete nonempty recording replayed");}
    std::printf("PASS: 2555 rotations, live world-space fixture, torn/invalid data; %d recorded frames replayed\n",count);
}
