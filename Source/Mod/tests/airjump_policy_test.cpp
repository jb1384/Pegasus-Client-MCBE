// Runs in an isolated process; never attaches to Minecraft or sends input.
#include "../src/modules/GameplayModules.cpp"
#include <cstdlib>
#include <cstdio>
using namespace utility::modules;
void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
std::array<void*,19> captured{};
bool accept_jump=true;
int calls{};
void __fastcall fake_jump(void* a,void* b,void* c,void* d,void* e,void* f,void* g,void* h,void* i,
    void* j,void* k,void* l,void* m,void* n,void* o,void* p,void* q,void* r,void* s){
    captured={a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s};++calls;
    if(accept_jump&&component_present(c)&&read<int>(p,16)==0)*reinterpret_cast<int*>(static_cast<Byte*>(p)+16)=10;
}
struct Presence {
    std::array<Byte,0x60> pool{};
    std::array<std::uint32_t,2048> sparse{};
    void* page=sparse.data();
    struct Proxy{void* pool;std::uint32_t entity;} proxy{pool.data(),11};
    Presence(){sparse.fill(0xFFFFFFFF);void* begin=&page;void* end=reinterpret_cast<Byte*>(&page)+8;
        std::memcpy(pool.data()+8,&begin,8);std::memcpy(pool.data()+16,&end,8);}
};
int main(){
    airjump::Requests requests;
    check(!requests.consume(1,11,100),"no jump before a physical press");
    requests.press(100);
    check(requests.consume(1,11,101)&&requests.consume(2,1,102),"both simulation replicas consume the same press");
    requests.press(130);
    check(!requests.consume(1,11,131)&&!requests.consume(2,1,131),"keyboard repeat does not jump again");
    requests.release();requests.press(200);
    check(requests.consume(1,11,201)&&requests.consume(2,1,202),"fresh press allows another jump on both replicas");
    requests.release();requests.press(300);
    check(!requests.consume(1,11,299)&&!requests.consume(1,11,801),"stale and reversed time rejected");
    requests.reset();check(!requests.consume(2,1,310),"disable or focus loss cancels queued jump");
    for(unsigned n=1;n<10;++n){requests.release();requests.press(1000+n);check(requests.consume(n,n,1000+n),"world changes do not exhaust replica slots");}

    Presence player,ground;player.sparse[11]=0;
    check(component_present(&player.proxy)&&!component_present(&ground.proxy),"native sparse membership");
    player.sparse[11]=0x40000;
    check(!component_present(&player.proxy),"wrong entity generation rejected");
    player.sparse[11]=0;
    check(!component_present(nullptr),"missing proxy rejected");
    std::uint32_t entity=11;
    std::array<Byte,20> jump_state{};
    std::array<float,9> movement{1,2,3,1,2,3,0,-0.08F,0};const auto before=movement;
    std::array<int,19> tokens{};std::array<void*,19> args{};
    for(unsigned n=0;n<19;++n)args[n]=&tokens[n];
    args[0]=&entity;args[2]=&ground.proxy;args[3]=&player.proxy;args[15]=jump_state.data();args[16]=movement.data();
    original_jump=&fake_jump;
    const auto run=[&](bool eligible,std::uint64_t now){dispatch_airjump(eligible,now,args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9],args[10],args[11],args[12],args[13],args[14],args[15],args[16],args[17],args[18]);};
    auto* delay=reinterpret_cast<int*>(jump_state.data()+16);
    airjump_requests.reset();airjump_requests.press(100);*delay=7;
    run(false,101);check(captured==args&&*delay==7,"ineligible actor forwarded untouched");
    run(true,102);auto expected=args;expected[2]=args[3];
    check(captured==expected&&*delay==10,"native jump gets only a scoped ground override and clear cooldown");
    check(movement==before,"Airjump never directly changes position or velocity");
    run(true,103);check(captured==args,"same client press cannot be consumed twice");
    int server_registry{};args[17]=&server_registry;entity=1;*delay=7;
    run(true,104);expected=args;expected[2]=args[3];
    check(captured==expected&&*delay==10,"integrated server independently receives the same native jump");
    airjump_requests.release();airjump_requests.press(200);ground.sparse[11]=0;*delay=0;
    run(true,201);check(captured==args,"ground takeoff uses native ground proxy");
    ground.sparse[11]=0xFFFFFFFF;run(true,202);check(captured==args,"holding takeoff key does not add an airborne jump");
    airjump_requests.release();airjump_requests.press(300);*delay=6;accept_jump=false;
    run(true,301);check(*delay==6,"native refusal restores cooldown");
    check(calls==7,"every dispatch forwards the native callback exactly once");
    flags[static_cast<unsigned>(GameplayFeature::airjump)]=false;
    airjump_hook(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7],args[8],args[9],args[10],args[11],args[12],args[13],args[14],args[15],args[16],args[17],args[18]);
    check(captured==args&&calls==8,"disabled native hook passes all nineteen arguments unchanged");
    std::puts("Airjump policy and native callback dispatch passed");
}
