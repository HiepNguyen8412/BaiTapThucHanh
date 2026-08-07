# Validation

Da kiem tra trong moi truong tao artifact:

- Tat ca `#include "..."` deu resolve duoc theo include directories cua tung project.
- Tat ca file duoc khai bao trong `.vcxproj` deu ton tai.
- Tat ca `.vcxproj` va `.vcxproj.filters` parse XML hop le.
- Solution chi gom 3 project cua luong chinh: `ScanEngine`, `ScanService`, `ScanClient`.
- `ScanEngineTest` duoc loai khoi solution modular de khong lam roi main flow.

Khong the thuc hien MSVC/Windows SDK build trong container Linux hien tai. Hay mo `ScanEngineSolution.sln` bang Visual Studio 2026, chon `Debug | x64`, sau do `Build -> Rebuild Solution` de xac nhan compile tren Windows.
