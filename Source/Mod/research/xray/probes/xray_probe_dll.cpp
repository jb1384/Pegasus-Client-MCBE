#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr DWORD kExpectedTimestamp = 0x6A8378BA;
constexpr DWORD kExpectedImageSize = 0x12888000;

std::filesystem::path outputDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) {
        return {};
    }

    return std::filesystem::path(buffer.data()) / L"BedrockUtilityFramework";
}

void writeStatus(const std::filesystem::path& directory, const std::string& text) {
    std::ofstream stream(directory / L"xray-probe-status.txt", std::ios::trunc);
    stream << text << '\n';
}

bool readablePage(const DWORD protection) {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const DWORD baseProtection = protection & 0xFF;
    return baseProtection == PAGE_READONLY || baseProtection == PAGE_READWRITE ||
           baseProtection == PAGE_WRITECOPY || baseProtection == PAGE_EXECUTE ||
           baseProtection == PAGE_EXECUTE_READ ||
           baseProtection == PAGE_EXECUTE_READWRITE ||
           baseProtection == PAGE_EXECUTE_WRITECOPY;
}

DWORD WINAPI captureMappedImage(void*) {
    const auto directory = outputDirectory();
    if (directory.empty()) {
        return 1;
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return 2;
    }

    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        writeStatus(directory, "failed: main module was not found");
        return 3;
    }

    const auto* base = reinterpret_cast<const std::byte*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        writeStatus(directory, "failed: invalid DOS header");
        return 4;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
        nt->OptionalHeader.SizeOfImage != kExpectedImageSize) {
        writeStatus(directory, "failed closed: Minecraft version does not match 1.26.4501.0");
        return 5;
    }

    const auto dumpPath = directory / L"minecraft-1.26.4501.0-mapped.bin";
    HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        writeStatus(directory, "failed: could not create mapped-image file");
        return 6;
    }

    std::array<std::byte, 64 * 1024> zeroes{};
    std::uintptr_t offset = 0;
    bool success = true;
    while (offset < kExpectedImageSize) {
        MEMORY_BASIC_INFORMATION page{};
        if (VirtualQuery(base + offset, &page, sizeof(page)) != sizeof(page)) {
            success = false;
            break;
        }

        const auto pageAddress = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
        const auto pageOffset = pageAddress > baseAddress ? pageAddress - baseAddress : 0;
        const auto regionEnd = pageOffset + page.RegionSize;
        const auto writeEnd = regionEnd < kExpectedImageSize ? regionEnd : kExpectedImageSize;
        auto writeStart = offset > pageOffset ? offset : pageOffset;
        std::size_t remaining = static_cast<std::size_t>(writeEnd - writeStart);
        const bool canRead = page.State == MEM_COMMIT && readablePage(page.Protect);

        while (remaining != 0) {
            const DWORD chunk = static_cast<DWORD>(
                remaining < zeroes.size() ? remaining : zeroes.size());
            const void* source = canRead ? static_cast<const void*>(base + writeStart)
                                         : static_cast<const void*>(zeroes.data());
            DWORD written = 0;
            if (!WriteFile(file, source, chunk, &written, nullptr) || written != chunk) {
                success = false;
                break;
            }
            writeStart += chunk;
            remaining -= chunk;
        }

        if (!success) {
            break;
        }
        offset = writeEnd;
    }

    CloseHandle(file);
    if (!success || offset != kExpectedImageSize) {
        DeleteFileW(dumpPath.c_str());
        writeStatus(directory, "failed: mapped-image capture was incomplete");
        return 7;
    }

    writeStatus(directory,
                "complete: minecraft-1.26.4501.0-mapped.bin is ready for analysis");
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, captureMappedImage, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
