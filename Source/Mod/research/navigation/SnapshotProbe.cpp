#include "integration/NativeNavigationAdapter.hpp"
#include "integration/NavigationWorldAccess.hpp"
#include "integration/NavigationNativeLayout.hpp"
namespace native=utility::integration::navigation_native;
struct Report {
    std::uint32_t version=1,status{},ticks{},cells{},grounded{},inventory_materials{};
    double average_ms{},maximum_ms{},health{},hunger{};
};
extern "C" __declspec(dllexport) Report BUF_NavigationSnapshotReport;
Report BUF_NavigationSnapshotReport;
namespace {
using Tick=void(__fastcall*)(void*);Tick original{};
utility::integration::NativeNavigationAdapter adapter;
double total{};
void __fastcall tick(void* player) {
    auto& report=BUF_NavigationSnapshotReport;
    if(report.ticks<100&&report.status!=4) {
        try {
            if(!report.ticks)adapter.initialize();
            LARGE_INTEGER start{},end{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
            const auto frame=adapter.observe(player);QueryPerformanceCounter(&end);
            const auto milliseconds=1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
            total+=milliseconds;++report.ticks;report.average_ms=total/report.ticks;
            if(milliseconds>report.maximum_ms)report.maximum_ms=milliseconds;
            report.cells=frame.world?static_cast<unsigned>(frame.world->cells.size()):0;
            report.grounded=frame.grounded?1:0;report.health=frame.inventory.health;report.hunger=frame.inventory.hunger;
            report.inventory_materials=frame.inventory.placement_count;report.status=report.ticks==100?3:2;
        }catch(...){report.status=4;}
    }
    original(player);
}
DWORD WINAPI install(void*) {
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos=native::world_read<IMAGE_DOS_HEADER>(image);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000)return 0;
    const auto nt=native::world_read<IMAGE_NT_HEADERS64>(image+dos.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.TimeDateStamp!=native::timestamp||nt.OptionalHeader.SizeOfImage!=native::image_size)return 0;
    const auto target=native::world_read<std::uintptr_t>(image+native::player_vtable+0xC0);
    HMODULE owner{};wchar_t path[MAX_PATH]{};
    if(!GetModuleHandleExW(6,reinterpret_cast<LPCWSTR>(target),&owner)||!GetModuleFileNameW(owner,path,MAX_PATH))return 0;
    const auto filename=wcsrchr(path,L'\\');
    bool recognized=filename&&_wcsicmp(filename+1,L"BedrockNavigationWorldProbe.dll")==0;
#ifdef BUF_SNAPSHOT_RELEASE_PROBE
    recognized=recognized||(filename&&_wcsicmp(filename+1,L"BedrockNavigationSnapshotProbe.dll")==0);
#endif
    if(!recognized)return 0;
    HMODULE self{};if(!GetModuleHandleExW(5,reinterpret_cast<LPCWSTR>(&tick),&self))return 0;
    original=reinterpret_cast<Tick>(target);auto slot=reinterpret_cast<void**>(image+native::player_vtable+0xC0);
    DWORD protection{},ignored{};if(!VirtualProtect(slot,8,PAGE_READWRITE,&protection))return 0;
    const auto previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&tick),reinterpret_cast<void*>(target));
    VirtualProtect(slot,8,protection,&ignored);BUF_NavigationSnapshotReport.status=previous==reinterpret_cast<void*>(target)?1:4;return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);const auto thread=CreateThread(nullptr,0,install,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;
}
