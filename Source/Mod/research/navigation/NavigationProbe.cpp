#include <Windows.h>
#include <cstdint>
#include <cstring>
#include "integration/NavigationNativeLayout.hpp"

namespace layout = utility::integration::navigation_native;
struct Sample {
    std::uint64_t sequence, time, player, dimension, input;
    std::uint32_t entity, valid;
    float bounds[6], rotation[2];
    unsigned char before[100], after[100];
    unsigned char raw_before[24],raw_after[24];
    float health,hunger;
    std::uint64_t raw_input,attributes;
};
static_assert(sizeof(Sample)==352);
struct Capture {
    std::uint32_t version=2, sample_size=sizeof(Sample), capacity=8192, status=0;
    volatile LONG64 published=0;
    Sample samples[8192]{};
};
extern "C" __declspec(dllexport) Capture BUF_NavigationCapture;
Capture BUF_NavigationCapture;
#ifdef BUF_NAVIGATION_ACTION_PROBE
// 1 requests one bounded forward-input trial. No job or repeated execution.
extern "C" __declspec(dllexport) volatile LONG BUF_NavigationTrial;
volatile LONG BUF_NavigationTrial=0;
#endif
#ifdef BUF_NAVIGATION_MINING_PROBE
extern "C" __declspec(dllexport) volatile LONG BUF_NavigationMiningTrial;
volatile LONG BUF_NavigationMiningTrial=0;
#endif
namespace {
using Tick = void(__fastcall*)(void*);
Tick original{};
std::uintptr_t base{};
volatile LONG sampling{};
bool copy(std::uintptr_t at, void* out, std::size_t size) noexcept {
    if(!at)return false;
    __try { std::memcpy(out,reinterpret_cast<void*>(at),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> T get(std::uintptr_t at) {T value{};copy(at,&value,sizeof(value));return value;}
#ifdef BUF_NAVIGATION_ACTION_PROBE
unsigned trial_ticks{};
std::uint64_t trial_player{},trial_dimension{};
float trial_start[3]{};
bool writable_copy(std::uintptr_t at,const void* data,std::size_t size) {
    __try {std::memcpy(reinterpret_cast<void*>(at),data,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool controls() {
    DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    CURSORINFO cursor{sizeof(cursor)};
    if(pid!=GetCurrentProcessId()||!GetCursorInfo(&cursor)||(cursor.flags&CURSOR_SHOWING))return false;
    for(int key : std::array<int,11>{'W','S','A','D',VK_SPACE,VK_SHIFT,VK_CONTROL,VK_ESCAPE,VK_LBUTTON,VK_RBUTTON})
        if(GetAsyncKeyState(key)&0x8000)return false;
    return true;
}
void forward_input(const Sample& sample,bool active) {
    // Trial uses the exact forward-only pattern observed in both components.
    // Release touches only the forward bit/axis previously submitted by us.
    for(auto at : {sample.input,sample.input+16,sample.raw_input}) {
        auto flags=get<std::uint32_t>(at);
        flags=active?(flags|0x2000u):(flags&~0x2000u);
        writable_copy(at,&flags,4);
    }
    const float forward=active?1.0f:0.0f;
    writable_copy(sample.input+0x28,&forward,4);
    writable_copy(sample.raw_input+20,&forward,4);
}
void trial(Sample& sample) {
    static bool f8_down{};
    const bool pressed=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if(pressed&&!f8_down&&!trial_ticks)InterlockedExchange(&BUF_NavigationTrial,1);
    f8_down=pressed;
    auto request=InterlockedCompareExchange(&BUF_NavigationTrial,0,0);
    if(!trial_ticks&&request!=1)return;
    const bool valid=sample.valid==215&&controls()&&sample.health>8&&sample.hunger>6;
    if(!trial_ticks) {
        if(!valid) {InterlockedExchange(&BUF_NavigationTrial,-1);return;}
        // Reject preexisting movement/button input rather than replacing it.
        if(get<std::uint32_t>(sample.raw_input)!=0||get<float>(sample.raw_input+16)!=0||get<float>(sample.raw_input+20)!=0) {
            InterlockedExchange(&BUF_NavigationTrial,-2);return;
        }
        trial_player=sample.player;trial_dimension=sample.dimension;
        std::memcpy(trial_start,sample.bounds,sizeof(trial_start));
        trial_ticks=1;InterlockedExchange(&BUF_NavigationTrial,2);
    }
    const bool same=sample.player==trial_player&&sample.dimension==trial_dimension;
    const float dx=sample.bounds[0]-trial_start[0],dz=sample.bounds[2]-trial_start[2];
    if(!valid||!same||trial_ticks>10||sample.bounds[1]<trial_start[1]-0.25f||dx*dx+dz*dz>9) {
        if(same&&sample.input&&sample.raw_input)forward_input(sample,false);
        trial_ticks=0;InterlockedExchange(&BUF_NavigationTrial,valid&&same?3:-3);return;
    }
    forward_input(sample,true);++trial_ticks;
}
#endif
#ifdef BUF_NAVIGATION_MINING_PROBE
struct BlockPosition {int x,y,z;};
unsigned mining_ticks{};
std::uintptr_t mining_player{},mining_dimension{};
BlockPosition mining_position{};
unsigned char mining_face{};
float mining_hit[3]{};
bool mining_controls() {
    DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    CURSORINFO cursor{sizeof(cursor)};
    if(pid!=GetCurrentProcessId()||!GetCursorInfo(&cursor)||(cursor.flags&CURSOR_SHOWING))return false;
    for(int key:std::array<int,10>{'W','S','A','D',VK_SPACE,VK_SHIFT,VK_LBUTTON,VK_RBUTTON,VK_ESCAPE,'E'})
        if(GetAsyncKeyState(key)&0x8000)return false;
    return true;
}
bool supported_mode(std::uintptr_t mode) {
    const auto table=get<std::uintptr_t>(mode);
    return table==base+0xE81A090||table==base+0xE81A130;
}
std::uintptr_t block_at(std::uintptr_t region,const BlockPosition& pos) {
    if(get<std::uintptr_t>(get<std::uintptr_t>(region)+0x10)!=base+0x2486DC0)return 0;
    return reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,const BlockPosition*)>(base+0x2486DC0)(region,&pos);
}
bool stone(std::uintptr_t block) {
    const auto type=get<std::uintptr_t>(block+0x68);const auto string=type+0xE8;
    const auto size=get<std::size_t>(string+16),capacity=get<std::size_t>(string+24);
    if(!type||size!=15||capacity<size)return false;
    char id[15]{};
    return copy(capacity<16?string:get<std::uintptr_t>(string),id,15)&&!std::memcmp(id,"minecraft:stone",15);
}
void mine_trial_native(Sample& sample) {
    static bool f9_down{};
    const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    const bool requested=down&&!f9_down;f9_down=down;
    if(!requested&&!mining_ticks)return;
    const auto mode=get<std::uintptr_t>(sample.player+0xAA0);
    const bool same=sample.player==mining_player&&sample.dimension==mining_dimension;
    const bool valid=sample.valid==215&&mining_controls()&&sample.health>8&&sample.hunger>6&&
        supported_mode(mode)&&get<std::uintptr_t>(mode+8)==sample.player;
    using Stop=void(__fastcall*)(std::uintptr_t,const BlockPosition*);
    if(!valid||mining_ticks>100) {
        if(mining_ticks&&same&&supported_mode(mode))
            reinterpret_cast<Stop>(base+0x2D2EF30)(mode,&mining_position);
        mining_ticks=0;InterlockedExchange(&BUF_NavigationMiningTrial,-1);return;
    }
    const auto region=get<std::uintptr_t>(sample.dimension+0xF0);
    if(!mining_ticks) {
        const auto hit=get<std::uintptr_t>(get<std::uintptr_t>(sample.player+0x1D8)+0x1E8);
        if(get<int>(hit+0x18)!=0) {InterlockedExchange(&BUF_NavigationMiningTrial,-2);return;}
        mining_position=get<BlockPosition>(hit+0x20);mining_face=get<unsigned char>(hit+0x1C);
        if(mining_face>5||!copy(hit+0x2C,mining_hit,12)||!stone(block_at(region,mining_position))) {
            InterlockedExchange(&BUF_NavigationMiningTrial,-3);return;
        }
        double distance=0;
        const float eye[]{(sample.bounds[0]+sample.bounds[3])*0.5f,sample.bounds[1]+1.62f,(sample.bounds[2]+sample.bounds[5])*0.5f};
        for(int i=0;i<3;++i){const auto delta=mining_hit[i]-eye[i];distance+=delta*delta;}
        if(!std::isfinite(distance)||distance>20.25) {InterlockedExchange(&BUF_NavigationMiningTrial,-4);return;}
        mining_player=sample.player;mining_dimension=sample.dimension;
        bool destroyed{};
        reinterpret_cast<bool(__fastcall*)(std::uintptr_t,const BlockPosition*,unsigned char,bool*)>(base+0x2D2D640)(mode,&mining_position,mining_face,&destroyed);
        mining_ticks=1;InterlockedExchange(&BUF_NavigationMiningTrial,1);return;
    }
    if(!same) {mining_ticks=0;InterlockedExchange(&BUF_NavigationMiningTrial,-5);return;}
    const auto current_block=block_at(region,mining_position);
    if(!current_block) {mining_ticks=0;InterlockedExchange(&BUF_NavigationMiningTrial,-7);return;}
    if(!stone(current_block)) {
        reinterpret_cast<Stop>(base+0x2D2EF30)(mode,&mining_position);
        mining_ticks=0;InterlockedExchange(&BUF_NavigationMiningTrial,2);return;
    }
    bool destroyed{};
    reinterpret_cast<bool(__fastcall*)(std::uintptr_t,const BlockPosition*,unsigned char,const float*,bool*)>(base+0x2D2E310)(mode,&mining_position,mining_face,mining_hit,&destroyed);
    ++mining_ticks;
}
void mine_trial(Sample& sample) {
    __try {mine_trial_native(sample);}
    __except(EXCEPTION_EXECUTE_HANDLER) {mining_ticks=0;InterlockedExchange(&BUF_NavigationMiningTrial,-6);}
}
#endif
void snapshot(void* player, Sample& sample) {
    auto p=reinterpret_cast<std::uintptr_t>(player);
    if(get<std::uintptr_t>(p)!=base+layout::player_vtable)return;
    sample.player=p;sample.dimension=get<std::uintptr_t>(p+0x1C8);
    sample.entity=get<std::uint32_t>(p+0x18);
    sample.input=layout::component(get<std::uintptr_t>(p+0x10),sample.entity,
        layout::move_input_hash,layout::move_input_size,copy);
    if(copy(get<std::uintptr_t>(p+0x220),sample.bounds,sizeof(sample.bounds)))sample.valid|=1;
    if(copy(get<std::uintptr_t>(p+0x228),sample.rotation,sizeof(sample.rotation)))sample.valid|=2;
    if(copy(sample.input,sample.before,sizeof(sample.before)))sample.valid|=4;
    const auto registry=get<std::uintptr_t>(p+0x10);
    sample.raw_input=layout::component(registry,sample.entity,layout::raw_input_hash,layout::raw_input_size,copy);
    if(copy(sample.raw_input,sample.raw_before,sizeof(sample.raw_before)))sample.valid|=16;
    sample.attributes=layout::component(registry,sample.entity,layout::attributes_hash,layout::attributes_size,copy);
    if(layout::attribute(sample.attributes,get<std::uint32_t>(base+layout::health_definition+4),sample.health,copy))sample.valid|=64;
    if(layout::attribute(sample.attributes,get<std::uint32_t>(base+layout::hunger_definition+4),sample.hunger,copy))sample.valid|=128;
}
void __fastcall tick(void* player) {
    if(InterlockedCompareExchange(&sampling,1,0)!=0) {original(player);return;}
    Sample sample{};sample.time=GetTickCount64();
    snapshot(player,sample);
#ifdef BUF_NAVIGATION_ACTION_PROBE
    trial(sample);
#endif
#ifdef BUF_NAVIGATION_MINING_PROBE
    mine_trial(sample);
#endif
    original(player);
    if(copy(sample.input,sample.after,sizeof(sample.after)))sample.valid|=8;
    if(copy(sample.raw_input,sample.raw_after,sizeof(sample.raw_after)))sample.valid|=32;
    const auto serial=static_cast<std::uint64_t>(BUF_NavigationCapture.published)+1;
    auto& destination=BUF_NavigationCapture.samples[(serial-1)%8192];
    // A reader accepts only matching nonzero sequence values before/after copy.
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&destination.sequence),0);
    std::memcpy(reinterpret_cast<char*>(&destination)+8,reinterpret_cast<char*>(&sample)+8,sizeof(Sample)-8);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&destination.sequence),static_cast<LONG64>(serial));
    InterlockedExchange64(&BUF_NavigationCapture.published,static_cast<LONG64>(serial));
    InterlockedExchange(&sampling,0);
}
DWORD WINAPI install(void*) {
    base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos=get<IMAGE_DOS_HEADER>(base);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x100000) {BUF_NavigationCapture.status=2;return 0;}
    const auto nt=get<IMAGE_NT_HEADERS64>(base+dos.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
        nt.FileHeader.TimeDateStamp!=layout::timestamp||nt.OptionalHeader.SizeOfImage!=layout::image_size) {
        BUF_NavigationCapture.status=2;return 0;
    }
    auto slot=reinterpret_cast<void**>(base+layout::player_vtable+0xC0);
#ifdef BUF_NAVIGATION_MINING_PROBE
    struct Signature {std::uintptr_t rva;unsigned char bytes[8];};
    constexpr Signature signatures[]{
        {0x2D2D640,{0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54}},
        {0x2D2E310,{0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41}},
        {0x2D2EF30,{0x56,0x48,0x83,0xEC,0x20,0x48,0x89,0xCE}},
        {0x2486DC0,{0x56,0x57,0x48,0x83,0xEC,0x38,0x48,0x89}}};
    for(const auto& signature:signatures) {
        unsigned char actual[8]{};
        if(!copy(base+signature.rva,actual,8)||std::memcmp(actual,signature.bytes,8)) {
            BUF_NavigationCapture.status=6;return 0;
        }
    }
#endif
    constexpr unsigned char expected[]={0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xEC,0xD8,0x02,0x00,0x00};
    unsigned char actual[sizeof(expected)]{};
    if(!copy(base+layout::tick_rva,actual,sizeof(actual))||std::memcmp(actual,expected,sizeof(actual))) {
        BUF_NavigationCapture.status=6;return 0;
    }
    auto target=get<void*>(reinterpret_cast<std::uintptr_t>(slot));
    // Only chain the verified vanilla tick or this project's installed framework.
    if(reinterpret_cast<std::uintptr_t>(target)!=base+layout::tick_rva) {
        HMODULE owner{};wchar_t name[MAX_PATH]{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target),&owner)||!GetModuleFileNameW(owner,name,MAX_PATH)) {
            BUF_NavigationCapture.status=3;return 0;
        }
        const auto filename=wcsrchr(name,L'\\');
        bool recognized=filename&&_wcsicmp(filename+1,L"BedrockUtilityFramework.Xray.dll")==0;
#ifdef BUF_NAVIGATION_ACTION_PROBE
        recognized=recognized||(filename&&_wcsicmp(filename+1,L"BedrockNavigationProbe.dll")==0);
#endif
#ifdef BUF_NAVIGATION_MINING_PROBE
        recognized=recognized||(filename&&(_wcsicmp(filename+1,L"BedrockNavigationProbe.dll")==0||
            _wcsicmp(filename+1,L"BedrockNavigationActionProbe.dll")==0||
            _wcsicmp(filename+1,L"BedrockNavigationMiningProbe.dll")==0));
#endif
        if(!recognized) {BUF_NavigationCapture.status=3;return 0;}
    }
    HMODULE self{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&tick),&self)) {BUF_NavigationCapture.status=4;return 0;}
    original=reinterpret_cast<Tick>(target);
    DWORD protection{};
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&protection)) {BUF_NavigationCapture.status=4;return 0;}
    const auto previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&tick),target);
    DWORD ignored{};VirtualProtect(slot,8,protection,&ignored);
    BUF_NavigationCapture.status=previous==target?1u:5u;
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const auto thread=CreateThread(nullptr,0,install,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);else BUF_NavigationCapture.status=4;
    }
    return TRUE;
}
