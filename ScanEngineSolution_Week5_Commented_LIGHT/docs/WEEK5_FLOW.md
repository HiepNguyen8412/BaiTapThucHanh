# WEEK 5 - SCAN ENGINE SERVICE FLOW

Tai lieu nay tach 3 loai luong de tranh bi trung y:

- **Luong tong quat**: nhin kien truc toan he thong.
- **Luong chi tiet**: di tu luc Service start den scan, PE, event, reconnect.
- **Luong chinh**: duong di quan trong nhat cua mot lenh SCAN.

---

# 1. LUONG TONG QUAT CUA BAI

```text
ScanClient
   |
   | HELLO / SCAN / QUERY / CANCEL / RESUME
   v
Common/Protocol
   |-- Magic + Version + Length
   |-- TLV
   `-- CRC32
   |
   v
PipeServer
   |-- Named Pipe ACL
   `-- Accept Client
   |
   v
ClientSession  <---------------- physical pipe connection
   |
   |-- PipeSecurity: PID + token + SID + SessionId + group
   |-- PolicyManager: path allow/deny
   `-- RateLimiter: anti-spam per clientId
   |
   v
SessionManager / SessionState  <--- logical session survives disconnect 10s
   |-- sessionId
   |-- eventSeq
   |-- EventHistory
   `-- EventQueue + Backpressure
   |
   v
JobManager
   |-- JobId
   |-- Priority Queue
   |-- Worker Pool
   `-- Cancel
   |
   v
Throttle
   |-- CPU / RAM / Disk
   |
   v
ResultCache
   |-- L1 memory
   `-- L2 ScanCache.dat (persistent, default TTL 7 days)
         key = path + mtime + size + engineVersion + ruleVersion + schemaVersion
   |
   | HIT -------------------------------> Result
   |
   ` MISS
      v
EngineLoader
   |
   v
ScanEngine.dll
   |
   v
EngineScanFile
   |
   +--> FileAnalyzer (metadata + generic entropy)
   |
   +--> PeReader
   |      DOS -> NT -> Optional -> DataDirectory -> Sections
   |      RvaToFileOffset()
   |
   +--> PeAnalyzer
   |      Imports / Exports / TLS / Delay Import / Resource / Signature
   |
   `--> PeRuleEvaluator
          SAFE / SUSPICIOUS / MALICIOUS
   |
   v
Engine Result
   |
   v
SessionState::SendJobEvent
   |-- assign EventSeq
   |-- save EventHistory
   `-- outbound EventQueue
         |-- verbose may drop when full
         |-- critical never drops
         `-- FLOW_CONTROL
   |
   v
Client
```

---

# 2. LUONG CHI TIET CUA BAI

## 2.1 Service khoi dong

```text
main / ServiceMain
      |
      v
ServiceApp::Start()
      |
      +--> Logger::Open()
      |
      +--> EngineLoader::Load(ScanEngine.dll)
      |       |-- LoadLibrary
      |       |-- GetProcAddress
      |       |-- EngineInitialize
      |       `-- EngineGetVersion
      |
      +--> ResultCache::ConfigurePersistent(ScanCache.dat, EngineVersion)
      |
      +--> SessionManager::Start()
      |
      +--> ThrottleMonitor::Start()
      |
      +--> JobManager::Start(workerCount)
      |       `-- tao worker pool 2..8 threads
      |
      `--> PipeServer::Start()
              `-- AcceptLoop thread
```

## 2.2 Client ket noi + handshake

```text
ScanClient::OpenSession()
      |
      v
ConnectPipe()
      |
      v
HELLO { clientId, pid, user, version }
      |
      v
Protocol::ReadMessage()
      |-- ReadExact(header)       <- partial read
      |-- validate magic/version/length
      |-- ReadExact(payload)      <- sticky packet khong lam lan goi sau
      `-- CRC32(payload)
              |
              +-- FAIL -> PROTOCOL_CHECKSUM_FAILED
              |
              `-- OK
                    v
ClientSession::PerformHandshake()
      |
      v
PipeSecurity::Authenticate()
      |-- GetNamedPipeClientProcessId
      |-- claimedPid == realPid
      |-- ImpersonateNamedPipeClient
      |-- OpenThreadToken
      |-- SID / TokenSessionId / group
      |-- compare process token and pipe token
      `-- RevertToSelf
              |
              +-- FAIL -> AUTH_xxx
              |
              `-- OK
                    v
SessionManager::Create()
      |
      v
WELCOME { sessionId }
```

## 2.3 Client gui SCAN

```text
ScanCommand()
   |
   v
WriteMessage(SCAN)
   |
   v
ClientSession::HandleScan()
   |
   +--> parse Path / Priority / Timeout
   |
   +--> PolicyManager::CanScan()
   |       `-- Windows\\System32 -> POLICY_PATH_DENIED
   |
   +--> RateLimiter::Allow(clientId)
   |       `-- too fast -> RATE_LIMITED + retryAfterMs
   |
   `--> JobManager::Submit(..., SessionState)
            |
            +--> generate JobId
            +--> generate FIFO sequence
            +--> jobs_[jobId]
            `--> Priority Queue
```

## 2.4 Worker xu ly

```text
WorkerLoop()
   |
   +--> pop Priority Queue
   |
   +--> Cancel?
   |       `-- yes -> CANCELLED
   |
   +--> Machine overloaded?
   |       `-- non-high job -> DELAYED -> push queue lai
   |
   +--> NormalizePath + GetFileIdentity
   |
   +--> Build CacheKey
   |       path
   |       lastWriteTime
   |       fileSize
   |       engineVersion
   |       ruleSetVersion
   |       cacheSchemaVersion
   |
   +--> ResultCache::TryGet()
   |       |
   |       +-- HIT -> COMPLETE FAST PATH
   |       |
   |       `-- MISS
   |             v
   `--> EngineLoader::Scan()
              |
              v
         EngineScanFile()
```

## 2.5 Engine + PE

```text
EngineScanFile()
   |
   +--> NormalizeFilePath
   +--> ReadFileMetadata
   +--> CalculateFileEntropy
   |
   +--> PeReader::Open()
   |       |
   |       +-- no MZ -> NotPe -> generic rules continue
   |       +-- truncated/bad header -> MALFORMED_PE
   |       `-- section/directory/RVA outside file -> STRUCT_CORRUPT
   |
   +--> DOS Header
   +--> NT Signature + File Header
   +--> Optional Header
   |       +-- 0x10B -> PE32
   |       `-- 0x20B -> PE32+
   +--> DataDirectory
   +--> Section Table
   +--> RvaToFileOffset()
   |
   +--> PeAnalyzer
   |       +-- Sections + entropy + WX + overlay
   |       +-- Imports risky groups
   |       +-- Exports
   |       +-- TLS callbacks
   |       +-- Delay Import
   |       +-- Resources / VersionInfo / CompanyName
   |       `-- Authenticode / WinVerifyTrust
   |
   `--> PeRuleEvaluator
           Group A Header
           Group B Sections
           Group C Import/Export/TLS
           Group D Resources
           Group E Signature
                    |
                    v
           Risk Score
           0..2  SAFE
           3..6  SUSPICIOUS
           >=7   MALICIOUS
```

## 2.6 Ket qua + backpressure

```text
EngineCallback / final Result
      |
      v
JobManager::Notify()
      |
      v
SessionState::SendJobEvent()
      |
      +--> classify
      |      Progress/Delayed = VERBOSE
      |      Completed/Failed/Cancelled = CRITICAL
      |
      +--> outbound queue < 256 ?
      |       `-- yes -> enqueue
      |
      `--> queue full
              |
              +-- VERBOSE -> DROP + droppedVerbose++
              |               `-- FLOW_CONTROL event
              |
              `-- CRITICAL -> never drop
                               remove old verbose if possible,
                               otherwise temporarily exceed soft limit
      |
      v
ClientSession::RunEventLoop()
      |
      v
WriteMessage(EVENT/FLOW_CONTROL)
      |
      v
Client
```

## 2.7 Reconnect + Resume

```text
Pipe broken
   |
   v
ClientSession physical connection stops
   |
   v
SessionManager::MarkDisconnected(sessionId)
   |
   +--> SessionState remains for 10 seconds
   +--> Job continues running
   `--> EventHistory continues storing events

Client:
   |
   +--> remember sessionId
   `--> remember lastEventSeq
   |
   v
Reconnect (deadline 10s)
   |
   v
RESUME { sessionId, lastEventSeq, pid, user, clientId }
   |
   v
PipeSecurity::Authenticate()
   |
   v
SessionManager::Resume()
   |-- session exists?
   |-- not expired?
   |-- same SID/session/clientId?
   `-- EventHistory::GetAfter(lastEventSeq)
             |
             v
       rebuild outbound queue
             |
             v
       replay missing events
             |
             v
       continue live streaming
```

---

# 3. LUONG CHINH CUA BAI

Day la luong nen dung khi duoc hoi: **"Mot lenh scan chay nhu the nao?"**

```text
ServiceApp::Start()
      |
      v
Engine + Cache + SessionManager + WorkerPool + PipeServer
      |
      v
Client connect + HELLO
      |
      v
Handshake / authenticate
      |
      v
Client gui SCAN
      |
      v
HandleScan()
      |
      v
Policy + RateLimit
      |
      v
JobManager::Submit()
      |
      v
Priority Queue
      |
      v
WorkerLoop()
      |
      +--> Cancel
      +--> Throttle
      |
      v
Cache
  /       \
HIT       MISS
 |          |
 |          v
 |       EngineLoader::Scan()
 |          |
 |          v
 |       EngineScanFile()
 |          |
 |          v
 |       PeReader
 |          |
 |          v
 |       PeAnalyzer
 |          |
 |          v
 |       PeRuleEvaluator
 |          |
 |          v
 |      SAFE / SUSPICIOUS / MALICIOUS
 |          |
 `------> Result
            |
            v
        Cache::Put()
            |
            v
        SessionState
            |
            v
        EventQueue
            |
            v
       Backpressure
            |
            v
          Client
```

## Mot cau de nho

**Luong cu van la Client -> Pipe -> JobManager -> Worker -> Cache -> Engine -> Result -> Client.**

Week 5 chi chen them cac lop xu ly:

```text
Protocol CRC32
Security
Policy / RateLimit
Session / Resume
Backpressure
Persistent versioned cache
PE Reader / Analyzer / Rules
```

nen ban chat he thong khong bi viet lai.
