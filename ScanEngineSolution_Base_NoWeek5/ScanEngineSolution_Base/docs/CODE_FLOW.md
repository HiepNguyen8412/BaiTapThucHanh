# CODE FLOW - BAN CO BAN

## 1. Khoi dong

`ServiceApp::Start()`
-> load `ScanEngine.dll`
-> start `ThrottleMonitor`
-> start `JobManager` worker pool
-> start `PipeServer`.

## 2. Client ket noi

Client tao ket noi den `\\.\pipe\AvScanPipe`.
`ClientSession::Run()` nhan `HELLO`, tra `WELCOME`, sau do lap nhan message.

## 3. SCAN

`HandleScan()`
-> doc `path/priority/timeout`
-> `JobManager::Submit()`
-> tao `ScanJob + jobId`
-> push vao priority queue.

## 4. Worker

`WorkerLoop()`
-> cho job bang `condition_variable`
-> lay job priority cao nhat
-> cancel check
-> throttle check
-> cache lookup
-> neu MISS thi `EngineLoader::Scan()`.

## 5. Engine

`EngineScanFile()`
-> normalize path
-> metadata
-> entropy
-> rule demo
-> build `EngineScanResultV1`
-> callback progress/result.

## 6. Tra ket qua

`JobManager::EngineCallback()`
-> cap nhat `ScanJob`
-> `IJobEventSink::SendJobEvent()`
-> `ClientSession::SendSnapshot()`
-> Named Pipe
-> Client hien thi.
