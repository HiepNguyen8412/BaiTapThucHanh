# Week 5 - Noi dung da nang cap

Ban nay giu nguyen main flow cua bai cu:

```text
Client -> Named Pipe -> ClientSession -> JobManager -> Worker -> Cache -> Engine -> Result -> Client
```

va chen them cac lop xu ly Week 5 vao dung diem can thiet.

## 1. Protocol / Named Pipe

- `Common/Protocol/Crc32.*`: CRC32 cho payload.
- `Protocol::ReadExact/WriteExact`: tiep tuc xu ly partial read/write.
- Header v2: magic + version + type + payloadSize + requestId + checksum.
- Length framing tach duoc cac goi bi dinh nhau (sticky packets).
- Standardized `ServiceErrorCode` cho protocol/auth/policy/resume/job/internal.

## 2. Backpressure + Resume

- `SessionState` la logical session, khong bi huy ngay khi physical pipe dut.
- `EventQueue`: soft limit 256; verbose co the drop, critical khong drop.
- `FLOW_CONTROL`: bao droppedVerbose + queueDepth.
- `EventHistory`: giu toi da 1024 event gan nhat.
- `SessionManager`: resume window 10 giay.
- Client gui `RESUME {sessionId,lastEventSeq}` va service replay event con thieu.

## 3. Pipe Security + Policy

- Named Pipe ACL: SYSTEM/Admin + Interactive User; runtime tiep tuc xac minh token that.
- `GetNamedPipeClientProcessId` + impersonation + thread token + SID + TokenSessionId.
- Doi chieu token cua pipe va token cua process PID.
- `PolicyManager`: resolve path va deny `Windows\\System32`.
- `RateLimiter`: token bucket 10 SCAN/s, burst 20 theo client principal.

## 4. Persistent Cache

- Cache van co fast path tren RAM.
- `ScanCache.dat` luu ket qua qua service restart, TTL mac dinh 7 ngay.
- Cache key them `engineVersion + ruleSetVersion + cacheSchemaVersion`.
- Update engine/rule/schema => key cu tu dong MISS, khong tai su dung ket qua stale.

## 5. PE Reader / Analyzer

`ScanEngine/PE` gom:

- `PeReader`: tu parse DOS -> NT -> Optional -> DataDirectory -> Section Table.
- PE32 (`0x10B`) va PE32+ (`0x20B`).
- `RvaToFileOffset()` tu viet, co range/overflow check.
- Truncated/bad core header => `MALFORMED_PE`.
- Section/directory/RVA vuot file => `STRUCT_CORRUPT`.
- PE metadata: machine, subsystem, DLL/driver/.NET/signed/debug/RichHeader, entry point, image base, section count, overlay.
- `PeAnalyzer`: section entropy/WX/name, imports/exports/TLS/delay import/resources/signature.
- `Authenticode`: `WinVerifyTrust` -> UNSIGNED / SIGNED_VALID / SIGNED_INVALID.
- `PeRuleEvaluator`: nhom A-E va score SAFE/SUSPICIOUS/MALICIOUS.

Luu y: phan resource va certificate anomaly la heuristic demo. `WinVerifyTrust` da verify chu ky; rule rieng cho timestamp/certificate-chain bat thuong cua mot chu ky van-valid chua duoc mo rong sau ket qua trust cua Windows.

## 6. Client

- Nho `sessionId` va `lastEventSeq` khi scan.
- Pipe dut: reconnect va RESUME trong 10 giay.
- Hien thi `FLOW_CONTROL`.
- Hien thi PE metadata khi co result.

## 7. Tai lieu luong

Xem `docs/WEEK5_FLOW.md`:

1. Luong tong quat.
2. Luong chi tiet.
3. Luong chinh.
