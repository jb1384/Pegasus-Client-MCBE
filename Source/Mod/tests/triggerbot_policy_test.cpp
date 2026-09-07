#include "../src/modules/GameplayModules.cpp"
#include <cstdlib>
#include <limits>
#include <set>
using namespace utility::modules;
void check(bool ok,const char* message) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
namespace {
alignas(16) Byte fake_player[0xE00]{}, fake_actor[0x300]{}, fake_level[0x200]{}, fake_mode[0xD0]{}, fake_hit[0x88]{};
int attacks{}, resolutions{};
bool living=true;
bool allow_input(){return true;}
bool deny_input(){return false;}
void* __fastcall resolve(void* hit){check(hit==fake_hit,"Resolver receives current HitResult");++resolutions;return fake_actor;}
bool __fastcall alive(void* actor){check(actor==fake_actor,"Health queried for target");return living;}
bool __fastcall attack(void* mode,void* actor,const Vec3* position){
    check(mode==fake_mode&&actor==fake_actor,"Native attack receives selected actor and local GameMode");
    check(position->x==1&&position->y==2&&position->z==3,"Native hit position preserved");++attacks;return true;
}
template<class T> void field(Byte* memory,std::size_t offset,T value){std::memcpy(memory+offset,&value,sizeof(value));}
void commit(std::uintptr_t rva){check(VirtualAlloc(image+(rva&~std::uintptr_t{4095}),8192,MEM_COMMIT,PAGE_EXECUTE_READWRITE)!=nullptr,"Commit synthetic native page");}
void code(std::uintptr_t rva,std::initializer_list<Byte> bytes){commit(rva);std::memcpy(image+rva,bytes.begin(),bytes.size());}
void jump(std::uintptr_t rva,void* target){const Byte op[]{0xFF,0x25,0,0,0,0};std::memcpy(image+rva,op,6);std::memcpy(image+rva+6,&target,8);}
void native_test(){
    image=static_cast<Byte*>(VirtualAlloc(nullptr,0x12888000,MEM_RESERVE,PAGE_NOACCESS));check(image!=nullptr,"Reserve synthetic image");
    code(0xD72560,{0x48,0x8B,0x81,0xE8,0x01,0,0,0xC3});
    code(0x47E8B00,{0x48,0x83,0xEC,0x48,0x48,0x8D,0x51,0x38,0x48,0x8D,0x4C,0x24,0x28});
    code(0x26EFD60,{0x48,0x83,0xEC,0x28,0x80,0xB9,0x69,0x02,0,0,0});
    code(0x2D2D020,{0x4D,0x89,0xC1,0x41,0xB0,0x01,0xE9});
    code(0x2D33510,{0x4D,0x89,0xC1,0x80,0xB9,0xC8,0,0,0,0x01});
    commit(0xE81A090);commit(0xE79E800);
    field(image,0xE81A090+0x78,image+0x2D2D020);field(image,0xE81A130+0x78,image+0x2D33510);
    check(verify_triggerbot(),"Exact native signatures accepted");image[0xD72560]^=1;
    check(!verify_triggerbot(),"Changed native signature disables integration");image[0xD72560]^=1;
    jump(0x47E8B00,reinterpret_cast<void*>(&resolve));jump(0x26EFD60,reinterpret_cast<void*>(&alive));
    jump(0x2D2D020,reinterpret_cast<void*>(&attack));jump(0x2D33510,reinterpret_cast<void*>(&attack));
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    field(fake_player,0,image+0xE820EC0);field(fake_player,0x1D8,fake_level);field(fake_player,0xAA0,fake_mode);
    field(fake_player,0x1C8,fake_level);field(fake_actor,0x1C8,fake_level);
    field(fake_player,0x10,fake_level);field(fake_actor,0x10,fake_level);field(fake_actor,0x18,7U);
    field(fake_level,0,image+0xE79E800);field(image,0xE79E800+0xA78,image+0xD72560);field(fake_level,0x1E8,fake_hit);
    field(fake_mode,0,image+0xE81A090);field(fake_mode,8,fake_player);field(fake_hit,0x18,1);field(fake_hit,0x2C,Vec3{1,2,3});
    std::memcpy(fake_actor+0x240,"minecraft:pig",13);field(fake_actor,0x250,std::size_t{13});field(fake_actor,0x258,std::size_t{15});
    trigger_ready=true;trigger_targets=triggerbot::all_targets;flags[static_cast<unsigned>(GameplayFeature::triggerbot)]=true;
    utility::integration::game_context_detail::picker_ready=true;++trigger_revision;
    const auto started=esp_time();
    for(int i=0;i<50;++i){utility::integration::observe_crosshair(fake_player,fake_hit);triggerbot_tick(fake_player,true,allow_input);Sleep(10);}
    const auto elapsed=esp_time()-started;
    std::printf("Native calls: attacks=%d resolutions=%d elapsed=%.3fs\n",attacks,resolutions,elapsed);
    check(attacks>=2&&attacks<=static_cast<int>(std::ceil(elapsed*15)),"Native adapter invokes bounded repeated attacks without desktop input");
    const auto before=attacks;
    trigger_targets=0;triggerbot_tick(fake_player,true,allow_input);check(attacks==before,"Both filters off prevents attack");
    trigger_targets=3;living=false;triggerbot_tick(fake_player,true,allow_input);check(attacks==before,"Dead target prevents attack");living=true;
    const auto before_resolve=resolutions;
    field(fake_hit,0x18,0);triggerbot_tick(fake_player,true,allow_input);check(resolutions==before_resolve,"Block hit is never resolved/attacked");field(fake_hit,0x18,1);
    utility::integration::game_context_detail::crosshair_time=GetTickCount64()-101;
    triggerbot_tick(fake_player,true,allow_input);check(resolutions==before_resolve,"Stale picker result rejected");
    utility::integration::observe_crosshair(fake_player,fake_hit);
    triggerbot_tick(fake_player,false,allow_input);check(attacks==before,"Menu/focus/dead-player control gate");
    for(int i=0;i<30;++i){utility::integration::observe_crosshair(fake_player,fake_hit);triggerbot_tick(fake_player,true,deny_input);Sleep(10);}
    check(attacks==before,"Focus loss immediately before attack cancels it");
    flags[static_cast<unsigned>(GameplayFeature::triggerbot)]=false;
    triggerbot_tick(fake_player,true,allow_input);check(attacks==before,"Disable stops attacks");
    utility::integration::clear_game_context();utility::integration::game_context_detail::picker_ready=false;
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
}
}
int main() {
    using namespace triggerbot;
    check(classify("minecraft:pig",true)==Target::mob,"living mob");
    check(classify("custom:living_mob",true)==Target::mob,"custom living mob");
    check(classify("minecraft:player.0.persona-test",true)==Target::player,"Bedrock player suffix");
    check(classify("minecraft:player",true)==Target::player,"plain player");
    for(auto name:{"minecraft:item","minecraft:arrow","minecraft:boat","minecraft:player","minecraft:pig"})
        check(classify(name,false)==Target::none,"No attacks on dead or nonliving actors");
    check(classify("minecraft:armor_stand",true)==Target::none,"Exclude armor stands");
    for(unsigned mask=0;mask<4;++mask){
        check(selected(Target::mob,mask)==((mask&1)!=0),"Mobs independent");
        check(selected(Target::player,mask)==((mask&2)!=0),"Players independent");
        check(!selected(Target::none,mask),"Invalid target never eligible");
    }
    GameplayModule module(GameplayFeature::triggerbot);
    utility::Module& menu=module;
    check(menu.name()=="triggerbot"&&menu.category()==utility::ModuleCategory::combat,"Registered combat name/category");
    check(menu.boolean_setting_count()==2&&menu.boolean_setting(0)&&menu.boolean_setting(1),"Both settings default on");
    check(menu.boolean_setting_name(0)=="Mobs"&&menu.boolean_setting_name(1)=="Players","Menu labels");
    menu.set_boolean_setting(0,false);check(!menu.boolean_setting(0)&&menu.boolean_setting(1),"Mobs toggle");
    menu.set_boolean_setting(1,false);check(!menu.boolean_setting(1),"Players toggle");
    menu.set_boolean_setting(2,true);check(!menu.boolean_setting(2)&&menu.boolean_setting_name(2).empty(),"Invalid settings ignored");
    check(!menu.available()&&!menu.enabled(),"Unavailable and off outside supported host");
    menu.set_enabled(true);check(!menu.enabled(),"Cannot enable without native integration");
    const TargetKey first{1,2,3},other{4,2,5};
    for(unsigned seed=1;seed<=32;++seed){
        Cadence cadence(seed);std::set<int> intervals;int clicks=0;double last=0;
        for(int tick=0;tick<1200;++tick){
            const double now=1.0+tick*0.05;
            if(cadence.update(now,first,true)){
                check(now>=1.130,"Initial reaction delay");
                if(last){check(now-last>=0.049,"No rapid double attack");intervals.insert(static_cast<int>(std::round((now-last)*1000)));}
                last=now;++clicks;
                check(!cadence.update(now,first,true),"Never multiple attacks per update");
            }
        }
        check(clicks>=480&&clicks<=840,"Sustained fast cadence stays between 8 and 14 CPS at 20Hz");
        check(intervals.size()>=3&&*intervals.rbegin()>=150,"Cadence varies with occasional pauses");
        check(!cadence.update(61.1,other,true),"Target switching requires reaction");
        check(!cadence.update(61.15,other,true),"No immediate switch attack");
        check(!cadence.update(61.2,other,false),"Losing eligibility stops immediately");
        check(!cadence.update(61.25,other,true),"Reacquisition starts fresh");
        check(!cadence.update(65.0,other,true),"No catch-up burst after a stall");
        check(!cadence.update(64.0,other,true),"Clock rollback resets");
        check(!cadence.update(std::numeric_limits<double>::quiet_NaN(),other,true),"Reject invalid time");
        check(!cadence.update(70.0,first,true),"Fresh after invalid clock");
        check(!cadence.update(70.01,{1,2,4},true),"Reused actor address with new generation resets");
        check(!cadence.update(70.02,{1,9,4},true),"Dimension change resets");
    }
    native_test();
    std::puts("Triggerbot settings, target filters and 32 minutes of simulated attack timing passed.");
}
