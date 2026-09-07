#include "XrayModule.hpp"
#include "../integration/TerrainLighting.hpp"
#include "../framework/Logger.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>
#include <algorithm>

namespace utility::modules {
namespace {
constexpr uintptr_t tick_rva = 0x996CCB0;
constexpr uintptr_t rebuild_rva = 0x996C110;
constexpr uintptr_t graphics_vector_rva = 0x11A73A28;
constexpr uintptr_t graphics_vtable_rva = 0xE7EA2E0;
constexpr uintptr_t coordinator_vtable_rva = 0xEA2ED10;
constexpr std::array<unsigned char,19> tick_bytes{
    0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xEC,0x78,0x01,0x00,0x00};
// Route cubes through flat lighting and supply maximum mesh block light.
// These are renderer reads only; world light propagation remains unchanged.
struct LightPatch { uintptr_t rva; size_t size; std::array<unsigned char,7> before; std::array<unsigned char,7> after; };
constexpr std::array<LightPatch,4> light_patches{{
    {0x237AEA4,7,{0x83,0xBB,0xEC,0,0,0,2},{0x83,0xBB,0xEC,0,0,0,127}},
    {0x237B19B,7,{0x83,0xBB,0xEC,0,0,0,2},{0x83,0xBB,0xEC,0,0,0,127}},
    {0x237EE3A,7,{0x0F,0xB6,0xAF,0xA4,0,0,0},{0xBD,15,0,0,0,0x90,0x90}},
    {0x237EF12,4,{0x45,0x0F,0xB6,0xFF},{0x41,0xB7,15,0x90}}
}};
// Final RGBA light-lookup pixels: scalar, vector (including scalar tails),
// and End scalar builders. Direct calls bypass their virtual tables.
constexpr std::array<LightPatch,6> texture_patches{{
    {0x5ADA079,7,{0x41,0x81,0xC0,0,0,0,0xFF},{0x41,0xB8,0xFF,0xFF,0xFF,0xFF,0x90}},
    {0x5AD8980,6,{0x81,0xC2,0,0,0,0xFF},{0xBA,0xFF,0xFF,0xFF,0xFF,0x90}},
    {0x5AD89D0,6,{0x81,0xC1,0,0,0,0xFF},{0xB9,0xFF,0xFF,0xFF,0xFF,0x90}},
    {0x5AD9D53,4,{0x66,0x0F,0xFE,0xD9},{0x66,0x0F,0x76,0xDB}},
    {0x5AD9D9D,4,{0x66,0x0F,0xFE,0xC8},{0x66,0x0F,0x76,0xC9}},
    {0x69F67C0,7,{0x41,0x81,0xC2,0,0,0,0xFF},{0x41,0xBA,0xFF,0xFF,0xFF,0xFF,0x90}}
}};
struct Graphics { uintptr_t object; uintptr_t block; uintptr_t type; int shape; float ao; bool retained; };
using Tick = void(__fastcall*)(void*);
struct State {
    std::atomic<bool> installed{false}, desired{false}, applied{false};
    std::atomic<bool> fullbright{false};
    std::atomic<unsigned> visibility{XrayModule::default_visibility};
    unsigned applied_visibility{XrayModule::default_visibility};
    std::atomic<int> fullbright_level{15};
    int applied_level{15};
    bool applied_fullbright{};
    bool applied_xray{};
    uintptr_t base{};
    Tick original{};
    std::recursive_mutex mutex;
    std::vector<Graphics> saved;
    uintptr_t registry_begin{};
    uintptr_t registry_end{};
    uintptr_t stone_graphics{};
    unsigned refresh_passes{};
};
// Native callbacks and their storage live until Minecraft exits; the DLL is pinned.
State& state() { static State* s = new State; return *s; }
bool readable(uintptr_t p, size_t size) noexcept {
    if (!p || !size || p + size < p) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof(m)) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return p >= reinterpret_cast<uintptr_t>(m.BaseAddress) &&
        p + size <= reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
}
template<class T> T get(uintptr_t p) noexcept { T v{}; if(readable(p,sizeof(T))) std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(T)); return v; }
bool name_of(uintptr_t type, char (&out)[160], size_t& length) noexcept {
    const uintptr_t text=type+0xE8;
    if(!readable(text,32)) return false;
    length=get<size_t>(text+16);
    const auto capacity=get<size_t>(text+24);
    if(length<11 || length>=sizeof(out) || capacity<length || capacity>4096) return false;
    const auto data=capacity<16 ? text : get<uintptr_t>(text);
    if(!readable(data,length)) return false;
    std::memcpy(out,reinterpret_cast<void*>(data),length); out[length]=0;
    return std::string_view(out,length).starts_with("minecraft:");
}
class PausedThreads {
    std::vector<HANDLE> handles;
    size_t suspended{};
    bool ok{true};
public:
    PausedThreads() {
        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snap==INVALID_HANDLE_VALUE) {ok=false;return;}
        THREADENTRY32 e{}; e.dwSize=sizeof(e);
        if(Thread32First(snap,&e)) do {
            if(e.th32OwnerProcessID==GetCurrentProcessId() && e.th32ThreadID!=GetCurrentThreadId()) {
                HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
                if(h) handles.push_back(h); else ok=false;
            }
        } while(Thread32Next(snap,&e));
        CloseHandle(snap);
        if(!ok) return;
        for(HANDLE h:handles) {
            if(SuspendThread(h)==DWORD(-1)) {ok=false;break;} ++suspended;
        }
    }
    ~PausedThreads() {while(suspended) ResumeThread(handles[--suspended]); for(HANDLE h:handles) CloseHandle(h);}
    bool outside(uintptr_t begin,size_t n) const noexcept {
        if(!ok) return false;
        for(size_t i=0;i<suspended;++i) {CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(handles[i],&c) || (c.Rip>=begin && c.Rip<begin+n)) return false;
        } return true;
    }
};
bool write_code(uintptr_t p,const void* bytes,size_t n) noexcept {
    DWORD previous{};
    if(!VirtualProtect(reinterpret_cast<void*>(p),n,PAGE_EXECUTE_READWRITE,&previous)) return false;
    std::memcpy(reinterpret_cast<void*>(p),bytes,n);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p),n);
    DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(p),n,previous,&ignored);return true;
}
void jump(unsigned char* p,uintptr_t target) { p[0]=0xFF;p[1]=0x25;std::memset(p+2,0,4);std::memcpy(p+6,&target,8); }
std::vector<LightPatch> lighting_patches(bool fullbright,int level) {
    std::vector<LightPatch> patches(light_patches.begin(),light_patches.end());
    // At lower levels retain the native light lookup; the white lookup used
    // by maximum Fullbright would otherwise erase the slider's effect.
    const auto mesh_level=static_cast<unsigned char>(fullbright?std::clamp(level,9,15):15);
    patches[2].after[1]=mesh_level;
    patches[3].after[2]=mesh_level;
    if(fullbright&&mesh_level==15)
        patches.insert(patches.end(),texture_patches.begin(),texture_patches.end());
    return patches;
}
bool lights(bool on, bool fullbright=false,int level=15) {
    auto& s=state();
    const auto patches=lighting_patches(on?fullbright:s.applied_fullbright,on?level:s.applied_level);
    for(const auto& p:patches) {
        const auto& expected=on?p.before:p.after;
        if(std::memcmp(reinterpret_cast<void*>(s.base+p.rva),expected.data(),p.size)) return false;
    }
    PausedThreads paused;
    for(const auto& p:patches) if(!paused.outside(s.base+p.rva,p.size)) return false;
    size_t written=0;
    for(const auto& p:patches) {
        if(!write_code(s.base+p.rva,(on?p.after:p.before).data(),p.size)) {
            for(size_t i=0;i<written;++i) write_code(s.base+patches[i].rva,(on?patches[i].before:patches[i].after).data(),patches[i].size);
            return false;
        } ++written;
    } return true;
}
bool snapshot(std::vector<Graphics>& out,uintptr_t& begin,uintptr_t& end,unsigned visibility) {
    auto& s=state();begin=get<uintptr_t>(s.base+graphics_vector_rva);end=get<uintptr_t>(s.base+graphics_vector_rva+8);
    if(end<=begin || (end-begin)%8 || (end-begin)/8>20000 || !readable(begin,end-begin)) return false;
    unsigned ores=0; bool air=false,stone=false;
    for(auto pos=begin;pos<end;pos+=8) {
        const auto object=get<uintptr_t>(pos);
        if(get<uintptr_t>(object)!=s.base+graphics_vtable_rva || !readable(object,0x80)) return false;
        const auto block=get<uintptr_t>(object+8), type=get<uintptr_t>(block+0x68);
        char name[160]{};size_t n{};
        if(!name_of(type,name,n)) continue;
        const std::string_view id(name,n);
        const bool ore=XrayModule::valuable(id);ores+=ore;
        const int shape=get<int>(object+16);
        if(shape < -1 || shape>512) return false;
        air|=id=="minecraft:air" && shape==-1;
        stone|=id=="minecraft:stone" && shape==0;
        out.push_back({object,block,type,shape,get<float>(object+20),XrayModule::retained(id,visibility)});
    }
    return air && stone && ores>=21;
}
void exchange_field(uintptr_t p,int value) noexcept { InterlockedExchange(reinterpret_cast<volatile LONG*>(p),value); }
void restore_graphics() {
    auto& s=state();
    // Match current entries; a world/resource reload may already have destroyed old objects.
    const auto begin=get<uintptr_t>(s.base+graphics_vector_rva),end=get<uintptr_t>(s.base+graphics_vector_rva+8);
    if(end<begin || end-begin>160000 || !readable(begin,end-begin)) {s.saved.clear();return;}
    for(auto p=begin;p<end;p+=8) {
        const auto object=get<uintptr_t>(p);
        for(const auto& g:s.saved) if(g.object==object && get<uintptr_t>(object)==s.base+graphics_vtable_rva &&
            get<uintptr_t>(object+8)==g.block && get<uintptr_t>(g.block+0x68)==g.type) {
            // Resource reload can reset fields on the same allocations. Restore only
            // fields still carrying our override; never replace freshly loaded values.
            if(s.applied_xray && !g.retained && get<int>(object+16)==-1) exchange_field(object+16,g.shape);
            if(((s.applied_xray && g.retained) || s.applied_fullbright) && get<int>(object+20)==0) {
                int bits{};std::memcpy(&bits,&g.ao,4);exchange_field(object+20,bits);
            }
            break;
        }
    }
    s.saved.clear();
}
void __fastcall tick_hook(void* coordinator) {
    auto& s=state();
    {
        std::lock_guard guard(s.mutex);
        if(get<uintptr_t>(reinterpret_cast<uintptr_t>(coordinator))==s.base+coordinator_vtable_rva) {
            const bool xray=s.desired.load();
            const bool fullbright=s.fullbright.load();
            const int level=s.fullbright_level.load();
            const bool requested=xray || fullbright;
            const unsigned visibility=s.visibility.load();
            const bool changed_registry=s.applied.load() &&
                (get<uintptr_t>(s.base+graphics_vector_rva)!=s.registry_begin || get<uintptr_t>(s.base+graphics_vector_rva+8)!=s.registry_end ||
                 get<uintptr_t>(s.stone_graphics)!=s.base+graphics_vtable_rva ||
                 get<int>(s.stone_graphics+16)!=(s.applied_xray ? -1 : 0) ||
                 (s.applied_fullbright && get<int>(s.stone_graphics+20)!=0));
            if(s.applied.load() && (!requested || changed_registry || xray!=s.applied_xray || fullbright!=s.applied_fullbright ||
                (xray && visibility!=s.applied_visibility) || (fullbright && level!=s.applied_level))) {
                if(lights(false)) {
                    restore_graphics();s.applied=false;s.refresh_passes=2;
                    Logger::instance().info("x-ray: restored terrain graphics and normal mesh lighting.");
                }
            }
            if(requested && !s.applied.load()) {
                std::vector<Graphics> entries;uintptr_t begin{},end{};
                if(snapshot(entries,begin,end,visibility) && lights(true,fullbright,level)) {
                    s.applied_xray=xray;s.applied_fullbright=fullbright;s.applied_visibility=visibility;s.applied_level=level;
                    s.saved=std::move(entries);s.registry_begin=begin;s.registry_end=end;
                    for(const auto& g:s.saved) {
                        char name[160]{};size_t length{};
                        if(name_of(g.type,name,length) && std::string_view(name,length)=="minecraft:stone")
                            s.stone_graphics=g.object;
                        if(xray && !g.retained) exchange_field(g.object+16,-1);
                        if(fullbright || (xray && g.retained)) exchange_field(g.object+20,0);
                    }
                    s.applied=true;s.refresh_passes=2;
                    Logger::instance().info("Terrain lighting: requested x-ray/fullbright graphics applied; rebuilding loaded chunks.");
                }
            }
            if(s.refresh_passes) {
                reinterpret_cast<void(__fastcall*)(void*,bool,bool)>(s.base+rebuild_rva)(coordinator,false,false);
                --s.refresh_passes;
            }
        }
    }
    s.original(coordinator);
}
bool install() {
    auto& s=state();std::lock_guard guard(s.mutex);if(s.installed) return true;
    const auto image=GetModuleHandleW(L"Minecraft.Windows.exe");if(!image)return false;
    s.base=reinterpret_cast<uintptr_t>(image);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(s.base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->FileHeader.TimeDateStamp!=0x6A8378BA || nt->OptionalHeader.SizeOfImage!=0x12888000) return false;
    if(std::memcmp(reinterpret_cast<void*>(s.base+tick_rva),tick_bytes.data(),tick_bytes.size()) ||
       get<uintptr_t>(s.base+coordinator_vtable_rva)!=s.base+0x99711A0 ||
       std::memcmp(reinterpret_cast<void*>(s.base+rebuild_rva),"\x55\x41\x57\x41\x56\x41\x55\x41\x54\x56\x57\x53\x48\x81\xEC\x88\x00\x00\x00",19)) return false;
    for(const auto& p:texture_patches) if(std::memcmp(reinterpret_cast<void*>(s.base+p.rva),p.before.data(),p.size)) return false;
    for(const auto& p:light_patches) if(std::memcmp(reinterpret_cast<void*>(s.base+p.rva),p.before.data(),p.size)) return false;
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!trampoline) return false;
    std::memcpy(trampoline,tick_bytes.data(),tick_bytes.size());jump(trampoline+19,s.base+tick_rva+19);
    DWORD ignored{};if(!VirtualProtect(trampoline,64,PAGE_EXECUTE_READ,&ignored)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,64);
    std::array<unsigned char,19> patch{};patch.fill(0x90);jump(patch.data(),reinterpret_cast<uintptr_t>(&tick_hook));
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&tick_hook),&pinned)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    s.original=reinterpret_cast<Tick>(trampoline);
    bool success=false;
    {PausedThreads paused;if(paused.outside(s.base+tick_rva,19)) success=write_code(s.base+tick_rva,patch.data(),patch.size());}
    if(!success) {s.original=nullptr;VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    s.installed=true;
    Logger::instance().info("x-ray: Minecraft 1.26.4501.0 coordinator hook installed.");return true;
}
} // namespace
bool XrayModule::available() const noexcept { return state().installed.load(); }
void XrayModule::on_register(EventBus&) { if(!install()) Logger::instance().info("x-ray: unsupported host or native signatures; module unavailable."); }
void XrayModule::set_boolean_setting(std::size_t index, bool value) noexcept {
    if(index>=setting_count) return;
    const unsigned bit=1U<<index;
    if(value) visibility_.fetch_or(bit); else visibility_.fetch_and(~bit);
    if(enabled()) state().visibility=visibility_.load();
}
void XrayModule::on_enable() { state().visibility=visibility_.load();state().desired=true; }
void XrayModule::on_disable() { state().desired=false; }
XrayModule::~XrayModule() {state().desired=false;}
} // namespace utility::modules

namespace utility::integration::terrain_lighting {
bool initialize() { return utility::modules::install(); }
bool available() noexcept { return utility::modules::state().installed.load(); }
void set_fullbright_level(int level) noexcept { utility::modules::state().fullbright_level=std::clamp(level,9,15); }
void set_fullbright(bool enabled) noexcept { utility::modules::state().fullbright=enabled; }
}
