# Validation - ban co ban

Kiem tra truoc khi nop:

1. Mo `ScanEngineSolution.sln`.
2. Chon x64 / Debug hoac Release.
3. Rebuild Solution.
4. Dam bao co `ScanEngine.dll`, `ScanService.exe`, `client.exe` trong thu muc output.
5. Chay `ScanService.exe --console`.
6. Chay `client.exe scan "<duong-dan-file>" --priority high`.
7. Scan lai cung file trong 10 phut de thay `[CACHE HIT]`.
8. Thu `stress` voi 20 client de quan sat queue/worker/throttle.
