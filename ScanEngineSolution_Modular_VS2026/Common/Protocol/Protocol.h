// ============================================================================
// MODULE : Common / Protocol
// ROLE   : Dinh nghia Named Pipe protocol, MessageHeader va TLV fields.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace AvProtocol
{
    constexpr wchar_t PIPE_NAME[] = L"\\\\.\\pipe\\AvScanPipe";
    constexpr std::uint32_t MAGIC = 0x50535641u; // 'AVSP' little-endian
    constexpr std::uint16_t VERSION = 1;
    constexpr std::uint32_t MAX_PAYLOAD_SIZE = 4u * 1024u * 1024u;

#pragma pack(push, 1)
    struct MessageHeader
    {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t type;
        std::uint32_t payloadSize;
        std::uint64_t requestId;
    };

    struct TlvHeader
    {
        std::uint16_t type;
        std::uint16_t reserved;
        std::uint32_t length;
    };
#pragma pack(pop)

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
        Telemetry = 9
    };

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
        P95Ms = 34
    };

    struct Message
    {
        MessageHeader header{};
        std::vector<std::uint8_t> payload;
    };

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

    class TlvReader
    {
    public:
        explicit TlvReader(const std::vector<std::uint8_t>& payload);
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
