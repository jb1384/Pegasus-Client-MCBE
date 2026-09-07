#include "../src/modules/GameplayModules.cpp"
#include <cstdlib>
#include <limits>
using namespace utility::modules;
void check(bool condition,const char* text) {
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}
}
bool close_enough(float a,float b){return std::abs(a-b)<0.001F;}
int tick_calls{};
bool active_controls=true;
bool fake_controls(void*) {return active_controls;}
void __fastcall fake_player_tick(void* actor) {
    ++tick_calls;
    // Simulate native movement overwriting velocity with no jump input at all.
    auto* state=read<float*>(actor,0x218);
    state[6]=0;state[7]=0;state[8]=0;
}
int main() {
    jetpack::Controller controller;
    jetpack::Velocity output{};
    check(controller.update(0,0,20,{},output)&&output.z>0&&output.z<1,"gradual takeoff");
    for(int i=0;i<60;++i)controller.update(0,0,20,{},output);
    check(close_enough(output.z,1)&&close_enough(output.x,0)&&close_enough(output.y,0),"forward speed converges without drag drift");
    controller.update(0,90,20,{},output);
    check(output.x<0&&output.x>-1&&output.z>0,"turn blends rather than snapping");
    for(int i=0;i<60;++i)controller.update(-90,90,100,{},output);
    check(close_enough(output.y,5)&&close_enough(output.x,0)&&close_enough(output.z,0),"look up gives vertical maximum speed");
    for(int i=0;i<60;++i)controller.update(90,0,4.3F,{},output);
    check(close_enough(output.y,-0.215F),"look down at walking speed");
    for(int i=0;i<60;++i)controller.update(-45,45,100,{},output);
    check(close_enough(std::sqrt(output.x*output.x+output.y*output.y+output.z*output.z),5),"diagonal speed is normalized");
    check(!controller.update(NAN,0,20,{},output),"invalid aim rejected");
    controller.update(0,0,20,{},output);
    check(close_enough(output.z,0.28F),"invalid input resets smoothing");
    GameplayModule module(GameplayFeature::jetpack);
    check(module.name()=="Jetpack"&&module.category()==utility::ModuleCategory::movement&&!module.enabled(),"registered movement module defaults off");
    check(module.has_value()&&close_enough(module.value(),10),"speed setting defaults to ten");
    module.set_value(1000);check(close_enough(module.value(),100),"upper slider clamp");
    module.set_value(-100);check(close_enough(module.value(),4.3F),"lower slider clamp");
    module.set_value(NAN);check(close_enough(module.value(),4.3F),"invalid slider input ignored");
    ready=true;jetpack_ready=false;check(!module.available(),"requires regular player tick hooks");
    jetpack_ready=true;airjump_ready=false;
    check(module.available(),"jetpack does not require the jump callback");
    std::array<Byte,0x230> actor{};
    std::array<float,9> state{1,2,3,4,5,6,0,0,0};
    std::array<float,2> rotation{-90,0};
    void* dimension=actor.data();void* angles=rotation.data();
    void* movement=state.data();
    std::memcpy(actor.data()+0x218,&movement,8);
    std::memcpy(actor.data()+0x1C8,&dimension,8);
    std::memcpy(actor.data()+0x228,&angles,8);
    module.set_enabled(true);
    jetpack_movement(actor.data(),state.data(),true);
    check(state[7]>0&&state[7]<0.215F,"native callback writes upward velocity");
    check(state[0]==1&&state[1]==2&&state[2]==3&&state[3]==4&&state[4]==5&&state[5]==6,"positions and interpolation history untouched");
    const auto before=state;
    jetpack_movement(actor.data(),state.data(),false);
    check(state==before,"inactive controls do not thrust");
    module.set_enabled(false);
    jetpack_movement(actor.data(),state.data(),true);
    check(state==before,"disabled jetpack does not write movement");
    module.set_enabled(true);
    for(int i=0;i<20;++i)
        dispatch_jetpack_tick(actor.data(),fake_player_tick,true,fake_controls);
    check(tick_calls==20&&state[7]>0.21F,"regular ticks sustain thrust without Space or jump callbacks");
    active_controls=false;
    dispatch_jetpack_tick(actor.data(),fake_player_tick,true,fake_controls);
    check(state[7]==0,"inactive regular tick leaves native movement untouched");
    active_controls=true;
    dispatch_jetpack_tick(actor.data(),fake_player_tick,false,fake_controls);
    check(state[7]==0,"other server players receive no thrust");
    module.set_enabled(false);
    dispatch_jetpack_tick(actor.data(),fake_player_tick,true,fake_controls);
    check(state[7]==0&&tick_calls==23,"disabled regular tick still forwards native callback once");
    std::puts("Jetpack steering, settings and movement dispatch passed");
}
