# Visual Studio 2026 build configuration

This solution is retargeted for Visual Studio 2026 / MSBuild C++.

## Project settings

- Visual Studio solution version: 18
- VCProjectVersion: 18.0
- PlatformToolset: v145
- Language standard: C++17 (`stdcpp17`)
- Platform: x64
- Windows SDK: `10.0` in the project file, so MSBuild selects an installed Windows 10/11 SDK.
- Build system: MSBuild `.sln/.vcxproj` (no CMake).

## Required installation

Open Visual Studio Installer and ensure **Desktop development with C++** is installed.
The repository also includes `.vsconfig`; Visual Studio can use it to offer the required components.

## Build

1. Open `ScanEngineSolution.sln` in Visual Studio 2026.
2. Select `Debug | x64`.
3. Build -> Rebuild Solution.
4. Expected output directory: `x64\\Debug\\`.

Expected files:

- `ScanEngine.dll`
- `ScanService.exe`
- `client.exe`

## Run

Terminal 1:

```cmd
cd /d <solution>\\x64\\Debug
ScanService.exe --console
```

Terminal 2:

```cmd
cd /d <solution>\\x64\\Debug
client.exe scan "D:\\test.exe" --priority high
```

## Why this retarget was needed

The previous modular package still used `v143` and Visual Studio project version 17.0. Visual Studio 2026 can open older projects, but `v143` only builds if that older toolset is installed side-by-side. This package explicitly uses `v145`, the Visual Studio 2026 MSBuild C++ platform toolset.
