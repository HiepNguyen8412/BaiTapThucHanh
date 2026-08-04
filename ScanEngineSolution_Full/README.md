# ScanEngineSolution – Full assignment

Solution C++17/Visual Studio 2022 gồm 4 project:

- `ScanEngine` – DLL được Service nạp động bằng `LoadLibraryW`/`GetProcAddress`.
- `ScanService` – Windows Service + chế độ console để debug.
- `ScanClient` – CLI giao tiếp qua Named Pipe `\\.\pipe\AvScanPipe`.
- `ScanEngineTest` – test trực tiếp DLL mà không qua Service.

Toàn bộ output được đặt chung tại:

```text
x64\Debug\
```

nên `ScanService.exe`, `client.exe`, `ScanEngineTest.exe` và `ScanEngine.dll` tự nằm cạnh nhau.

## 1. Chức năng đã cài đặt

### Engine DLL

API version 1:

```cpp
EngineInitialize(configJson)
EngineScanFile(path, options, callback, userContext)
EngineGetVersion()
EngineShutdown()
```

Rule demo:

| Rule | Điểm |
|---|---:|
| File ngoài `C:\` | +2 |
| Extension `.exe .dll .sys .js .vbs .ps1` | +1 |
| File lớn hơn 50 MiB | +1 |
| Entropy lớn hơn ngưỡng | +1 |

Mapping:

```text
0–1 -> SAFE
2–3 -> SUSPICIOUS
4–5 -> MALICIOUS
```

Engine đọc tối đa 1 MiB đầu file để tính Shannon entropy, report các stage và hỗ trợ hủy bằng việc callback trả `FALSE`.

### Service

- Named Pipe duplex: `\\.\pipe\AvScanPipe`.
- Binary TLV protocol có message header và field TLV.
- HELLO/WELCOME handshake.
- SCAN/QUERY/CANCEL.
- Priority queue: low/normal/high.
- Worker thread pool: 2–8 worker tùy số logical CPU.
- Thread-safe cache key: `normalizedPath + lastWriteTime + fileSize`.
- Cache TTL: 10 phút.
- FAST PATH khi cache hit.
- Throttle state: `IDLE / BUSY / OVERLOADED`.
- Khi `OVERLOADED`, chỉ job high priority chạy; job còn lại nhận event `DELAYED`.
- CPU: `GetSystemTimes`.
- RAM: `GlobalMemoryStatusEx`.
- Disk activity demo: I/O rate của tiến trình Service bằng `GetProcessIoCounters` (đề cho phép dùng I/O rate thay disk queue length).
- Telemetry: received/success/failed/cancelled/cache hit/pending/running/average/p95.
- Log file: `x64\Debug\ScanService.log`.
- Chạy được dưới SCM như Windows Service hoặc `--console` để debug.

### Client

```text
client.exe scan "D:\a.exe" --priority high --timeout 30000
client.exe query <jobId>
client.exe cancel <jobId>
client.exe telemetry
client.exe stress "D:\a.exe" --count 20 --priority low
```

Client giữ kết nối khi scan để nhận progress/event streaming.

## 2. Build

1. Mở `ScanEngineSolution.sln` bằng Visual Studio 2022.
2. Chọn `Debug | x64`.
3. `Build -> Rebuild Solution`.

Kết quả:

```text
x64\Debug\ScanEngine.dll
x64\Debug\ScanService.exe
x64\Debug\client.exe
x64\Debug\ScanEngineTest.exe
```

## 3. Test Engine DLL riêng

Mở terminal tại `x64\Debug`:

```cmd
ScanEngineTest.exe "C:\Windows\System32\notepad.exe"
```

Kết quả sẽ có stage 0–100%, score, flags, entropy và size.

## 4. Chạy Service ở console mode

Terminal 1:

```cmd
cd /d <duong-dan-solution>\x64\Debug
ScanService.exe --console
```

Terminal 2:

```cmd
cd /d <duong-dan-solution>\x64\Debug
client.exe scan "C:\Windows\System32\notepad.exe" --priority high
```

Console mode dễ đặt breakpoint và chưa cần quyền Administrator.

## 5. Test cache

Chạy cùng một file hai lần liên tiếp:

```cmd
client.exe scan "C:\Windows\System32\notepad.exe" --priority high
client.exe scan "C:\Windows\System32\notepad.exe" --priority high
```

Lần hai phải hiện:

```text
FAST PATH: cache hit [CACHE HIT]
```

Nếu file thay đổi size hoặc last-write-time, cache key đổi và Engine scan lại.

## 6. Test 20 job

```cmd
client.exe stress "C:\Windows\System32\notepad.exe" --count 20 --priority low
client.exe telemetry
```

Để dễ quan sát queue hơn, dùng một file lớn hoặc nhiều đường dẫn khác nhau. Khi máy đạt trạng thái overloaded, low/normal job nhận `DELAYED`, còn high job vẫn được chạy.

## 7. Test query/cancel

Lệnh `scan` in `jobId` ngay khi Service ACK. Mở terminal khác:

```cmd
client.exe query 1
client.exe cancel 1
```

Để cancel dễ quan sát, scan file lớn và đặt `maxEntropyBytes` lớn hơn trong `JobManager.cpp`, hoặc đặt breakpoint/sleep trong vòng đọc file khi demo.

## 8. Cài thành Windows Service

Mở CMD/Terminal **Run as Administrator** tại `x64\Debug`:

```cmd
ScanService.exe install
sc start AvScanService
```

Kiểm tra:

```cmd
sc query AvScanService
client.exe telemetry
```

Dừng và xóa:

```cmd
sc stop AvScanService
ScanService.exe uninstall
```

`ScanEngine.dll` phải nằm cùng thư mục với `ScanService.exe`, vì Service nạp DLL từ thư mục executable.

## 9. Protocol TLV

Message header:

```cpp
struct MessageHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t payloadSize;
    uint64_t requestId;
};
```

Mỗi field:

```cpp
struct TlvHeader
{
    uint16_t type;
    uint16_t reserved;
    uint32_t length;
};
```

Các message chính:

```text
HELLO -> WELCOME
SCAN  -> ACK(jobId) -> EVENT(progress...) -> EVENT(result)
QUERY -> EVENT(snapshot)
CANCEL -> ACK
TELEMETRY -> TELEMETRY(snapshot)
```

## 10. Module và trách nhiệm

```text
Client
  -> chỉ gửi command và hiển thị event

Service
  -> PipeServer: session/protocol/streaming
  -> JobManager: queue/priority/cancel/workers
  -> ResultCache: fast path + TTL
  -> ThrottleMonitor: IDLE/BUSY/OVERLOADED
  -> Telemetry + Logger
  -> EngineLoader: dynamic loading

Engine DLL
  -> file metadata
  -> entropy
  -> rule scoring
  -> callback progress/result
```

## 11. Lưu ý học thuật

Rule trong bài chỉ để demo kiến trúc scan engine. `.exe`, file lớn hoặc entropy cao không tự động chứng minh malware. Hệ thống thật cần signature, parser định dạng, behavior, reputation, sandbox và nhiều nguồn tín hiệu khác.

## 12. Hạn chế có chủ đích của bản demo

- Protocol dùng binary little-endian cho hệ Windows cùng kiến trúc.
- Job history đang giữ trong RAM đến khi Service dừng; bản production nên có retention cleanup/persistence.
- I/O throttle đo I/O rate của Service, không dùng Performance Counter `PhysicalDisk\Current Disk Queue Length` để tránh thêm dependency PDH.
- `EngineInitialize` có parser số đơn giản cho ba field config; đây không phải JSON parser tổng quát.
