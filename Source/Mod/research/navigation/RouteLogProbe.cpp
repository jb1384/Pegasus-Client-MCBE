#include <Windows.h>
#include <cstdio>
#include "integration/NavigationWorldAccess.hpp"
#include "integration/NavigationNativeLayout.hpp"
namespace n=utility::integration::navigation_native;
namespace {
using Tick=void(__fastcall*)(void*);
Tick previous{};std::uintptr_t probe{};
void __fastcall tick(void* player) {
    previous(player);
    // Repair only the verified V5 diagnostic's exclusive log handle, after its
    // game-thread callback has finished. Never close any game-owned stream.
    auto* report=reinterpret_cast<unsigned*>(GetProcAddress(reinterpret_cast<HMODULE>(probe),"BUF_NavigationRouteReport"));
    if(!report||report[0]!=1||report[1]<16)return;
    auto** stream=reinterpret_cast<FILE**>(probe+0x2DDE0);
    if(*stream){std::fclose(*stream);*stream=nullptr;}
}
DWORD WINAPI install(void*) {
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    probe=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"BedrockNavigationRouteProbeV5.dll"));
    if(!probe)return 0;
    const auto target=n::world_read<std::uintptr_t>(image+n::player_vtable+0xC0);
    // Addresses and operand verified from this exact V5 DLL before this repair.
    const unsigned char instruction[]{0x48,0x8B,0x0D,0x89,0xAC,0x02,0x00};
    unsigned char actual[7]{};
    if(target!=probe+0x26C0||!n::world_copy(probe+0x3150,actual,7)||std::memcmp(instruction,actual,7))return 0;
    HMODULE self{};if(!GetModuleHandleExW(5,reinterpret_cast<LPCWSTR>(&tick),&self))return 0;
    previous=reinterpret_cast<Tick>(target);
    auto** slot=reinterpret_cast<void**>(image+n::player_vtable+0xC0);DWORD protection{},ignored{};
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&protection))return 0;
    InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&tick),reinterpret_cast<void*>(target));
    VirtualProtect(slot,8,protection,&ignored);return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);const auto t=CreateThread(nullptr,0,install,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;
}
