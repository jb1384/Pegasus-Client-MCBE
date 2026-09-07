# pegasus utility mod 1.0

Source for Pegasus utility mod and its desktop injector. Download compiled binaries from this repository's Releases page. See [BUILDING.md](BUILDING.md) for build instructions and source/release provenance.

## Start

1. Keep `Pegasus.exe` and `BedrockUtilityFramework.Xray.dll` together in this folder.
2. Start a fresh Minecraft Bedrock 64-bit session. The injector does not check the game version.
3. Open `Pegasus.exe`. Select your game session, then choose **Load utility mod**. Use Refresh if you started Minecraft afterward.
4. In a Minecraft world, press **Tab** for the menu (clickgui) or use the arrow keys to activate modules.

Restart Minecraft before loading again or switching DLL versions. 

## Requirements

- Windows 10/11 x64, with .NET Framework 4.8 for the injector.
- The bundled DLL depends on the x64 Visual Studio C++ debug runtimes: `MSVCP140D.dll`, `VCRUNTIME140D.dll`, `VCRUNTIME140_1D.dll`, and `ucrtbased.dll`. These come with the C++ development tools and are not included here. The normal Visual C++ Redistributable alone does not supply them.
- The game must be able to read this folder. The injector grants packaged applications read/execute access to the bundled DLL only. A restricted parent folder can still prevent loading; use an accessible local folder if needed.
- If access is denied, run Pegasus with the same privilege level as Minecraft or administrator privledges.

## Release contents and source provenance

- `BedrockUtilityFramework.Xray.dll`
- `Pegasus.exe`: x64 desktop injector with a dark purple GUI. It validates the bundled DLL's SHA-256, does not restrict the Minecraft version, and uses the standard Windows DLL loader.
- `Source/Injector`: complete C# GUI/injector source and build script.
- `Source/Mod`: full current C++ module source, tests, CMake configuration, and optional probe sources referenced by that configuration.
- `SHA256SUMS.txt`: checksums provided alongside the downloadable release assets.

**Source snapshot distinction:** the available current mod source contains later, unfinished Baritone/navigation changes. It includes the smooth-jetpack implementation, but it is not an exact historical source snapshot of the bundled DLL. No matching pre-navigation source snapshot was found. Rebuilding it produces the current development version. The packaged DLL preserves the completed jetpack gameplay objects and rebuilds only the splash-text hook. The later navigation source was not linked into this DLL.

## Build the injector

From PowerShell in this folder:

```powershell
& '.\Source\Injector\build.ps1'
```

The script uses the Windows .NET Framework compiler and writes `Pegasus.exe` here. No external packages are required. If intentionally distributing a different DLL, update `ExpectedHash` in the injector source before rebuilding.

## Build the mod source

Requires Visual Studio 2022 with Desktop development with C++, a Windows SDK, and CMake 3.24 or newer. From this folder, choose a short writable build directory:

```powershell
cmake -S '.\Source\Mod' -B C:/pegasus-build -G 'Visual Studio 17 2022' -A x64
cmake --build C:/pegasus-build --config Debug
ctest --test-dir C:/pegasus-build -C Debug --output-on-failure
```

The output is `C:/pegasus-build/Debug/BedrockUtilityFramework.Xray.dll`. This is the current development source build described above. It is not automatically copied into this package.

