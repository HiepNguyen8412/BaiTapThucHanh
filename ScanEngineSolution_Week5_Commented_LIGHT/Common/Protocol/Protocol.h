// ============================================================================
// MODULE : Common / Protocol
// ROLE   : Named Pipe framing + TLV + CRC32. Shared by Service and Client.
// WEEK 5 : partial read, sticky packets, checksum, RESUME and FLOW_CONTROL.
// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

//Framing TLV: magic + version + length + checksum
namespace AvProtocol
{
    constexpr wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\AvScanPipe";
    constexpr std::uint32_t MAGIC = 0x50535641u; // 'AVSP' little-endian
    constexpr std::uint16_t VERSION = 2;
    constexpr std::uint32_t MAX_PAYLOAD_SIZE = 4u * 1024u * 1024u;

//Tổng kích thước logic của header
#pragma pack(push, 1)
    // Header co dinh cua moi frame. payloadSize la length-prefix; checksum bao ve phan payload.
    struct MessageHeader
    {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t type;
        std::uint32_t payloadSize;
        std::uint64_t requestId;
        std::uint32_t checksum; // CRC32(payload). CRC32(empty) = 0.
    };

    // Moi field trong payload co header rieng: type + length, sau do moi den bytes cua value.
    struct TlvHeader
    {
        std::uint16_t type;
        std::uint16_t reserved;
        std::uint32_t length;
    };

#pragma pack(pop)

    // MessageType xac dinh y nghia cua ca frame; HandleMessage dung truong nay de dispatch request.
    enum class MessageType : std::uint16_t
    {
        Hello = 1,
        Welcome = 2,
        Scan = 3,
        Query = 4,
        Cancel = 5,
        Ack = 6,
        Event = 7,
        Error = 8,
        Telemetry = 9,
        Resume = 10,
        FlowControl = 11
    };

    // FieldType xac dinh y nghia tung TLV trong payload. Reader co the bo qua field khong can dung.
    enum class FieldType : std::uint16_t
    {
        ClientId = 1,
        Pid = 2,
        User = 3,
        Version = 4,
        SessionId = 5,
        ServerVersion = 6,
        Policy = 7,
        Path = 8,
        Priority = 9,
        TimeoutMs = 10,
        JobId = 11,
        JobStatus = 12,
        Progress = 13,
        Stage = 14,
        Message = 15,
        Verdict = 16,
        RiskScore = 17,
        MatchedRules = 18,
        FileSize = 19,
        LastWriteTime = 20,
        Entropy = 21,
        DurationMs = 22,
        CacheHit = 23,
        Win32Error = 24,
        ErrorCode = 25,
        TotalReceived = 26,
        TotalSucceeded = 27,
        TotalFailed = 28,
        TotalCancelled = 29,
        CacheHits = 30,
        Pending = 31,
        Running = 32,
        AverageMs = 33,
        P95Ms = 34,

        // Week 5 session/event flow.
        EventSeq = 35,
        LastEventSeq = 36,
        DroppedEvents = 37,
        QueueDepth = 38,
        RetryAfterMs = 39,

        // Week 5 PE result fields.
        Machine = 40,
        Subsystem = 41,
        IsPe = 42,
        IsPe32Plus = 43,
        IsDll = 44,
        IsDriver = 45,
        IsManaged = 46,
        IsSigned = 47,
        SignatureStatus = 48,
        HasDebug = 49,
        HasRichHeader = 50,
        EntryPointRva = 51,
        ImageBase = 52,
        SectionCount = 53,
        OverlaySize = 54
    };

    // Stable protocol/service errors. These are not raw GetLastError values.
    enum class ServiceErrorCode : std::uint32_t
    {
        Ok = 0,
        ProtocolBadMagic = 1001,
        ProtocolBadVersion = 1002,
        ProtocolFrameTooLarge = 1003,
        ProtocolChecksumFailed = 1004,
        ProtocolMalformedTlv = 1005,
        ProtocolUnexpectedMessage = 1006,

        AuthPidMismatch = 2001,
        AuthTokenFailed = 2002,
        AuthUserMismatch = 2003,
        AuthGroupDenied = 2004,
        AuthSessionMismatch = 2005,

        PolicyPathDenied = 3001,
        RateLimited = 3002,

        ResumeNotFound = 4001,
        ResumeExpired = 4002,
        ResumeIdentityMismatch = 4003,
        ResumeAlreadyConnected = 4004,

        JobNotFound = 5001,
        InvalidRequest = 5002,
        InternalError = 9000
    };

    struct Message
    {
        MessageHeader header{};
        std::vector<std::uint8_t> payload;
    };

    // TlvWriter dung de serialize payload; moi Add... them mot field TLV vao vector byte.
    class TlvWriter
    {
    public:
        void AddBytes(FieldType type, const void* data, std::uint32_t length);
        void AddU32(FieldType type, std::uint32_t value);
        void AddU64(FieldType type, std::uint64_t value);
        void AddDouble(FieldType type, double value);
        void AddBool(FieldType type, bool value);
        void AddUtf8(FieldType type, const std::string& value);
        void AddWide(FieldType type, const std::wstring& value);
        const std::vector<std::uint8_t>& Data() const noexcept { return data_; }

    private:
        std::vector<std::uint8_t> data_;
    };

    // TlvReader validate payload va cung cap Get... theo FieldType de deserialize an toan.
    class TlvReader
    {
    public:
        explicit TlvReader(const std::vector<std::uint8_t>& payload);
        bool IsValid() const noexcept { return valid_; }
        bool GetU32(FieldType type, std::uint32_t& value) const;
        bool GetU64(FieldType type, std::uint64_t& value) const;
        bool GetDouble(FieldType type, double& value) const;
        bool GetBool(FieldType type, bool& value) const;
        bool GetUtf8(FieldType type, std::string& value) const;
        bool GetWide(FieldType type, std::wstring& value) const;

    private:
        struct FieldView
        {
            FieldType type;
            const std::uint8_t* data;
            std::uint32_t length;
        };
        bool valid_{true};
        std::vector<FieldView> fields_;
        const FieldView* Find(FieldType type) const;
    };

    bool ReadExact(HANDLE handle, void* buffer, DWORD bytes, DWORD& errorCode);
    bool WriteExact(HANDLE handle, const void* buffer, DWORD bytes, DWORD& errorCode);
    bool ReadMessage(HANDLE pipe, Message& message, DWORD& errorCode);
    bool WriteMessage(
        HANDLE pipe,
        MessageType type,
        std::uint64_t requestId,
        const TlvWriter& payload,
        DWORD& errorCode);
}
