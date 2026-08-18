# ScanEngineSolution - Ban co ban

He thong demo Scan Engine gom 3 thanh phan:

- `client.exe`: CLI gui lenh SCAN / QUERY / CANCEL / TELEMETRY.
- `ScanService.exe`: Named Pipe server, Job Queue, Worker Pool, Priority/Cancel, Cache, Throttle, Telemetry.
- `ScanEngine.dll`: DLL duoc nap dong, phan tich file va stream progress qua callback.

## Luong chinh

```text
Client
  -> Named Pipe \\.\pipe\AvScanPipe
  -> ClientSession
  -> HandleScan
  -> JobManager::Submit
  -> Priority Queue
  -> WorkerLoop
  -> Throttle
  -> Cache(path + lastWriteTime + size)
       HIT  -> tra ket qua
       MISS -> EngineLoader::Scan
             -> EngineScanFile
             -> metadata + entropy + rule demo
             -> callback progress/result
  -> Service gui event ve Client
```

## Engine DLL API

- `EngineInitialize(configJson)`
- `EngineScanFile(path, options, callback, context)`
- `EngineGetVersion()`
- `EngineShutdown()`

Rule demo:

- File ngoai o `C:\` -> +2 diem.
- Extension `.exe .dll .sys .js .vbs .ps1` -> +1.
- File > 50 MB -> +1.
- Entropy > nguong -> +1.

Verdict demo:

- 0-1: SAFE
- 2-3: SUSPICIOUS
- >=4: MALICIOUS

## Cache

Cache chi nam trong RAM, thread-safe, TTL 10 phut.
Key:

```text
normalizedPath + lastWriteTime + fileSize
```

File thay doi thi key thay doi va scan lai.

## Build

Mo `ScanEngineSolution.sln` bang Visual Studio, chon `x64` va Build Solution.
Output duoc dat trong `x64\Debug` hoac `x64\Release`.

## Chay demo

Terminal 1:

```bat
ScanService.exe --console
```

Terminal 2:

```bat
client.exe scan "D:\test.exe" --priority high
client.exe query <jobId>
client.exe cancel <jobId>
client.exe telemetry
client.exe stress "D:\test.exe" --count 20
```

## Da luoc bo khoi ban nay

Ban nay khong con cac module nang cap Week 5: backpressure/FLOW_CONTROL, reconnect/resume,
ACL/impersonation/policy/rate-limit, persistent/versioned cache va PE parser/rule A-E/Authenticode.
