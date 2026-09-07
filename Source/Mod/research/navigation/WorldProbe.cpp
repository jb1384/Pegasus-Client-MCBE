#include "integration/NavigationWorldAccess.hpp"
#include "integration/NavigationNativeLayout.hpp"
namespace native=utility::integration::navigation_native;
struct Capture {
    std::uint64_t sequence{};
    std::uint32_t version=1,status{};
    native::NativePosition position{};
    native::NativeCell cell{};
    std::uint32_t selected{},inventory_valid{};
    unsigned char stack[0x98]{};
};
static_assert(sizeof(Capture)==712);
extern "C" __declspec(dllexport) Capture BUF_NavigationWorldCapture;
Capture BUF_NavigationWorldCapture;
namespace {
std::uintptr_t image{};
using Tick=void(__fastcall*)(void*);
Tick original{};
void capture(std::uintptr_t player) {
    static LONG64 sequence{};
    auto& output=BUF_NavigationWorldCapture;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&output.sequence),0);
    const auto level=native::world_read<std::uintptr_t>(player+0x1D8);
    const auto hit=native::world_read<std::uintptr_t>(level+0x1E8);
    output.position=native::world_read<native::NativePosition>(hit+0x20);
    const auto dimension=native::world_read<std::uintptr_t>(player+0x1C8);
    const auto region=native::world_read<std::uintptr_t>(dimension+0xF0);
    output.status=native::world_read<int>(hit+0x18)==0&&native::capture_cell(image,region,output.position,output.cell)?2:3;
    output.inventory_valid=0;
    __try {
        const auto supplies=native::world_read<std::uintptr_t>(player+0x5B8);
        output.selected=native::world_read<unsigned>(supplies+0x10);
        const auto inventory=native::world_read<std::uintptr_t>(supplies+0xB8);
        const auto get=native::world_read<std::uintptr_t>(native::world_read<std::uintptr_t>(inventory)+0x38);
        if(output.selected<9&&get>=image&&get<image+native::image_size) {
            const auto stack=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,int)>(get)(inventory,output.selected);
            output.inventory_valid=native::world_copy(stack,output.stack,sizeof(output.stack))?1:0;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&output.sequence),++sequence);
}
void __fastcall tick(void* player) {
    static bool pressed{};
    const bool down=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    if(down&&!pressed&&native::world_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(player))==image+native::player_vtable)
        capture(reinterpret_cast<std::uintptr_t>(player));
    pressed=down;original(player);
}
DWORD WINAPI install(void*) {
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos=native::world_read<IMAGE_DOS_HEADER>(image);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000)return 0;
    const auto nt=native::world_read<IMAGE_NT_HEADERS64>(image+dos.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.TimeDateStamp!=native::timestamp||nt.OptionalHeader.SizeOfImage!=native::image_size)return 0;
    const auto target=native::world_read<std::uintptr_t>(image+native::player_vtable+0xC0);
    HMODULE owner{};wchar_t path[MAX_PATH]{};
    if(!GetModuleHandleExW(6,reinterpret_cast<LPCWSTR>(target),&owner)||!GetModuleFileNameW(owner,path,MAX_PATH))return 0;
    const auto filename=wcsrchr(path,L'\\');
    if(target!=image+native::tick_rva&&(!filename||_wcsicmp(filename+1,L"BedrockNavigationMiningProbeV2.dll")))return 0;
    HMODULE self{};if(!GetModuleHandleExW(5,reinterpret_cast<LPCWSTR>(&tick),&self))return 0;
    original=reinterpret_cast<Tick>(target);auto slot=reinterpret_cast<void**>(image+native::player_vtable+0xC0);
    DWORD protection{},ignored{};if(!VirtualProtect(slot,8,PAGE_READWRITE,&protection))return 0;
    const auto previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&tick),reinterpret_cast<void*>(target));
    VirtualProtect(slot,8,protection,&ignored);
    BUF_NavigationWorldCapture.status=previous==reinterpret_cast<void*>(target)?1:4;return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);const auto thread=CreateThread(nullptr,0,install,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;
}
