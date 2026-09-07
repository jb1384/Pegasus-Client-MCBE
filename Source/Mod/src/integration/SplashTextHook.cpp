#include "SplashTextHook.hpp"

#include <TlHelp32.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace utility::integration {
namespace {

constexpr std::uint32_t supported_timestamp = 0x6A8378BA;
constexpr std::uint32_t supported_image_size = 0x12888000;
constexpr std::uintptr_t splash_loader_rva = 0x41FA580;
constexpr std::size_t patch_size = 19;
constexpr std::string_view replacement = "made by Roundomegaboi";

constexpr std::array<std::byte, patch_size> expected_prologue{
    std::byte{0x55},
    std::byte{0x41}, std::byte{0x57},
    std::byte{0x41}, std::byte{0x56},
    std::byte{0x41}, std::byte{0x55},
    std::byte{0x41}, std::byte{0x54},
    std::byte{0x56},
    std::byte{0x57},
    std::byte{0x53},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xEC},
    std::byte{0x88}, std::byte{0x05}, std::byte{0x00}, std::byte{0x00},
};

using SplashLoader = void(__fastcall*)(void*, void*, void*, void*);

std::atomic<SplashLoader> original_loader{};
std::atomic_bool replacement_enabled{true};
std::atomic_uint32_t active_calls{};

class ActiveCall final {
public:
    ActiveCall() noexcept { active_calls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveCall() noexcept { active_calls.fetch_sub(1, std::memory_order_acq_rel); }
};

class SuspendedThreads final {
public:
    SuspendedThreads() noexcept {
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
                if (entry.th32OwnerProcessID != process_id || entry.th32ThreadID == current_thread_id) {
                    continue;
                }

                HANDLE thread = OpenThread(
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                    FALSE,
                    entry.th32ThreadID);
                if (thread != nullptr) {
                    threads_.push_back(thread);
                }
            } while (Thread32Next(snapshot, &entry) != FALSE);
        }
        CloseHandle(snapshot);

        for (HANDLE thread : threads_) {
            if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                valid_ = false;
                resume();
                return;
            }
            ++suspended_count_;
        }
        valid_ = true;
    }

    ~SuspendedThreads() noexcept {
        resume();
        for (HANDLE thread : threads_) {
            CloseHandle(thread);
        }
    }

    SuspendedThreads(const SuspendedThreads&) = delete;
    SuspendedThreads& operator=(const SuspendedThreads&) = delete;

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    [[nodiscard]] bool instruction_pointers_outside(const void* address, std::size_t length) const noexcept {
        const auto begin = reinterpret_cast<std::uintptr_t>(address);
        const auto end = begin + length;
        for (std::size_t index = 0; index < suspended_count_; ++index) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(threads_[index], &context) == FALSE) {
                return false;
            }
            if (context.Rip >= begin && context.Rip < end) {
                return false;
            }
        }
        return true;
    }

private:
    void resume() noexcept {
        while (suspended_count_ != 0) {
            --suspended_count_;
            ResumeThread(threads_[suspended_count_]);
        }
    }

    std::vector<HANDLE> threads_{};
    std::size_t suspended_count_{};
    bool valid_{};
};

void write_absolute_jump(std::byte* destination, const void* target, std::size_t length) noexcept {
    destination[0] = std::byte{0xFF};
    destination[1] = std::byte{0x25};
    destination[2] = std::byte{0x00};
    destination[3] = std::byte{0x00};
    destination[4] = std::byte{0x00};
    destination[5] = std::byte{0x00};
    const auto target_value = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(destination + 6, &target_value, sizeof(target_value));
    for (std::size_t index = 14; index < length; ++index) {
        destination[index] = std::byte{0x90};
    }
}

[[nodiscard]] bool writable_address(const void* address, std::size_t length) noexcept {
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) == 0 || information.State != MEM_COMMIT) {
        return false;
    }

    const DWORD protection = information.Protect & 0xff;
    const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize;
    return writable && length <= region_end - begin;
}

void replace_splash_text(void* object) noexcept {
    if (object == nullptr) {
        return;
    }

    // Minecraft 1.26.4501.0 stores a release-mode MSVC std::string at +0x10.
    // Write into its existing allocation/SSO buffer only; never allocate or free
    // memory across the game's and this DLL's separate C++ runtimes.
    auto* string_object = static_cast<std::byte*>(object) + 0x10;
    if (!writable_address(string_object, 0x20)) {
        return;
    }

    std::size_t size{};
    std::size_t capacity{};
    std::memcpy(&size, string_object + 0x10, sizeof(size));
    std::memcpy(&capacity, string_object + 0x18, sizeof(capacity));
    if (capacity < replacement.size() || capacity > (1u << 20) || size > capacity) {
        return;
    }

    char* buffer = reinterpret_cast<char*>(string_object);
    if (capacity >= 16) {
        std::memcpy(&buffer, string_object, sizeof(buffer));
    }
    if (buffer == nullptr || !writable_address(buffer, replacement.size() + 1)) {
        return;
    }

    std::memcpy(buffer, replacement.data(), replacement.size());
    buffer[replacement.size()] = '\0';
    size = replacement.size();
    std::memcpy(string_object + 0x10, &size, sizeof(size));
}

void __fastcall splash_loader_hook(void* object, void* argument2, void* argument3, void* argument4) {
    const ActiveCall active_call;
    const SplashLoader original = original_loader.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(object, argument2, argument3, argument4);
        if (replacement_enabled.load()) replace_splash_text(object);
    }
}

[[nodiscard]] bool is_supported_image(HMODULE image) noexcept {
    if (image == nullptr) {
        return false;
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(image, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    const wchar_t* filename = path;
    for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            filename = cursor + 1;
        }
    }
    if (_wcsicmp(filename, L"Minecraft.Windows.exe") != 0) {
        return false;
    }

    const auto* base = reinterpret_cast<const std::byte*>(image);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.TimeDateStamp == supported_timestamp &&
        nt->OptionalHeader.SizeOfImage == supported_image_size;
}

} // namespace

bool SplashTextHook::install() noexcept {
    replacement_enabled=true;
    if (installed_) {
        return true;
    }

    HMODULE image = GetModuleHandleW(nullptr);
    if (!is_supported_image(image)) {
        return false;
    }

    auto* target = reinterpret_cast<std::byte*>(image) + splash_loader_rva;
    if (std::memcmp(target, expected_prologue.data(), expected_prologue.size()) != 0) {
        return false;
    }

    auto* trampoline = static_cast<std::byte*>(VirtualAlloc(
        nullptr,
        patch_size + 14,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        return false;
    }
    std::memcpy(trampoline, target, patch_size);
    write_absolute_jump(trampoline + patch_size, target + patch_size, 14);
    FlushInstructionCache(GetCurrentProcess(), trampoline, patch_size + 14);
    original_loader.store(reinterpret_cast<SplashLoader>(trampoline), std::memory_order_release);

    SuspendedThreads suspended;
    if (!suspended.valid() || !suspended.instruction_pointers_outside(target, patch_size)) {
        original_loader.store(nullptr, std::memory_order_release);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    DWORD old_protection{};
    if (VirtualProtect(target, patch_size, PAGE_EXECUTE_READWRITE, &old_protection) == FALSE) {
        original_loader.store(nullptr, std::memory_order_release);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    std::memcpy(original_, target, patch_size);
    write_absolute_jump(target, reinterpret_cast<const void*>(&splash_loader_hook), patch_size);
    FlushInstructionCache(GetCurrentProcess(), target, patch_size);
    DWORD ignored{};
    VirtualProtect(target, patch_size, old_protection, &ignored);

    target_ = target;
    trampoline_ = trampoline;
    installed_ = true;
    return true;
}

void SplashTextHook::uninstall() noexcept {
    replacement_enabled=false;
    if (!installed_ || target_ == nullptr) {
        return;
    }

    {
        SuspendedThreads suspended;
        if (!suspended.valid() ||
            !suspended.instruction_pointers_outside(target_, patch_size) ||
            !suspended.instruction_pointers_outside(trampoline_, patch_size + 14) ||
            !suspended.instruction_pointers_outside(
                reinterpret_cast<const void*>(&splash_loader_hook), 0x200)) {
            return;
        }

        DWORD old_protection{};
        if (VirtualProtect(target_, patch_size, PAGE_EXECUTE_READWRITE, &old_protection) == FALSE) {
            return;
        }
        std::memcpy(target_, original_, patch_size);
        FlushInstructionCache(GetCurrentProcess(), target_, patch_size);
        DWORD ignored{};
        VirtualProtect(target_, patch_size, old_protection, &ignored);
    }

    installed_ = false;
    target_ = nullptr;
    while (active_calls.load(std::memory_order_acquire) != 0) {
        SwitchToThread();
    }
    original_loader.store(nullptr, std::memory_order_release);
    VirtualFree(trampoline_, 0, MEM_RELEASE);
    trampoline_ = nullptr;
}

bool SplashTextHook::installed() const noexcept {
    return installed_;
}

} // namespace utility::integration
