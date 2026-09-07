#include "CriticalsModule.hpp"

#include "../framework/Logger.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace utility::modules {
namespace {

constexpr std::uint32_t supported_timestamp = 0x6A8378BA;
constexpr std::uint32_t supported_image_size = 0x12888000;
constexpr std::uintptr_t attack_begin_rva = 0x2FB8DE0;
constexpr std::uintptr_t attack_end_rva = 0x2FB9CCE;

struct Patch {
    std::uintptr_t rva;
    std::size_t size;
    std::array<unsigned char, 6> original;
    std::array<unsigned char, 6> replacement;
};

constexpr std::array<Patch, 8> patches{{
    {0x2FB8FC4, 6, {0x0F, 0x85, 0x92, 0x01, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x2FB8FD8, 6, {0x0F, 0x86, 0x7E, 0x01, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x2FB907D, 6, {0x0F, 0x82, 0xD9, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x2FB908D, 6, {0x0F, 0x85, 0xC9, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x2FB912D, 2, {0x72, 0x2D},                         {0x90, 0x90}},
    {0x2FB913F, 2, {0x75, 0x1B},                         {0x90, 0x90}},
    {0x2FB914C, 2, {0x75, 0x0E},                         {0x90, 0x90}},
    {0x2FB9156, 6, {0x0F, 0x85, 0x96, 0x03, 0x00, 0x00}, {0xE9, 0x97, 0x03, 0x00, 0x00, 0x90}},
}};

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
                if (entry.th32OwnerProcessID == process_id && entry.th32ThreadID != current_thread) {
                    HANDLE thread = OpenThread(
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                        FALSE, entry.th32ThreadID);
                    if (thread != nullptr) {
                        threads_.push_back(thread);
                    }
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
    [[nodiscard]] bool outside(std::uintptr_t begin, std::uintptr_t end) const noexcept {
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

[[nodiscard]] bool write_bytes(void* target, const unsigned char* bytes, std::size_t size) noexcept {
    DWORD old_protection{};
    if (VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protection) == FALSE) {
        return false;
    }
    std::memcpy(target, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored{};
    return VirtualProtect(target, size, old_protection, &ignored) != FALSE;
}

} // namespace

CriticalsModule::~CriticalsModule() { restore(); }
std::string_view CriticalsModule::name() const noexcept { return "Criticals"; }
ModuleCategory CriticalsModule::category() const noexcept { return ModuleCategory::combat; }
bool CriticalsModule::available() const noexcept { return signatures_valid_; }

void CriticalsModule::on_register(EventBus&) {
    signatures_valid_ = verify();
    Logger::instance().info(signatures_valid_
        ? "Criticals native path verified for Minecraft 1.26.4501.0."
        : "Criticals unavailable: exact 1.26.4501.0 signatures were not present.");
}

void CriticalsModule::on_enable() {
    if (apply()) {
        Logger::instance().info("Criticals enabled; vanilla attacks use the native critical branch.");
    } else {
        Logger::instance().info("Criticals could not be enabled safely; attack code was left unchanged.");
    }
}

void CriticalsModule::on_disable() {
    restore();
    Logger::instance().info("Criticals disabled; vanilla critical prerequisites restored.");
}

bool CriticalsModule::verify() noexcept {
    HMODULE image = GetModuleHandleW(nullptr);
    if (!supported_image(image)) {
        return false;
    }
    const auto* base = reinterpret_cast<const unsigned char*>(image);
    for (const Patch& patch : patches) {
        if (std::memcmp(base + patch.rva, patch.original.data(), patch.size) != 0) {
            return false;
        }
    }
    return true;
}

bool CriticalsModule::apply() noexcept {
    if (patched_) {
        return true;
    }
    if (!signatures_valid_ || !verify()) {
        return false;
    }
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    SuspendedThreads suspended;
    const auto image = reinterpret_cast<std::uintptr_t>(base);
    if (!suspended.valid() || !suspended.outside(
            image + attack_begin_rva, image + attack_end_rva)) {
        return false;
    }
    std::size_t applied{};
    for (; applied < patches.size(); ++applied) {
        const Patch& patch = patches[applied];
        if (!write_bytes(base + patch.rva, patch.replacement.data(), patch.size)) {
            break;
        }
    }
    if (applied != patches.size()) {
        while (applied != 0) {
            const Patch& patch = patches[--applied];
            (void)write_bytes(base + patch.rva, patch.original.data(), patch.size);
        }
        return false;
    }
    patched_ = true;
    return true;
}

void CriticalsModule::restore() noexcept {
    if (!patched_) {
        return;
    }
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        return;
    }
    SuspendedThreads suspended;
    const auto image = reinterpret_cast<std::uintptr_t>(base);
    if (!suspended.valid() || !suspended.outside(
            image + attack_begin_rva, image + attack_end_rva)) {
        return;
    }
    for (const Patch& patch : patches) {
        (void)write_bytes(base + patch.rva, patch.original.data(), patch.size);
    }
    patched_ = false;
}

} // namespace utility::modules
