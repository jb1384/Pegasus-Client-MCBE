#include "AntiKnockbackModule.hpp"
#include "../integration/NavigationBridge.hpp"

#include "../framework/Logger.hpp"
#include "../integration/GameContext.hpp"

#include <TlHelp32.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace utility::modules {
namespace {

constexpr std::uint32_t supported_timestamp = 0x6A8378BA;
constexpr std::uint32_t supported_image_size = 0x12888000;
constexpr std::uintptr_t apply_knockback_rva = 0x26EA750;
constexpr std::uintptr_t local_player_table_rva = 0xE820EC0;
constexpr std::uintptr_t server_player_table_rva = 0xE833530;
constexpr std::size_t patch_size = 15;

constexpr std::array<std::byte, patch_size> expected_prologue{
    std::byte{0x55}, std::byte{0x41}, std::byte{0x57}, std::byte{0x41},
    std::byte{0x56}, std::byte{0x56}, std::byte{0x57}, std::byte{0x53},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xEC}, std::byte{0xD8},
    std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
};

std::atomic<AntiKnockbackModule*> active_module{};
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
        const DWORD current_thread = GetCurrentThreadId();
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return;
        }
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry) != FALSE) {
            do {
                if (entry.th32OwnerProcessID != process_id || entry.th32ThreadID == current_thread) {
                    continue;
                }
                HANDLE thread = OpenThread(
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                    FALSE, entry.th32ThreadID);
                if (thread != nullptr) {
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

    ~SuspendedThreads() noexcept {
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

void write_absolute_jump(std::byte* destination, const void* target) noexcept {
    destination[0] = std::byte{0xFF};
    destination[1] = std::byte{0x25};
    std::memset(destination + 2, 0, 4);
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(destination + 6, &address, sizeof(address));
}

[[nodiscard]] bool supported_image(HMODULE image) noexcept {
    if (image == nullptr) {
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

[[nodiscard]] bool verified_player(void* actor) noexcept {
    if (!integration::readable_game_memory(actor, sizeof(void*))) {
        return false;
    }
    HMODULE image = GetModuleHandleW(nullptr);
    const auto table = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(actor));
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    return table == base + local_player_table_rva || table == base + server_player_table_rva;
}

} // namespace

AntiKnockbackModule::~AntiKnockbackModule() {
    active_.store(false, std::memory_order_release);
    uninstall_hook();
}

std::string_view AntiKnockbackModule::name() const noexcept { return "Anti-Knockback"; }
ModuleCategory AntiKnockbackModule::category() const noexcept { return ModuleCategory::combat; }
bool AntiKnockbackModule::available() const noexcept { return hook_installed_; }

void AntiKnockbackModule::on_register(EventBus&) {
    Logger::instance().info(install_hook()
        ? "Anti-Knockback hook installed for Minecraft 1.26.4501.0."
        : "Anti-Knockback unavailable: exact 1.26.4501.0 signatures were not present.");
}

void AntiKnockbackModule::on_enable() {
    active_.store(true, std::memory_order_release);
    Logger::instance().info("Anti-Knockback enabled.");
}

void AntiKnockbackModule::on_disable() {
    active_.store(false, std::memory_order_release);
    Logger::instance().info("Anti-Knockback disabled; vanilla knockback restored.");
}

void __fastcall AntiKnockbackModule::apply_knockback_hook(
    void* actor, void* source, float horizontal, float direction_x,
    float direction_z, void* rules) noexcept {
    const ActiveCall call;
    AntiKnockbackModule* module = active_module.load(std::memory_order_acquire);
    if (module == nullptr || module->original_ == nullptr) {
        return;
    }
    if (!integration::navigation_owns_controls.load() && module->active_.load(std::memory_order_acquire) && verified_player(actor)) {
        return;
    }
    module->original_(actor, source, horizontal, direction_x, direction_z, rules);
}

bool AntiKnockbackModule::install_hook() noexcept {
    if (hook_installed_) {
        return true;
    }
    HMODULE image = GetModuleHandleW(nullptr);
    if (!supported_image(image)) {
        return false;
    }
    auto* target = reinterpret_cast<std::byte*>(image) + apply_knockback_rva;
    if (std::memcmp(target, expected_prologue.data(), expected_prologue.size()) != 0) {
        return false;
    }
    auto* trampoline = static_cast<std::byte*>(VirtualAlloc(
        nullptr, patch_size + 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        return false;
    }
    std::memcpy(trampoline, target, patch_size);
    write_absolute_jump(trampoline + patch_size, target + patch_size);
    FlushInstructionCache(GetCurrentProcess(), trampoline, patch_size + 14);

    original_ = reinterpret_cast<ApplyKnockbackFunction>(trampoline);
    active_module.store(this, std::memory_order_release);
    SuspendedThreads suspended;
    if (!suspended.valid() || !suspended.outside(target, patch_size)) {
        active_module.store(nullptr, std::memory_order_release);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        original_ = nullptr;
        return false;
    }
    DWORD old_protection{};
    if (VirtualProtect(target, patch_size, PAGE_EXECUTE_READWRITE, &old_protection) == FALSE) {
        active_module.store(nullptr, std::memory_order_release);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        original_ = nullptr;
        return false;
    }
    std::memcpy(original_bytes_, target, patch_size);
    write_absolute_jump(target, reinterpret_cast<void*>(&apply_knockback_hook));
    target[14] = std::byte{0x90};
    FlushInstructionCache(GetCurrentProcess(), target, patch_size);
    DWORD ignored{};
    VirtualProtect(target, patch_size, old_protection, &ignored);

    target_ = target;
    trampoline_ = trampoline;
    hook_installed_ = true;
    return true;
}

void AntiKnockbackModule::uninstall_hook() noexcept {
    if (!hook_installed_) {
        return;
    }
    active_.store(false, std::memory_order_release);
    SuspendedThreads suspended;
    if (!suspended.valid() || !suspended.outside(target_, patch_size) ||
        !suspended.outside(trampoline_, patch_size + 14)) {
        return;
    }
    DWORD old_protection{};
    if (VirtualProtect(target_, patch_size, PAGE_EXECUTE_READWRITE, &old_protection) == FALSE) {
        return;
    }
    std::memcpy(target_, original_bytes_, patch_size);
    FlushInstructionCache(GetCurrentProcess(), target_, patch_size);
    DWORD ignored{};
    VirtualProtect(target_, patch_size, old_protection, &ignored);
    active_module.store(nullptr, std::memory_order_release);
    while (active_calls.load(std::memory_order_acquire) != 0) {
        SwitchToThread();
    }
    VirtualFree(trampoline_, 0, MEM_RELEASE);
    trampoline_ = nullptr;
    target_ = nullptr;
    original_ = nullptr;
    hook_installed_ = false;
}

} // namespace utility::modules
