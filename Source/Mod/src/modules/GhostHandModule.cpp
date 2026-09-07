#include "GhostHandModule.hpp"
#include "../framework/Logger.hpp"
#include "../integration/GameContext.hpp"

#include <array>
#include <cstring>
#include <intrin.h>
#include <TlHelp32.h>
#include <cmath>
#include <vector>
#include <string_view>

namespace utility::modules {
namespace {
constexpr std::uintptr_t branch_rva = 0x4F5E06;
constexpr unsigned char vanilla_opcode = 0x77; // ja: no block result
constexpr unsigned char ghost_opcode = 0xEB;   // jmp: ignore block distance
constexpr std::array<unsigned char, 11> branch_signature{
    0x0F,0x28,0xFE,0x83,0xF8,0xFD,0x77,0x61,0x49,0x8B,0x06};
constexpr std::array<unsigned char, 8> destination_signature{
    0x4C,0x89,0xE1,0xE8,0x8F,0x41,0x40,0x01};

std::atomic_bool players_only{}, filter_active{};
using ResolveActor = void*(__fastcall*)(void*);
ResolveActor resolve_actor{};
unsigned char* filter_image{};
constexpr std::uintptr_t resolve_call_rva = 0x4F5F38;
constexpr std::array<unsigned char, 5> resolve_call{0xE8,0xC3,0x2B,0x2F,0x04};

template<class T> T read_field(const void* object, std::size_t offset = 0) noexcept {
    T result{};
    if (object && integration::readable_game_memory(static_cast<const unsigned char*>(object)+offset,sizeof(T)))
        std::memcpy(&result,static_cast<const unsigned char*>(object)+offset,sizeof(T));
    return result;
}

bool player_actor(void* actor) noexcept {
    // Same native identifier used by ESP/triggerbot; bounded MSVC string read.
    if (!integration::readable_game_memory(actor,0x260)) return false;
    auto* name=static_cast<unsigned char*>(actor)+0x240;
    const auto size=read_field<std::size_t>(name,16), capacity=read_field<std::size_t>(name,24);
    if (size==0 || size>256 || capacity<size || capacity>65536) return false;
    const auto* data=capacity>=16?read_field<const char*>(name):reinterpret_cast<const char*>(name);
    if (!integration::readable_game_memory(data,size)) return false;
    const std::string_view id(data,size);
    return id=="minecraft:player" || id.starts_with("minecraft:player.");
}

struct Point { float x,y,z; };
float hit_distance(void* origin_hit, void* target_hit) noexcept {
    const auto origin=read_field<Point>(origin_hit);
    const auto target=read_field<Point>(target_hit,0x2C);
    const float x=target.x-origin.x,y=target.y-origin.y,z=target.z-origin.z;
    return std::sqrt(x*x+y*y+z*z);
}

void* filter_candidate(void* actor, void* candidate, void* level) noexcept {
    if (!actor || !filter_active.load() || !players_only.load() || player_actor(actor)) return actor;
    // This runs before the picker overwrites Level's block hit. Preserve that
    // result by returning no candidate when a non-player is behind the block.
    if (!level || read_field<void*>(read_field<void*>(level),0xA78)!=filter_image+0xD72560) return nullptr;
    auto* block=read_field<void*>(level,0x1E8);
    if (!integration::readable_game_memory(block,0x88) ||
        !integration::readable_game_memory(candidate,0x88)) return nullptr;
    const int type=read_field<int>(block,0x18);
    if (type==2 || type==3) return actor; // Native miss / no blocking result.
    if (type<0 || type>4) return nullptr;
    const float obstruction=hit_distance(block,block), entity=hit_distance(block,candidate);
    // Native comparison at 0x4F6111 adds 0.1 before comparing against the cap.
    return std::isfinite(obstruction) && std::isfinite(entity) && obstruction>entity+0.1F ? actor : nullptr;
}

void* __fastcall resolve_hook(void* candidate, void* level) noexcept {
    return filter_candidate(resolve_actor(candidate),candidate,level);
}

// Installation changes a complete call instruction. Stop and inspect all
// other threads before the write; failed inspection leaves the call intact.
class PausedThreads {
public:
    PausedThreads() {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if (snapshot==INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{sizeof(entry)};
        bool enumerated=Thread32First(snapshot,&entry)!=FALSE;
        bool opened=enumerated;
        if (enumerated) do {
            if (entry.th32OwnerProcessID==GetCurrentProcessId() && entry.th32ThreadID!=GetCurrentThreadId()) {
                HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,entry.th32ThreadID);
                if (!thread) opened=false;
                else threads.push_back(thread);
            }
        } while (Thread32Next(snapshot,&entry));
        CloseHandle(snapshot);
        if (!opened) return;
        for (auto thread:threads) {
            if (SuspendThread(thread)==DWORD(-1)) return;
            ++paused;
        }
        valid=true;
    }
    ~PausedThreads() {
        while (paused) ResumeThread(threads[--paused]);
        for (auto thread:threads) CloseHandle(thread);
    }
    bool outside(const void* start,std::size_t size) const {
        if (!valid) return false;
        const auto address=reinterpret_cast<std::uintptr_t>(start);
        for (auto thread:threads) {
            CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
            if (!GetThreadContext(thread,&context) || (context.Rip>=address && context.Rip<address+size)) return false;
        }
        return true;
    }
private:
    std::vector<HANDLE> threads;
    std::size_t paused{};
    bool valid{};
};

bool install_filter(unsigned char* image) noexcept {
    if (filter_image) return filter_image==image;
    constexpr unsigned char getter[]{0x48,0x8B,0x81,0xE8,0x01,0,0,0xC3};
    constexpr unsigned char resolver[]{0x48,0x83,0xEC,0x48,0x48,0x8D,0x51,0x38,0x48,0x8D,0x4C,0x24,0x28};
    if (std::memcmp(image+resolve_call_rva,resolve_call.data(),resolve_call.size()) ||
        std::memcmp(image+0xD72560,getter,sizeof(getter)) ||
        std::memcmp(image+0x47E8B00,resolver,sizeof(resolver))) return false;
    // Short call reaches a nearby relay; r14 is the picker's current Level.
    unsigned char* relay{};
    const auto origin=reinterpret_cast<std::uintptr_t>(image+resolve_call_rva)&~std::uintptr_t{0xFFFF};
    for (std::uintptr_t delta=0x10000;delta<0x70000000 && !relay;delta+=0x10000) {
        for (auto address:{origin+delta,origin>delta?origin-delta:std::uintptr_t{0}}) {
            if (!address) continue;
            relay=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(address),4096,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            if (relay) break;
        }
    }
    if (!relay) return false;
    const unsigned char code[]{0x4C,0x89,0xF2,0xFF,0x25,0,0,0,0}; // mov rdx,r14; jmp [rip]
    std::memcpy(relay,code,sizeof(code));
    auto callback=&resolve_hook;
    std::memcpy(relay+sizeof(code),&callback,sizeof(callback));
    DWORD protection{};
    if (!VirtualProtect(relay,4096,PAGE_EXECUTE_READ,&protection)) { VirtualFree(relay,0,MEM_RELEASE);return false; }
    FlushInstructionCache(GetCurrentProcess(),relay,17);
    auto redirected=resolve_call;
    const auto displacement=reinterpret_cast<std::intptr_t>(relay)-reinterpret_cast<std::intptr_t>(image+resolve_call_rva+5);
    if (displacement<INT32_MIN || displacement>INT32_MAX) { VirtualFree(relay,0,MEM_RELEASE);return false; }
    const auto relative=static_cast<std::int32_t>(displacement);
    std::memcpy(redirected.data()+1,&relative,4);
    // Retain the pass-through hook for process lifetime, like gameplay hooks.
    // Pin its code before publishing it; it never retains a Module or Actor*.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&resolve_hook),&pinned)) { VirtualFree(relay,0,MEM_RELEASE);return false; }
    bool installed=false;
    {
        PausedThreads paused;
        if (paused.outside(image+resolve_call_rva,5) &&
            !std::memcmp(image+resolve_call_rva,resolve_call.data(),5) &&
            VirtualProtect(image+resolve_call_rva,5,PAGE_EXECUTE_READWRITE,&protection)) {
            resolve_actor=reinterpret_cast<ResolveActor>(image+0x47E8B00);
            filter_image=image;
            std::memcpy(image+resolve_call_rva,redirected.data(),5);
            FlushInstructionCache(GetCurrentProcess(),image+resolve_call_rva,5);
            DWORD ignored{};VirtualProtect(image+resolve_call_rva,5,protection,&ignored);
            installed=true;
        }
    }
    if (!installed) VirtualFree(relay,0,MEM_RELEASE);
    return installed;
}

bool matches(unsigned char* image, bool patched) noexcept {
    if (!image) return false;
    auto expected = branch_signature;
    expected[6] = patched ? ghost_opcode : vanilla_opcode;
    return integration::readable_game_memory(image + branch_rva - 6, expected.size()) &&
        integration::readable_game_memory(image + 0x4F5E69, destination_signature.size()) &&
        !std::memcmp(image + branch_rva - 6, expected.data(), expected.size()) &&
        !std::memcmp(image + 0x4F5E69, destination_signature.data(), destination_signature.size());
}

bool supported_host(HMODULE host) noexcept {
    if (!host || host != GetModuleHandleW(L"Minecraft.Windows.exe")) return false;
    auto* base = reinterpret_cast<unsigned char*>(host);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->FileHeader.TimeDateStamp == 0x6A8378BA && nt->OptionalHeader.SizeOfImage == 0x12888000;
}

// The picker starts xmm7 at the full ray length (xmm6), then replaces it
// with distance to the blocking hit. Skip only that replacement. The native
// Survival/Creative caps, closest-entity search, and final reach gate remain.
// Both branch forms have the same size and destination. One atomic opcode
// exchange cannot expose a torn instruction, and requires no DLL trampoline.
bool set_ghost_patch(unsigned char* image, bool enable) noexcept {
    if (!matches(image, !enable)) return false;
    auto* opcode = reinterpret_cast<volatile char*>(image + branch_rva);
    DWORD protection{};
    if (!VirtualProtect(image + branch_rva, 1, PAGE_EXECUTE_READWRITE, &protection)) return false;
    const auto expected = static_cast<char>(enable ? vanilla_opcode : ghost_opcode);
    const auto desired = static_cast<char>(enable ? ghost_opcode : vanilla_opcode);
    const bool changed = _InterlockedCompareExchange8(opcode, desired, expected) == expected;
    FlushInstructionCache(GetCurrentProcess(), image + branch_rva, 1);
    DWORD ignored{};
    VirtualProtect(image + branch_rva, 1, protection, &ignored);
    return changed;
}
} // namespace

GhostHandModule::~GhostHandModule() { on_disable(); }
bool GhostHandModule::boolean_setting() const noexcept { return players_only.load(); }
void GhostHandModule::set_boolean_setting(bool value) noexcept { players_only.store(value); }

void GhostHandModule::on_register(EventBus&) {
    auto host = GetModuleHandleW(nullptr);
    image_ = reinterpret_cast<unsigned char*>(host);
    ready_ = supported_host(host) && matches(image_, false) && install_filter(image_);
    Logger::instance().info(ready_ ? "Ghost Hand ready; native entity picker signatures verified." :
        "Ghost Hand unavailable: unsupported host or picker signature mismatch.");
}

void GhostHandModule::on_enable() {
    if (patched_) return;
    filter_active.store(true);
    if (!ready_ || !set_ghost_patch(image_, true)) {
        filter_active.store(false);
        Logger::instance().info("Ghost Hand could not enable; picker was not changed.");
        set_enabled(false);
        return;
    }
    patched_ = true;
    Logger::instance().info("Ghost Hand enabled; entity selection ignores intervening blocks.");
}

void GhostHandModule::on_disable() {
    if (!patched_) return;
    if (set_ghost_patch(image_, false)) {
        patched_ = false;
        filter_active.store(false);
        Logger::instance().info("Ghost Hand disabled; native block occlusion restored.");
    } else {
        Logger::instance().info("Ghost Hand restoration failed; restart Minecraft to restore native picking.");
    }
}
} // namespace utility::modules
