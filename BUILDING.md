# Building Pegasus

## Download the compiled version

Open this repository's Releases page and download the ZIP from **v1.0.0-beta**. Extract it before running `Pegasus.exe`; keep `BedrockUtilityFramework.Xray.dll` beside it. Both files are also available individually. Release assets have SHA-256 checksums.

The compiled mod requires the x64 Visual Studio C++ debug runtimes. A standard Visual C++ Redistributable installation alone does not provide those debug libraries. See README for the full requirements.

## Build the injector

On Windows x64 with .NET Framework 4.8, run from the repository root:

```powershell
& '.\Source\Injector\build.ps1'
```

This creates `Pegasus.exe` at the repository root using the Windows .NET compiler. No NuGet packages are needed. The injector's `ExpectedHash` is pinned to the DLL uploaded with this release.

## Build the current mod source

Install Visual Studio 2022 with Desktop development with C++, a Windows SDK, and CMake 3.24 or newer. Open a developer PowerShell prompt in the repository root:

```powershell
cmake -S '.\Source\Mod' -B C:/pegasus-build -G 'Visual Studio 17 2022' -A x64
cmake --build C:/pegasus-build --config Debug
ctest --test-dir C:/pegasus-build -C Debug --output-on-failure
```

The output is `C:/pegasus-build/Debug/BedrockUtilityFramework.Xray.dll`.

The current source includes later unfinished navigation development. It does **not** reproduce the released DLL exactly. The released DLL was made from the original completed smooth-jetpack gameplay objects with a rebuilt splash hook. Those historical object files are not required for building the current source and are not included in this repository.

To use an intentionally rebuilt DLL with the injector, calculate its SHA-256 with `Get-FileHash`, update `ExpectedHash` in `Source/Injector/Pegasus.cs`, and rebuild the injector. Keep the resulting EXE and DLL together.

## Verification

The release DLL passed a disposable-process injection check. The source snapshot previously compiled with thirteen tests passing and one environment-dependent menu test skipped; the subsequently added splash text test also passed separately. These checks do not certify in-game behavior on every Minecraft version.
