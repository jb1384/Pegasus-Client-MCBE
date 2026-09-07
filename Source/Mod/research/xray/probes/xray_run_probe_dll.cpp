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
constexpr std::uintptr_t kRunRva = 0x546C810;
constexpr std::uintptr_t kRunEndRva = 0x546CE13;
constexpr std::size_t kPatchLength = 19;
constexpr std::array<std::byte, kPatchLength> kExpectedBytes{
    std::byte{0x55}, std::byte{0x41}, std::byte{0x57}, std::byte{0x41},
    std::byte{0x56}, std::byte{0x41}, std::byte{0x55}, std::byte{0x41},
    std::byte{0x54}, std::byte{0x56}, std::byte{0x57}, std::byte{0x53},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xEC}, std::byte{0xD8},
    std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
};

using RunFunction = void(__fastcall*)(void*, void*);

RunFunction g_original = nullptr;
std::filesystem::path g_logPath;
std::atomic<unsigned> g_callCount{0};
SRWLOCK g_captureLock = SRWLOCK_INIT;
std::array<std::uintptr_t, 512> g_seen{};
std::atomic<std::size_t> g_seenCount{0};
std::atomic<unsigned> g_phase{1};
std::atomic<bool> g_nameScanComplete{false};
std::atomic<bool> g_contextGraphCaptured{false};
ULONGLONG g_startTick = 0;

struct Event {
    std::atomic<unsigned> ready{0};
    unsigned milliseconds{};
    unsigned phase{};
    std::uint16_t blockIndex{};
    std::uint64_t signature{};
};

std::array<Event, 200000> g_events{};
std::atomic<std::size_t> g_eventCount{0};
std::size_t g_flushedEvents = 0;

bool readable(const void* pointer, std::size_t size) {
    if (pointer == nullptr || size == 0) return false;
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
    if (count == 0 || count >= buffer.size()) return {};
    return std::filesystem::path(buffer.data()) / L"BedrockUtilityFramework";
}

void appendText(const std::string& text) {
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

void appendHexAndAscii(std::string& output, const char* label,
                       const void* pointer, std::size_t size) {
    if (!readable(pointer, size)) return;
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    char line[1024]{};
    int used = std::snprintf(line, sizeof(line), "    %s %p hex=", label, pointer);
    for (std::size_t index = 0; index < size && used > 0 &&
         static_cast<std::size_t>(used) + 3 < sizeof(line); ++index) {
        used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used),
                              "%02X", bytes[index]);
    }
    if (used > 0 && static_cast<std::size_t>(used) + size + 12 < sizeof(line)) {
        used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used),
                              " ascii=");
        for (std::size_t index = 0; index < size; ++index) {
            const unsigned char value = bytes[index];
            line[used++] = value >= 32 && value <= 126 ? static_cast<char>(value) : '.';
        }
        line[used++] = '\r';
        line[used++] = '\n';
        line[used] = '\0';
    }
    output.append(line, static_cast<std::size_t>(used));
}

bool markFirstSeen(std::uintptr_t object) {
    if (object == 0) return false;
    AcquireSRWLockExclusive(&g_captureLock);
    const std::size_t count = g_seenCount.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < count; ++index) {
        if (g_seen[index] == object) {
            ReleaseSRWLockExclusive(&g_captureLock);
            return false;
        }
    }
    if (count == g_seen.size()) {
        ReleaseSRWLockExclusive(&g_captureLock);
        return false;
    }
    g_seen[count] = object;
    g_seenCount.store(count + 1, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_captureLock);
    return true;
}

void hashBytes(std::uint64_t& hash, const void* pointer, std::size_t size) {
    if (!readable(pointer, size)) {
        hash ^= 0xFF;
        hash *= 1099511628211ULL;
        return;
    }
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
}

std::uint64_t textureSignature(std::uintptr_t slot) {
    if (!readable(reinterpret_cast<const void*>(slot), 0x88)) return 0;
    std::uint64_t hash = 1469598103934665603ULL;
    const auto faceBase = *reinterpret_cast<const std::uintptr_t*>(slot + 0x38);
    const auto faceCount = *reinterpret_cast<const std::uint16_t*>(slot + 0x42);
    hashBytes(hash, reinterpret_cast<const void*>(slot + 0x40), 2);
    if (faceCount > 64 || faceBase == 0) return hash;
    for (std::uint16_t face = 0; face < faceCount; ++face) {
        const auto entry = faceBase + static_cast<std::uintptr_t>(face) * 16;
        if (!readable(reinterpret_cast<const void*>(entry), 16)) break;
        const auto descriptor = *reinterpret_cast<const std::uintptr_t*>(entry);
        hashBytes(hash, reinterpret_cast<const void*>(entry + 8), 8);
        if (descriptor != 0) {
            hashBytes(hash, reinterpret_cast<const void*>(descriptor + 0x28), 14);
        }
    }
    return hash;
}

void captureContextGraph(std::uintptr_t context, std::uint16_t blockIndex) {
    if (!g_nameScanComplete.load(std::memory_order_acquire) || blockIndex < 170 ||
        g_contextGraphCaptured.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    constexpr std::array<std::size_t, 17> strides{
        8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136,
    };
    std::string output;
    output.reserve(1024 * 1024);
    char heading[256]{};
    std::snprintf(heading, sizeof(heading),
                  "context-graph context=%p blockIndex=%u\r\n",
                  reinterpret_cast<void*>(context), static_cast<unsigned>(blockIndex));
    output += heading;
    appendHexAndAscii(output, "context", reinterpret_cast<const void*>(context), 0x800);

    for (std::size_t offset = 0; offset < 0x800; offset += sizeof(std::uintptr_t)) {
        if (!readable(reinterpret_cast<const void*>(context + offset), sizeof(std::uintptr_t))) {
            break;
        }
        const auto base = *reinterpret_cast<const std::uintptr_t*>(context + offset);
        if (!readable(reinterpret_cast<const void*>(base), 64)) continue;

        char baseLabel[96]{};
        std::snprintf(baseLabel, sizeof(baseLabel), "context-pointer+%03zX", offset);
        appendHexAndAscii(output, baseLabel, reinterpret_cast<const void*>(base), 128);

        for (const std::size_t stride : strides) {
            const auto element = base + static_cast<std::uintptr_t>(blockIndex) * stride;
            if (!readable(reinterpret_cast<const void*>(element), 64)) continue;
            const auto pointee = *reinterpret_cast<const std::uintptr_t*>(element);
            if (!readable(reinterpret_cast<const void*>(pointee), 64)) continue;
            char elementLabel[128]{};
            std::snprintf(elementLabel, sizeof(elementLabel),
                          "candidate+%03zX-stride-%03zX", offset, stride);
            appendHexAndAscii(output, elementLabel,
                              reinterpret_cast<const void*>(element), 96);
            char pointeeLabel[128]{};
            std::snprintf(pointeeLabel, sizeof(pointeeLabel),
                          "candidate-pointee+%03zX-stride-%03zX", offset, stride);
            appendHexAndAscii(output, pointeeLabel,
                              reinterpret_cast<const void*>(pointee), 256);
        }
    }
    appendText(output);
    appendText("context-graph complete\r\n");
}

void recordEvent(unsigned phase, std::uint16_t blockIndex, std::uint64_t signature) {
    const std::size_t index = g_eventCount.fetch_add(1, std::memory_order_relaxed);
    if (index >= g_events.size()) return;
    auto& event = g_events[index];
    event.milliseconds = static_cast<unsigned>(GetTickCount64() - g_startTick);
    event.phase = phase;
    event.blockIndex = blockIndex;
    event.signature = signature;
    event.ready.store(1, std::memory_order_release);
}

void flushEvents() {
    const std::size_t reserved =
        (g_eventCount.load(std::memory_order_acquire) < g_events.size())
            ? g_eventCount.load(std::memory_order_relaxed)
            : g_events.size();
    std::string output;
    output.reserve(65536);
    while (g_flushedEvents < reserved) {
        const auto& event = g_events[g_flushedEvents];
        if (event.ready.load(std::memory_order_acquire) == 0) break;
        char line[160]{};
        std::snprintf(line, sizeof(line),
                      "event ms=%u phase=%u blockIndex=%u signature=%016llX\r\n",
                      event.milliseconds, event.phase,
                      static_cast<unsigned>(event.blockIndex),
                      static_cast<unsigned long long>(event.signature));
        output += line;
        ++g_flushedEvents;
        if (output.size() >= 60000) {
            appendText(output);
            output.clear();
        }
    }
    if (!output.empty()) appendText(output);
}

void captureRun(void* arguments, unsigned callIndex) {
    const unsigned phase = g_phase.load(std::memory_order_relaxed);
    if (phase == 0) return;
    if (!readable(arguments, 0x78)) return;
    const auto address = reinterpret_cast<std::uintptr_t>(arguments);
    const auto records = *reinterpret_cast<const std::uintptr_t*>(address + 0x48);
    const auto first = *reinterpret_cast<const std::uint64_t*>(address + 0x68);
    const auto last = *reinterpret_cast<const std::uint64_t*>(address + 0x70);
    if (records == 0 || last < first || last - first > 65536) return;

    for (std::uint64_t index = first; index < last; ++index) {
        const auto record = records + index * 24;
        if (!readable(reinterpret_cast<const void*>(record), 24)) break;
        const auto object = *reinterpret_cast<const std::uintptr_t*>(record + 8);
        const auto context = *reinterpret_cast<const std::uintptr_t*>(record + 16);
        std::uint16_t id2a = 0xFFFF;
        std::uint16_t id2c = 0xFFFF;
        if (readable(reinterpret_cast<const void*>(object), 0x60)) {
            id2a = *reinterpret_cast<const std::uint16_t*>(object + 0x2A);
            id2c = *reinterpret_cast<const std::uint16_t*>(object + 0x2C);
        }
        if (id2a == 0xFFFF || !readable(reinterpret_cast<const void*>(context), 0x728)) {
            continue;
        }
        const auto table720 = *reinterpret_cast<const std::uintptr_t*>(context + 0x720);
        const auto table708 = *reinterpret_cast<const std::uintptr_t*>(context + 0x708);
        const auto renderSlot = table720 + static_cast<std::uintptr_t>(id2a) * 0x88;
        if (table720 == 0 || !readable(reinterpret_cast<const void*>(renderSlot), 8)) {
            continue;
        }
        const auto renderData = *reinterpret_cast<const std::uintptr_t*>(renderSlot);
        const std::uint64_t signature = textureSignature(renderSlot);
        recordEvent(phase, id2a, signature);
        captureContextGraph(context, id2a);
        if (!markFirstSeen(static_cast<std::uintptr_t>(signature))) continue;

        AcquireSRWLockExclusive(&g_captureLock);
        std::string output;
        char item[512]{};
        std::snprintf(item, sizeof(item),
                      "  texture[%zu] call=%u phase=%u slot=%llu object=%p context=%p "
                      "id2a=%u id2c=%u table720=%p render=%p table708=%p\r\n",
                      g_seenCount.load(std::memory_order_relaxed) - 1, callIndex,
                      phase,
                      static_cast<unsigned long long>(index),
                      reinterpret_cast<void*>(object), reinterpret_cast<void*>(context),
                      static_cast<unsigned>(id2a), static_cast<unsigned>(id2c),
                      reinterpret_cast<void*>(table720), reinterpret_cast<void*>(renderData),
                      reinterpret_cast<void*>(table708));
        output += item;
        appendHexAndAscii(output, "object", reinterpret_cast<const void*>(object), 64);
        appendHexAndAscii(output, "texture-slot",
                          reinterpret_cast<const void*>(renderSlot), 0x88);
        if (table708 != 0) {
            const auto attributes = table708 + static_cast<std::uintptr_t>(id2c) * 0x58;
            appendHexAndAscii(output, "attributes",
                              reinterpret_cast<const void*>(attributes), 0x58);
        }

        const auto faceBase = *reinterpret_cast<const std::uintptr_t*>(renderSlot + 0x38);
        const auto faceCount = *reinterpret_cast<const std::uint16_t*>(renderSlot + 0x42);
        if (faceBase != 0 && faceCount <= 64) {
            for (std::uint16_t face = 0; face < faceCount; ++face) {
                const auto entry = faceBase + static_cast<std::uintptr_t>(face) * 16;
                appendHexAndAscii(output, "face-entry",
                                  reinterpret_cast<const void*>(entry), 16);
                if (readable(reinterpret_cast<const void*>(entry), 8)) {
                    const auto descriptor =
                        *reinterpret_cast<const std::uintptr_t*>(entry);
                    appendHexAndAscii(output, "face-descriptor",
                                      reinterpret_cast<const void*>(descriptor), 64);
                }
            }
        }
        appendText(output);
        ReleaseSRWLockExclusive(&g_captureLock);
    }
}

void __fastcall runHook(void* self, void* arguments) {
    const unsigned callIndex = g_callCount.fetch_add(1, std::memory_order_relaxed);
    captureRun(arguments, callIndex);
    g_original(self, arguments);
}

void scanCanonicalNames() {
    constexpr std::array<const char*, 7> needles{
        "minecraft:stone", "minecraft:diamond_ore", "minecraft:coal_ore",
        "minecraft:ancient_debris", "minecraft:bedrock", "minecraft:water",
        "minecraft:lava",
    };
    std::array<unsigned, needles.size()> hitCounts{};
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    auto cursor = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const auto maximum = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    std::vector<unsigned char> buffer(1024 * 1024 + 64);
    const HANDLE process = GetCurrentProcess();
    appendText("scan: canonical-name memory scan started\r\n");
    while (cursor < maximum) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) !=
            sizeof(info)) {
            break;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto regionEnd = regionStart + info.RegionSize;
        const DWORD baseProtection = info.Protect & 0xFF;
        const bool canRead = info.State == MEM_COMMIT &&
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
            baseProtection != PAGE_EXECUTE;
        if (canRead) {
            std::uintptr_t chunkStart = regionStart;
            std::size_t carry = 0;
            while (chunkStart < regionEnd) {
                const std::size_t wanted = static_cast<std::size_t>(
                    (regionEnd - chunkStart) < 1024 * 1024
                        ? (regionEnd - chunkStart) : 1024 * 1024);
                SIZE_T copied = 0;
                if (!ReadProcessMemory(process, reinterpret_cast<const void*>(chunkStart),
                                       buffer.data() + carry, wanted, &copied) || copied == 0) {
                    break;
                }
                const std::size_t available = carry + static_cast<std::size_t>(copied);
                for (std::size_t needleIndex = 0; needleIndex < needles.size(); ++needleIndex) {
                    if (hitCounts[needleIndex] >= 64) continue;
                    const char* needle = needles[needleIndex];
                    const std::size_t length = std::strlen(needle);
                    for (std::size_t offset = 0; offset + length <= available; ++offset) {
                        if (buffer[offset] != static_cast<unsigned char>(needle[0]) ||
                            std::memcmp(buffer.data() + offset, needle, length) != 0) {
                            continue;
                        }
                        const auto hit = chunkStart - carry + offset;
                        char line[320]{};
                        std::snprintf(line, sizeof(line),
                                      "name-hit needle=%s address=%p type=0x%lX allocation=%p\r\n",
                                      needle, reinterpret_cast<void*>(hit),
                                      static_cast<unsigned long>(info.Type), info.AllocationBase);
                        appendText(line);
                        const auto contextStart = hit >= 128 ? hit - 128 : hit;
                        std::string context;
                        appendHexAndAscii(context, "name-context",
                                          reinterpret_cast<const void*>(contextStart), 320);
                        appendText(context);
                        ++hitCounts[needleIndex];
                        if (hitCounts[needleIndex] >= 64) break;
                    }
                }
                carry = available < 63 ? available : 63;
                if (carry != 0) {
                    std::memmove(buffer.data(), buffer.data() + available - carry, carry);
                }
                chunkStart += copied;
            }
        }
        if (regionEnd <= cursor) break;
        cursor = regionEnd;
    }
    appendText("scan: canonical-name memory scan complete\r\n");
    g_nameScanComplete.store(true, std::memory_order_release);
}

DWORD WINAPI phaseMonitor(void*) {
    ULONGLONG nextFlush = GetTickCount64();
    bool scanned = false;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (!scanned && now - g_startTick >= 5000) {
            scanCanonicalNames();
            scanned = true;
        }
        if (now >= nextFlush) {
            flushEvents();
            nextFlush = now + 1000;
        }
        Sleep(100);
    }
}

void writeAbsoluteJump(std::byte* destination, const void* target) {
    destination[0] = std::byte{0xFF};
    destination[1] = std::byte{0x25};
    destination[2] = destination[3] = destination[4] = destination[5] = std::byte{0};
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(destination + 6, &address, sizeof(address));
}

struct SuspendedThread { HANDLE handle{}; };

void resumeThreads(std::vector<SuspendedThread>& threads) {
    for (auto& thread : threads) {
        ResumeThread(thread.handle);
        CloseHandle(thread.handle);
    }
    threads.clear();
}

bool suspendThreads(std::vector<SuspendedThread>& threads,
                    std::uintptr_t start, std::uintptr_t end) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    THREADENTRY32 entry{static_cast<DWORD>(sizeof(THREADENTRY32))};
    bool safe = true;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == currentThreadId) continue;
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
                (context.Rip >= start && context.Rip < end)) {
                safe = false;
                break;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return safe;
}

bool installHook(std::byte* target, std::uintptr_t runEnd) {
    if (std::memcmp(target, kExpectedBytes.data(), kPatchLength) != 0) {
        appendText("failed closed: run prologue mismatch\r\n");
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
    g_original = reinterpret_cast<RunFunction>(trampoline);

    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        std::vector<SuspendedThread> threads;
        threads.reserve(256);
        const bool safe = suspendThreads(
            threads, reinterpret_cast<std::uintptr_t>(target), runEnd);
        if (safe) {
            DWORD oldProtection = 0;
            if (VirtualProtect(target, kPatchLength, PAGE_EXECUTE_READWRITE, &oldProtection)) {
                std::array<std::byte, kPatchLength> patch{};
                patch.fill(std::byte{0x90});
                writeAbsoluteJump(patch.data(), reinterpret_cast<const void*>(&runHook));
                std::memcpy(target, patch.data(), patch.size());
                FlushInstructionCache(GetCurrentProcess(), target, patch.size());
                DWORD ignored = 0;
                VirtualProtect(target, kPatchLength, oldProtection, &ignored);
                resumeThreads(threads);
                appendText("installed: guarded live tessellation probe\r\n");
                return true;
            }
        }
        resumeThreads(threads);
        Sleep(25);
    }
    VirtualFree(trampoline, 0, MEM_RELEASE);
    g_original = nullptr;
    appendText("failed: run routine remained active during guarded install\r\n");
    return false;
}

DWORD WINAPI initialize(void*) {
    const auto directory = outputDirectory();
    if (directory.empty()) return 1;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return 2;
    g_logPath = directory / L"xray-identity-probe-v8.log";
    DeleteFileW(g_logPath.c_str());
    g_startTick = GetTickCount64();
    appendText("startup: XrayIdentityProbe8 context-graph capture entered\r\n");

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
    auto* target = const_cast<std::byte*>(base + kRunRva);
    if (installHook(target, reinterpret_cast<std::uintptr_t>(base) + kRunEndRva)) {
        HANDLE monitor = CreateThread(nullptr, 0, phaseMonitor, nullptr, 0, nullptr);
        if (monitor != nullptr) CloseHandle(monitor);
    }
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
