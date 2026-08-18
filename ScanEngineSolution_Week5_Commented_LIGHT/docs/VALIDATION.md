# Validation

Da kiem tra trong moi truong tao artifact:

- Tat ca `.vcxproj` parse XML hop le.
- Tat ca file Week 5 da duoc them vao `.vcxproj/.filters` va source path ton tai.
- 20 translation units C++ bi thay doi/bo sung da qua `g++ -std=c++17 -fsyntax-only` voi Windows API stub de bat loi syntax, include va interface noi bo.
- `PeReader` da duoc check lai: DataDirectory/Security Directory/RVA vuot file tra `STRUCT_CORRUPT`.
- `EventHistory` reject `lastEventSeq` qua cu so voi history hoac lon hon sequence ma session da tao.
- Output ZIP loai `.vs`, `x64` va build artifacts; van giu `.sln`, `.vcxproj`, `.filters`, source va scripts.

## Gioi han cua validation

Container hien tai la Linux, khong co MSVC/Windows SDK thuc. Vi vay chua the xac nhan buoc compile/link/runtime Windows cuoi cung (`CreateNamedPipe`, impersonation, `WinVerifyTrust`) bang Visual Studio tai day.

Tren may Windows cua ban:

1. Mo `ScanEngineSolution.sln` bang Visual Studio.
2. Chon `Debug | x64` (hoac `Release | x64`).
3. `Build -> Rebuild Solution`.
4. Chay `run_console.cmd` hoac `ScanService.exe console`.
5. Test client scan file PE thuong, file corrupt, cache hit, rate limit va reconnect/resume.
