#include "integration/NativeNavigationAdapter.hpp"
#include "integration/NavigationWorldAccess.hpp"
#include "integration/NavigationNativeLayout.hpp"
#include <cstdio>
#include <share.h>
namespace native=utility::integration::navigation_native;
namespace nav=utility::navigation;
struct Report {
    std::uint32_t version=1,status{},ticks{},route_size{};
    double feet[3]{},goal[3]{};
    char activity[160]{};
};
static_assert(sizeof(Report)==224);
extern "C" __declspec(dllexport) Report BUF_NavigationRouteReport;
Report BUF_NavigationRouteReport;
namespace {
using Tick=void(__fastcall*)(void*);Tick original{};
utility::integration::NativeNavigationAdapter adapter;
nav::Controller controller;
bool initialized{},running{},warming{};ULONGLONG started{},warm_started{};
#ifdef BUF_ROUTE_V5
FILE* recording{};
void record(const nav::Frame& frame,bool request) {
    if(!recording) {
        wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
        if(!GetTempPathW(MAX_PATH,folder))return;
        swprintf_s(path,L"%sBedrockNavigationRoute-%lu.tsv",folder,GetCurrentProcessId());
        recording=_wfsopen(path,L"a",_SH_DENYNO);
        if(recording)std::fprintf(recording,"time\trequest\tstatus\tticks\tmanual\tcontrol\tphysical\towned\taim\tflags\tside\tforward\tpitch\tyaw\texpected_pitch\texpected_yaw\tx\ty\tz\tactivity\n");
    }
    if(!recording)return;
    const auto& i=adapter.input_observation();const auto& r=BUF_NavigationRouteReport;
    std::fprintf(recording,"%llu\t%d\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%08x\t%.8f\t%.8f\t%.8f\t%.8f\t%.8f\t%.8f\t%.6f\t%.6f\t%.6f\t%s\n",
        GetTickCount64(),request,r.status,r.ticks,frame.manual_input,frame.control,i.physical,i.owned,i.aim_owned,
        i.flags,i.sideways,i.forward,i.pitch,i.yaw,i.expected_pitch,i.expected_yaw,
        frame.feet.x,frame.feet.y,frame.feet.z,r.activity);
    std::fflush(recording);
}
#endif
void __fastcall tick(void* player) {
    auto& report=BUF_NavigationRouteReport;
    try {
        if(!initialized){adapter.initialize();initialized=true;report.status=1;}
        static bool pressed{};
#if defined(BUF_ROUTE_V6)
        const bool down=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
#elif defined(BUF_ROUTE_V5) || defined(BUF_ROUTE_V4)
        const bool down=(GetAsyncKeyState(VK_HOME)&0x8000)!=0;
#elif defined(BUF_ROUTE_V3)
        const bool down=(GetAsyncKeyState(VK_F4)&0x8000)!=0;
#elif defined(BUF_ROUTE_V2)
        const bool down=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
#else
        const bool down=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
#endif
        const bool request=down&&!pressed;pressed=down;
        if(request||running||warming) {
            const auto frame=adapter.observe(player);
            nav::Settings settings;settings.allow_break=false;settings.allow_place=false;
            settings.allow_sprint=false;settings.allow_parkour=false;settings.auto_eat=false;
            if(request&&(!frame.control||frame.manual_input)) {
                report.status=21;
                std::snprintf(report.activity,sizeof(report.activity),"Start rejected: %s",
                    !frame.control?"gameplay not focused, interface open, or player not alive":"movement, interaction, or view input active");
            }
            if(request&&frame.control&&!frame.manual_input) {
                controller.stop();adapter.release();running=false;warming=true;warm_started=GetTickCount64();
                report.status=22;report.ticks=0;std::snprintf(report.activity,sizeof(report.activity),"Observing nearby terrain");
            }
            if(warming&&(!frame.control||frame.manual_input)) {
                warming=false;report.status=21;std::snprintf(report.activity,sizeof(report.activity),"Terrain observation interrupted by manual input or interface");
            }
            if(warming&&GetTickCount64()-warm_started>=1500) {
                warming=false;
                const auto rotation=native::world_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(player)+0x228);
                const auto yaw=native::world_read<float>(rotation+4)*0.017453292519943295;
                const double dx=-std::sin(yaw),dz=std::cos(yaw);
                auto target=nav::block_at(frame.feet);
                if(std::abs(dx)>std::abs(dz))target.x+=dx>0?8:-8;else target.z+=dz>0?8:-8;
                const auto command=nav::parse_command({"goto",std::to_string(target.x),std::to_string(target.z)});
                if(command.command) {
                    controller.command(*command.command,frame,settings,[](const std::string&){return false;});
                    running=true;started=GetTickCount64();report.ticks=0;
                    report.goal[0]=target.x;report.goal[1]=target.y;report.goal[2]=target.z;
                }
            }
            if(running) {
                controller.tick(frame,settings,adapter,GetTickCount64()/1000.0);++report.ticks;
                const auto& status=controller.status();
                report.status=10+static_cast<unsigned>(status.state);report.route_size=static_cast<unsigned>(status.route.size());
                std::snprintf(report.activity,sizeof(report.activity),"%s",status.message.c_str());
                report.feet[0]=frame.feet.x;report.feet[1]=frame.feet.y;report.feet[2]=frame.feet.z;
                if(!controller.owns_controls()||GetTickCount64()-started>30000) {
                    if(controller.owns_controls()){controller.stop("Diagnostic time limit reached");std::snprintf(report.activity,sizeof(report.activity),"Diagnostic time limit reached");report.status=20;}
                    adapter.release();running=false;
                }
            }
#ifdef BUF_ROUTE_V5
            record(frame,request);
#endif
        }
    }catch(...){adapter.release();running=false;report.status=4;}
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
#ifdef BUF_ROUTE_V6
    if(!filename||_wcsicmp(filename+1,L"BedrockNavigationRouteLogProbe.dll"))return 0;
#elif defined(BUF_ROUTE_V5)
    constexpr unsigned char signature[]{0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xEC,0xD8,0x02,0x00,0x00};
    unsigned char actual[sizeof(signature)]{};
    if(target!=image+0x2F2C3E0||!native::world_copy(target,actual,sizeof(actual))||std::memcmp(actual,signature,sizeof(actual))) {
        BUF_NavigationRouteReport.status=4;return 0;
    }
#elif defined(BUF_ROUTE_V4)
    if(!filename||_wcsicmp(filename+1,L"BedrockNavigationRouteProbeV3.dll"))return 0;
#elif defined(BUF_ROUTE_V3)
    if(!filename||_wcsicmp(filename+1,L"BedrockNavigationRouteProbeV2.dll"))return 0;
#elif defined(BUF_ROUTE_V2)
    if(!filename||_wcsicmp(filename+1,L"BedrockNavigationRouteProbe.dll"))return 0;
#else
    if(!filename||_wcsicmp(filename+1,L"BedrockNavigationSnapshotProbeRelease.dll"))return 0;
#endif
    HMODULE self{};if(!GetModuleHandleExW(5,reinterpret_cast<LPCWSTR>(&tick),&self))return 0;
    original=reinterpret_cast<Tick>(target);auto slot=reinterpret_cast<void**>(image+native::player_vtable+0xC0);
    DWORD protection{},ignored{};if(!VirtualProtect(slot,8,PAGE_READWRITE,&protection))return 0;
    const auto previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&tick),reinterpret_cast<void*>(target));
    VirtualProtect(slot,8,protection,&ignored);BUF_NavigationRouteReport.status=previous==reinterpret_cast<void*>(target)?1:4;return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);const auto thread=CreateThread(nullptr,0,install,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;
}
