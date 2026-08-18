# CODE FLOW - dung de thuyet trinh

## A. Service start

`ScanService/Startup/main.cpp::wmain`
-> `RunConsole` / `ServiceMain`
-> `ServiceApp::Start`
-> `EngineLoader::Load`
-> `ThrottleMonitor::Start`
-> `JobManager::Start` (tao 2..8 worker)
-> `PipeServer::Start` (tao accept thread)
-> `TelemetryLoop`

## B. Scan request

`ScanClient/App/main.cpp::ScanCommand`
-> `OpenSession`
-> `CreateFileW(\\.\pipe\AvScanPipe)`
-> HELLO/WELCOME
-> `TlvWriter` + `WriteMessage(SCAN)`

Service:
`ClientSession::Run`
-> `ReadMessage`
-> `HandleMessage`
-> `HandleScan`
-> `JobManager::Submit`

## C. Job scheduler (CORE)

`Submit` **chay tren ClientSession thread**:
1. `make_shared<ScanJob>`
2. `nextJobId_++`
3. `nextSequence_++`
4. `jobs_[id] = job` (de Query/Cancel)
5. `queue_.push(job)` (de worker xu ly)
6. `queueCv_.notify_one()`
7. return `jobId` cho `HandleScan` -> ACK Client

`QueueCompare`:
- High > Normal > Low
- cung priority: sequence nho hon truoc

## D. WorkerLoop

Worker wake -> `queue_.top/pop`
-> cancelRequested?
-> throttle Overloaded? (Normal/Low delayed)
-> state Running
-> `WinUtil::NormalizePath`
-> `WinUtil::GetFileIdentity`
-> `CacheKey{normalizedPath,lastWriteTime,fileSize}`
-> `ResultCache::TryGet`

Cache HIT:
-> copy cached result
-> Completed 100%
-> Notify
-> worker quay lai queue

Cache MISS:
-> tao `EngineScanOptionsV1`
-> tao `CallbackContext{JobManager*, ScanJob}`
-> `EngineLoader::Scan`
-> function pointer `scanFile_`
-> DLL `EngineScanFile`

## E. Engine DLL

`EngineScanFile`
-> validate API/options
-> callback 0%
-> `NormalizeFilePath`
-> callback 5%
-> `ReadFileMetadata`
-> callback 15%
-> `CalculateFileEntropy`
   -> `CreateFileW`
   -> `GetFileSizeEx`
   -> buffer 64 KiB
   -> `ReadFile` loop
   -> frequencies[256]
   -> callback progress + cooperative cancel
   -> Shannon entropy
-> callback 85%
-> rule scoring
-> callback 95%
-> `EngineScanResultV1`
-> callback Result 100%
-> return Success/Cancelled/Error

## F. Callback -> Client

`JobManager::EngineCallback` **van tren worker thread**
-> `userContext` -> dung ScanJob
-> check `cancelRequested`
-> update progress/stage/message
-> copy result neu co
-> `Notify`
-> `ClientSession::SendJobEvent`
-> push `eventQueue_`
-> return TRUE/FALSE ve DLL

`ClientSession::RunEventLoop` (event thread)
-> pop snapshot
-> `SendSnapshot`
-> `WriteMessage`
-> `WriteFile`
-> Client `ReadMessage`
-> `PrintJobMessage`
