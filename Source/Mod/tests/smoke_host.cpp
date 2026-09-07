#include <Windows.h>

#include <chrono>
#include <iostream>
#include <thread>

int wmain(int argument_count, wchar_t* arguments[]) {
    if (argument_count != 2) {
        std::wcerr << L"Usage: BedrockUtilitySmokeHost.exe <dll-path>\n";
        return 2;
    }

    const HMODULE library = LoadLibraryW(arguments[1]);
    if (library == nullptr) {
        std::wcerr << L"LoadLibraryW failed with error " << GetLastError() << L".\n";
        return 1;
    }

    // Give the DLL's initialization worker time to finish before this disposable
    // host exits and Windows unloads its modules.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::wcout << L"DLL loaded in the smoke-test process.\n";
    return 0;
}
