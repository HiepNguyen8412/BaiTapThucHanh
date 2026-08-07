# ScanEngineSolution - Modular Edition

> **Target environment:** Visual Studio 2026, MSBuild C++, platform toolset `v145`, C++17, x64.
> The project intentionally keeps `WindowsTargetPlatformVersion=10.0` so an installed Windows 10/11 SDK can be selected by MSBuild.


Muc tieu cua ban nay la **giu nguyen kien truc Client -> Service -> DLL**, nhung sap xep source theo module de doc code, debug va thuyet trinh de hon.

## 1. Main projects

- `ScanClient`: CLI, gui request/nhan event qua Named Pipe.
- `ScanService`: trung tam dieu phoi Job/Worker/Cache/Throttle va bridge toi DLL.
- `ScanEngine`: DLL phan tich file, entropy, rule scoring, callback.
- `ScanEngineTest` **khong dua vao solution modular** de tranh lam roi main flow.

## 2. Thu tu doc code (khuyen nghi)

1. `ScanClient/App/main.cpp` -> `ScanCommand()`
2. `Common/Protocol/Protocol.cpp` -> `WriteMessage()` / `ReadMessage()`
3. `ScanService/Communication/PipeServer.cpp` -> `HandleScan()`
4. `ScanService/Jobs/JobManager.cpp` -> `Submit()`
5. `ScanService/Jobs/JobManager.h` -> `QueueCompare`
6. `ScanService/Jobs/JobManager.cpp` -> `WorkerLoop()`
7. `Common/Platform/WinUtil.cpp` -> path + file identity
8. `ScanService/Cache/Cache.cpp` -> `TryGet()` / `Put()`
9. `ScanService/EngineBridge/EngineLoader.cpp` -> `Scan()`
10. `ScanEngine/Core/Engine.cpp` -> `EngineScanFile()`
11. `ScanEngine/Analysis/FileAnalyzer.cpp` -> metadata + entropy
12. `ScanService/Jobs/JobManager.cpp` -> `EngineCallback()`
13. `ScanService/Communication/PipeServer.cpp` -> event queue + `SendSnapshot()`
14. `ScanClient/App/main.cpp` -> `ReadMessage()` + display

## 3. Main flow

```text
Client ScanCommand
    -> Named Pipe / TLV
    -> PipeServer::HandleScan
    -> JobManager::Submit
       -> cap jobId + sequence
       -> jobs_[jobId]
       -> priority_queue.push
       -> queueCv.notify_one
    -> WorkerLoop
       -> Cancel?
       -> Throttle?
       -> NormalizePath + GetFileIdentity
       -> CacheKey(path + lastWriteTime + fileSize)
       -> Cache HIT: Completed fast path
       -> Cache MISS: EngineLoader::Scan
           -> ScanEngine.dll::EngineScanFile
           -> metadata + entropy + rules
           -> EngineCallback(progress/result)
    -> cache.Put(result)
    -> Job Completed
    -> Event Queue
    -> SendSnapshot / Named Pipe
    -> Client hien thi
```

## 4. Thread model

```text
Service Main Thread      : startup / stop
Accept Thread            : CreateNamedPipe + ConnectNamedPipe
ClientSession Thread     : read command cua 1 client
ClientSession EventThread: gui snapshot tu event queue
Worker Threads (2..8)    : xu ly Job va goi DLL
Throttle Thread          : CPU/RAM/I/O sampling
Telemetry Thread         : log snapshot dinh ky
```

**Engine DLL khong tao worker thread. `EngineScanFile()` chay tren worker thread cua Service.**

## 5. Build

Visual Studio 2026:

1. Mo `ScanEngineSolution.sln`
2. Chon `Debug | x64`
3. `Build -> Rebuild Solution`
4. Output: `x64\Debug\ScanEngine.dll`, `ScanService.exe`, `client.exe`

Khong dung CMake.
