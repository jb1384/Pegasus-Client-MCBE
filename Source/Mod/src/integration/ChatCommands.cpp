#include "ChatCommands.hpp"
#include "GameContext.hpp"
#include "../framework/ModuleManager.hpp"
#include "../framework/Logger.hpp"
#include "../framework/ChatStyle.hpp"
#include <TlHelp32.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace utility::integration {
namespace {
using Byte = unsigned char;
using Submit = void(__fastcall*)(void*);
// Minecraft uses the release MSVC string ABI even when this DLL is Debug.
struct NativeString {
    union { char text[16]; char* pointer; } data{};
    std::size_t size{}, capacity{15};
};
static_assert(sizeof(NativeString)==32);
struct OptionalString { NativeString value; bool present{}; Byte padding[7]{}; };
using Display = void(__fastcall*)(void*,const NativeString*,const OptionalString*,bool);
Submit original_submit{};
Display display_message{};
ModuleManager* manager{};
bool chat_passthrough{};
std::mutex callback_mutex;
Byte* image{};

template<class T> T read(const void* object, std::size_t offset=0) {
    T value{};
    if (object && readable_game_memory(static_cast<const Byte*>(object)+offset,sizeof(T)))
        std::memcpy(&value,static_cast<const Byte*>(object)+offset,sizeof(T));
    return value;
}
void reply(void* controller, const std::string& text) {
    auto* model=read<void*>(controller,0xD48);
    auto* control=read<void*>(model,0x40);
    if (!read<bool>(control)) return;
    auto* client=read<void*>(model,0x50);
    auto* chat=read<void*>(client,0x648);
    if (!chat || !display_message) return;
    const std::string prefixed=chat_style::line(text);
    NativeString message; message.size=prefixed.size();
    if (message.size<16) std::memcpy(message.data.text,prefixed.data(),message.size);
    else { message.data.pointer=const_cast<char*>(prefixed.c_str()); message.capacity=message.size; }
    const OptionalString source;
    display_message(chat,&message,&source,false);
}
void __fastcall submit_hook(void* controller) {
    std::unique_lock lock(callback_mutex);
    if (chat_passthrough) { lock.unlock(); original_submit(controller); return; }
    // Same game/UI thread as native submission. Inspect the actual edited string,
    // so clipboard, history, keyboard layout and IME do not bypass interception.
    auto* field=static_cast<Byte*>(controller)+0xD60;
    const auto native=read<NativeString>(field);
    char* data=native.capacity>=16 ? native.data.pointer : reinterpret_cast<char*>(field);
    if (!native.size || !readable_game_memory(data,1) || data[0]!=',') {
        original_submit(controller); return;
    }
    // Clear in place, preserving the game's allocation/capacity and allocator.
    // Do this before parsing, and even when shut down, so failures never send commands.
    const auto length=native.size;
    std::string command;
    try {
        if (length<=32768 && length<=native.capacity && readable_game_memory(data,length)) command.assign(data,length);
    } catch (...) {}
    const std::size_t zero{};
    std::memcpy(field+16,&zero,sizeof(zero)); data[0]='\0';
    try {
        if (command.empty()) reply(controller,std::string(chat_style::red)+"Command is too long or could not be read.");
        else if (!manager) reply(controller,std::string(chat_style::red)+"Commands are unavailable.");
        else {
            std::vector<std::string> replies;
            manager->commands().execute(command,*manager,replies);
            for (const auto& line : replies) reply(controller,line);
        }
    } catch (...) { Logger::instance().info("Local command failed; chat submission was suppressed."); }
    // The native empty-input path updates/clears the chat UI without sending text.
    original_submit(controller);
}
class ChatSuspendedThreads final {
public:
    ChatSuspendedThreads() noexcept {
        const DWORD process_id = GetCurrentProcessId();
        const DWORD current_thread_id = GetCurrentThreadId();
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return;
        }
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry) != FALSE) {
            do {
                if (entry.th32OwnerProcessID == process_id && entry.th32ThreadID != current_thread_id) {
                    HANDLE thread = OpenThread(
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                        FALSE, entry.th32ThreadID);
                    if (thread == nullptr) { CloseHandle(snapshot); return; }
                    threads_.push_back(thread);
                }
            } while (Thread32Next(snapshot, &entry) != FALSE);
        }
        CloseHandle(snapshot);
        for (HANDLE thread : threads_) {
            if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                resume();
                return;
            }
            ++suspended_count_;
        }
        valid_ = true;
    }

    ~ChatSuspendedThreads() noexcept {
        resume();
        for (HANDLE thread : threads_) {
            CloseHandle(thread);
        }
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] bool outside(const void* address, std::size_t length) const noexcept {
        const auto begin = reinterpret_cast<std::uintptr_t>(address);
        const auto end = begin + length;
        for (std::size_t index = 0; index < suspended_count_; ++index) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(threads_[index], &context) == FALSE ||
                (context.Rip >= begin && context.Rip < end)) {
                return false;
            }
        }
        return true;
    }

private:
    void resume() noexcept {
        while (suspended_count_ != 0) {
            ResumeThread(threads_[--suspended_count_]);
        }
    }

    std::vector<HANDLE> threads_{};
    std::size_t suspended_count_{};
    bool valid_{};
};
}
bool install_chat_commands(ModuleManager& modules) noexcept {
    std::lock_guard lock(callback_mutex);
    if (original_submit) { manager=&modules; chat_passthrough=false; return true; }
    image=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr));
    const auto dos=read<IMAGE_DOS_HEADER>(image);
    if (dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>4096) return false;
    const auto nt=read<IMAGE_NT_HEADERS64>(image,dos.e_lfanew);
    if (nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.TimeDateStamp!=0x6A8378BA || nt.OptionalHeader.SizeOfImage!=0x12888000) return false;
    auto* target=image+0x4B41070;
    constexpr Byte signature[]{0x55,0x41,0x57,0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xEC,0xD8,0,0,0};
    // Confirm the input field and downstream normal-chat send call as well as prologue.
    constexpr Byte field[]{0x48,0x8D,0xB9,0x60,0x0D,0,0};
    constexpr Byte send[]{0xE8,0xA5,0x71,0xF0,0xFC};
    if (std::memcmp(target,signature,sizeof(signature)) || std::memcmp(image+0x4B4109D,field,sizeof(field)) ||
        std::memcmp(image+0x4B41386,send,sizeof(send))) return false;
    auto* trampoline=static_cast<Byte*>(VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!trampoline) return false;
    const auto jump=[](Byte* at,void* to){const Byte op[]{0xFF,0x25,0,0,0,0};std::memcpy(at,op,6);std::memcpy(at+6,&to,8);};
    std::memcpy(trampoline,target,sizeof(signature));jump(trampoline+sizeof(signature),target+sizeof(signature));
    DWORD protection{};
    if (!VirtualProtect(trampoline,64,PAGE_EXECUTE_READ,&protection)) { VirtualFree(trampoline,0,MEM_RELEASE);return false; }
    FlushInstructionCache(GetCurrentProcess(),trampoline,29);
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&submit_hook),&pinned)) {VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    bool installed=false;
    {
        ChatSuspendedThreads guard;
        if (guard.valid() && guard.outside(target,sizeof(signature)) &&
            VirtualProtect(target,sizeof(signature),PAGE_EXECUTE_READWRITE,&protection)) {
            {
                manager=&modules;
                display_message=reinterpret_cast<Display>(image+0x16DA850);
                original_submit=reinterpret_cast<Submit>(trampoline);
                jump(target,reinterpret_cast<void*>(&submit_hook));target[14]=0x90;
                FlushInstructionCache(GetCurrentProcess(),target,sizeof(signature));installed=true;
            }
            DWORD ignored{};VirtualProtect(target,sizeof(signature),protection,&ignored);
        }
    }
    if (!installed) VirtualFree(trampoline,0,MEM_RELEASE);
    return installed;
}
void stop_chat_commands(bool pass_through) noexcept {
    std::lock_guard lock(callback_mutex); manager=nullptr; chat_passthrough=pass_through;
}
}
