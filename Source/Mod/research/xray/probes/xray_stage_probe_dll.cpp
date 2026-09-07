#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr DWORD kExpectedTimestamp = 0x6A8378BA;
constexpr DWORD kExpectedImageSize = 0x12888000;
constexpr std::uintptr_t kStageRva = 0x650FD60;
constexpr std::uintptr_t kStageEndRva = 0x651174B;
constexpr std::size_t kPatchLength = 19;
constexpr std::array<std::byte, kPatchLength> kExpectedBytes{
    std::byte{0x55}, std::byte{0x41}, std::byte{0x57}, std::byte{0x41},
    std::byte{0x56}, std::byte{0x41}, std::byte{0x55}, std::byte{0x41},
    std::byte{0x54}, std::byte{0x56}, std::byte{0x57}, std::byte{0x53},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xEC}, std::byte{0xD8},
    std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
};

using StageFunction = void(__fastcall*)(void*, void*, void*);

StageFunction g_original = nullptr;
std::atomic<unsigned> g_captureCount{0};
std::filesystem::path g_logPath;

bool readable(const void* pointer, std::size_t size) {
    if (pointer == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(pointer, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    return start >= regionStart && size <= info.RegionSize &&
           start - regionStart <= info.RegionSize - size;
}

std::filesystem::path outputDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(buffer.data()) / L"BedrockUtilityFramework";
}

void appendText(const char* text) {
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    CloseHandle(file);
}

void captureRegistry(void* inputs, unsigned captureIndex) {
    char line[2048]{};
    const auto inputAddress = reinterpret_cast<std::uintptr_t>(inputs);
    if (!readable(inputs, 16)) {
        std::snprintf(line, sizeof(line), "capture %u: inputs=%p unreadable\r\n",
                      captureIndex, inputs);
        appendText(line);
        return;
    }

    const auto begin = *reinterpret_cast<const std::uintptr_t*>(inputAddress);
    const auto end = *reinterpret_cast<const std::uintptr_t*>(inputAddress + 8);
    if (begin == 0 || end < begin || end - begin > 64 * 512 ||
        (end - begin) % 64 != 0 || !readable(reinterpret_cast<const void*>(begin), end - begin)) {
        std::snprintf(line, sizeof(line),
                      "capture %u: inputs=%p invalid registry [%p,%p)\r\n",
                      captureIndex, inputs, reinterpret_cast<void*>(begin),
                      reinterpret_cast<void*>(end));
        appendText(line);
        return;
    }

    const std::size_t count = (end - begin) / 64;
    std::snprintf(line, sizeof(line),
                  "capture %u: inputs=%p entries=%zu\r\n",
                  captureIndex, inputs, count);
    appendText(line);

    for (std::size_t index = 0; index < count; ++index) {
        const auto entry = begin + index * 64;
        const auto object = *reinterpret_cast<const std::uintptr_t*>(entry);
        const auto taggedType = *reinterpret_cast<const std::uintptr_t*>(entry + 0x38);
        const auto typeBase = taggedType & ~std::uintptr_t{3};
        const char* typeName = nullptr;
        if (typeBase != 0 && readable(reinterpret_cast<const void*>(typeBase + 16), 4)) {
            const char* candidate = reinterpret_cast<const char*>(typeBase + 16);
            if (candidate[0] == '.' && candidate[1] == '?' && candidate[2] == 'A') {
                typeName = candidate;
            }
        }

        std::array<std::uintptr_t, 8> words{};
        if (object != 0 && readable(reinterpret_cast<const void*>(object), sizeof(words))) {
            std::memcpy(words.data(), reinterpret_cast<const void*>(object), sizeof(words));
        }
        std::snprintf(
            line, sizeof(line),
            "  [%03zu] object=%p type=%p name=%.320s words="
            "%016llX %016llX %016llX %016llX %016llX %016llX %016llX %016llX\r\n",
            index, reinterpret_cast<void*>(object), reinterpret_cast<void*>(typeBase),
            typeName != nullptr ? typeName : "<unresolved>",
            static_cast<unsigned long long>(words[0]),
            static_cast<unsigned long long>(words[1]),
            static_cast<unsigned long long>(words[2]),
            static_cast<unsigned long long>(words[3]),
            static_cast<unsigned long long>(words[4]),
            static_cast<unsigned long long>(words[5]),
            static_cast<unsigned long long>(words[6]),
            static_cast<unsigned long long>(words[7]));
        appendText(line);
    }
}

void __fastcall stageHook(void* self, void* description, void* inputs) {
    const unsigned captureIndex = g_captureCount.fetch_add(1, std::memory_order_relaxed);
    if (captureIndex < 8) {
        captureRegistry(inputs, captureIndex);
    }
    g_original(self, description, inputs);
}

void writeAbsoluteJump(std::byte* destination, const void* target) {
    destination[0] = std::byte{0xFF};
    destination[1] = std::byte{0x25};
    destination[2] = destination[3] = destination[4] = destination[5] = std::byte{0};
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(destination + 6, &address, sizeof(address));
}

struct SuspendedThread {
    HANDLE handle{};
};

void resumeThreads(std::vector<SuspendedThread>& threads) {
    for (auto& thread : threads) {
        ResumeThread(thread.handle);
        CloseHandle(thread.handle);
    }
    threads.clear();
}

bool suspendThreads(std::vector<SuspendedThread>& threads,
                    std::uintptr_t stageStart,
                    std::uintptr_t stageEnd) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    THREADENTRY32 entry{static_cast<DWORD>(sizeof(THREADENTRY32))};
    bool safe = true;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == currentThreadId) {
                continue;
            }
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                                       FALSE, entry.th32ThreadID);
            if (thread == nullptr || SuspendThread(thread) == static_cast<DWORD>(-1)) {
                if (thread != nullptr) CloseHandle(thread);
                safe = false;
                break;
            }
            threads.push_back({thread});
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread, &context) ||
                (context.Rip >= stageStart && context.Rip < stageEnd)) {
                safe = false;
                break;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return safe;
}

bool installHook(std::byte* target, std::uintptr_t stageEnd) {
    if (std::memcmp(target, kExpectedBytes.data(), kPatchLength) != 0) {
        appendText("failed closed: stage prologue mismatch\r\n");
        return false;
    }

    auto* trampoline = static_cast<std::byte*>(VirtualAlloc(
        nullptr, kPatchLength + 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        appendText("failed: trampoline allocation\r\n");
        return false;
    }
    std::memcpy(trampoline, target, kPatchLength);
    writeAbsoluteJump(trampoline + kPatchLength, target + kPatchLength);
    g_original = reinterpret_cast<StageFunction>(trampoline);

    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        std::vector<SuspendedThread> threads;
        threads.reserve(256);
        const bool safe = suspendThreads(
            threads, reinterpret_cast<std::uintptr_t>(target), stageEnd);
        if (safe) {
            DWORD oldProtection = 0;
            if (VirtualProtect(target, kPatchLength, PAGE_EXECUTE_READWRITE, &oldProtection)) {
                std::array<std::byte, kPatchLength> patch{};
                patch.fill(std::byte{0x90});
                writeAbsoluteJump(patch.data(), reinterpret_cast<const void*>(&stageHook));
                std::memcpy(target, patch.data(), patch.size());
                FlushInstructionCache(GetCurrentProcess(), target, patch.size());
                DWORD ignored = 0;
                VirtualProtect(target, kPatchLength, oldProtection, &ignored);
                resumeThreads(threads);
                appendText("installed: guarded block-stage registry probe\r\n");
                return true;
            }
        }
        resumeThreads(threads);
        Sleep(25);
    }
    VirtualFree(trampoline, 0, MEM_RELEASE);
    g_original = nullptr;
    appendText("failed: stage remained active during guarded install\r\n");
    return false;
}

DWORD WINAPI initialize(void*) {
    const auto directory = outputDirectory();
    if (directory.empty()) return 1;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return 2;
    g_logPath = directory / L"xray-stage-probe-v3.log";
    DeleteFileW(g_logPath.c_str());
    appendText("startup: XrayStageProbe3 runtime-classifier probe entered\r\n");

    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) return 3;
    const auto* base = reinterpret_cast<const std::byte*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 4;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    char identity[160]{};
    std::snprintf(identity, sizeof(identity),
                  "identity: timestamp=0x%08lX imageSize=0x%08lX\r\n",
                  static_cast<unsigned long>(nt->FileHeader.TimeDateStamp),
                  static_cast<unsigned long>(nt->OptionalHeader.SizeOfImage));
    appendText(identity);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
        nt->OptionalHeader.SizeOfImage != kExpectedImageSize) {
        appendText("failed closed: Minecraft version does not match 1.26.4501.0\r\n");
        return 5;
    }
    auto* target = const_cast<std::byte*>(base + kStageRva);
    installHook(target, reinterpret_cast<std::uintptr_t>(base) + kStageEndRva);
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    }
    return TRUE;
}
