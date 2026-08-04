#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../Common/Protocol.h"
#include "../Common/WinUtil.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::atomic_uint64_t g_requestId{1};
    std::mutex g_consoleMutex;

    const wchar_t* JobStateText(std::uint32_t state)
    {
        switch (state)
        {
        case 0: return L"PENDING";
        case 1: return L"DELAYED";
        case 2: return L"RUNNING";
        case 3: return L"COMPLETED";
        case 4: return L"FAILED";
        case 5: return L"CANCELLED";
        default: return L"UNKNOWN";
        }
    }

    const wchar_t* VerdictText(std::uint32_t verdict)
    {
        switch (verdict)
        {
        case 0: return L"SAFE";
        case 1: return L"SUSPICIOUS";
        case 2: return L"MALICIOUS";
        default: return L"UNKNOWN";
        }
    }

    std::uint32_t ParsePriority(const std::wstring& text)
    {
        if (_wcsicmp(text.c_str(), L"high") == 0) return 2;
        if (_wcsicmp(text.c_str(), L"low") == 0) return 0;
        return 1;
    }

    HANDLE ConnectPipe()
    {
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            HANDLE pipe = CreateFileW(
                AvProtocol::PIPE_NAME,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);
            if (pipe != INVALID_HANDLE_VALUE) return pipe;
            if (GetLastError() != ERROR_PIPE_BUSY) return INVALID_HANDLE_VALUE;
            WaitNamedPipeW(AvProtocol::PIPE_NAME, 2000);
        }
        return INVALID_HANDLE_VALUE;
    }

    bool SendHello(HANDLE pipe)
    {
        AvProtocol::TlvWriter writer;
        writer.AddUtf8(AvProtocol::FieldType::ClientId, "ScanClient");
        writer.AddU32(AvProtocol::FieldType::Pid, GetCurrentProcessId());
        writer.AddWide(AvProtocol::FieldType::User, WinUtil::GetCurrentUserNameString());
        writer.AddUtf8(AvProtocol::FieldType::Version, "1.0.0");
        DWORD error = ERROR_SUCCESS;
        const auto requestId = g_requestId++;
        if (!AvProtocol::WriteMessage(
                pipe,
                AvProtocol::MessageType::Hello,
                requestId,
                writer,
                error))
        {
            return false;
        }
        AvProtocol::Message response;
        if (!AvProtocol::ReadMessage(pipe, response, error)) return false;
        return static_cast<AvProtocol::MessageType>(response.header.type) ==
            AvProtocol::MessageType::Welcome;
    }

    bool OpenSession(HANDLE& pipe)
    {
        pipe = ConnectPipe();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            std::lock_guard lock(g_consoleMutex);
            std::wcerr << L"Cannot connect to " << AvProtocol::PIPE_NAME
                << L". Start ScanService first. Error=" << GetLastError() << L'\n';
            return false;
        }
        if (!SendHello(pipe))
        {
            std::lock_guard lock(g_consoleMutex);
            std::wcerr << L"HELLO/WELCOME handshake failed.\n";
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            return false;
        }
        return true;
    }

    void PrintError(const AvProtocol::Message& message)
    {
        AvProtocol::TlvReader reader(message.payload);
        std::uint32_t code = 0;
        std::wstring text;
        reader.GetU32(AvProtocol::FieldType::ErrorCode, code);
        reader.GetWide(AvProtocol::FieldType::Message, text);
        std::lock_guard lock(g_consoleMutex);
        std::wcerr << L"[ERROR] code=" << code << L" " << text << L'\n';
    }

    std::uint32_t PrintJobMessage(const AvProtocol::Message& message, bool compact)
    {
        AvProtocol::TlvReader reader(message.payload);
        std::uint64_t jobId = 0;
        std::uint32_t state = 0;
        std::uint32_t progress = 0;
        std::uint32_t stage = 0;
        std::wstring text;
        bool cacheHit = false;
        reader.GetU64(AvProtocol::FieldType::JobId, jobId);
        reader.GetU32(AvProtocol::FieldType::JobStatus, state);
        reader.GetU32(AvProtocol::FieldType::Progress, progress);
        reader.GetU32(AvProtocol::FieldType::Stage, stage);
        reader.GetWide(AvProtocol::FieldType::Message, text);
        reader.GetBool(AvProtocol::FieldType::CacheHit, cacheHit);

        std::uint32_t verdict = 0;
        std::uint32_t riskScore = 0;
        std::uint32_t rules = 0;
        std::uint64_t fileSize = 0;
        std::uint64_t duration = 0;
        double entropy = 0.0;
        const bool hasVerdict = reader.GetU32(AvProtocol::FieldType::Verdict, verdict);
        reader.GetU32(AvProtocol::FieldType::RiskScore, riskScore);
        reader.GetU32(AvProtocol::FieldType::MatchedRules, rules);
        reader.GetU64(AvProtocol::FieldType::FileSize, fileSize);
        reader.GetU64(AvProtocol::FieldType::DurationMs, duration);
        reader.GetDouble(AvProtocol::FieldType::Entropy, entropy);

        std::lock_guard lock(g_consoleMutex);
        if (compact)
        {
            if (state == 3 || state == 4 || state == 5)
            {
                std::wcout << L"job=" << jobId << L" " << JobStateText(state);
                if (hasVerdict) std::wcout << L" verdict=" << VerdictText(verdict);
                if (cacheHit) std::wcout << L" cache=HIT";
                std::wcout << L'\n';
            }
            return state;
        }

        std::wcout << L"[job " << jobId << L"] " << JobStateText(state)
            << L" " << progress << L"% stage=" << stage;
        if (!text.empty()) std::wcout << L" - " << text;
        if (cacheHit) std::wcout << L" [CACHE HIT]";
        std::wcout << L'\n';

        if (hasVerdict)
        {
            std::wcout << L"  verdict=" << VerdictText(verdict)
                << L", score=" << riskScore
                << L", rules=0x" << std::hex << rules << std::dec
                << L", size=" << fileSize
                << L", entropy=" << entropy
                << L", durationMs=" << duration << L'\n';
        }
        return state;
    }

    int ScanCommand(
        const std::wstring& path,
        std::uint32_t priority,
        std::uint32_t timeoutMs,
        bool compact = false)
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        if (!OpenSession(pipe)) return 1;

        AvProtocol::TlvWriter writer;
        writer.AddWide(AvProtocol::FieldType::Path, path);
        writer.AddU32(AvProtocol::FieldType::Priority, priority);
        writer.AddU32(AvProtocol::FieldType::TimeoutMs, timeoutMs);
        DWORD error = ERROR_SUCCESS;
        const auto requestId = g_requestId++;
        if (!AvProtocol::WriteMessage(
                pipe,
                AvProtocol::MessageType::Scan,
                requestId,
                writer,
                error))
        {
            CloseHandle(pipe);
            return 1;
        }

        std::uint64_t jobId = 0;
        bool terminal = false;
        int exitCode = 1;
        while (!terminal)
        {
            AvProtocol::Message message;
            if (!AvProtocol::ReadMessage(pipe, message, error)) break;
            const auto type = static_cast<AvProtocol::MessageType>(message.header.type);
            if (type == AvProtocol::MessageType::Error)
            {
                PrintError(message);
                break;
            }
            if (type == AvProtocol::MessageType::Ack)
            {
                AvProtocol::TlvReader reader(message.payload);
                reader.GetU64(AvProtocol::FieldType::JobId, jobId);
                if (!compact)
                {
                    std::lock_guard lock(g_consoleMutex);
                    std::wcout << L"SCAN accepted. jobId=" << jobId << L'\n';
                }
                continue;
            }
            if (type == AvProtocol::MessageType::Event)
            {
                AvProtocol::TlvReader reader(message.payload);
                std::uint64_t eventJobId = 0;
                reader.GetU64(AvProtocol::FieldType::JobId, eventJobId);
                if (jobId == 0) jobId = eventJobId;
                if (eventJobId != jobId) continue;
                const std::uint32_t state = PrintJobMessage(message, compact);
                terminal = state == 3 || state == 4 || state == 5;
                exitCode = state == 3 ? 0 : 2;
            }
        }
        CloseHandle(pipe);
        return exitCode;
    }

    int QueryOrCancelCommand(bool cancel, std::uint64_t jobId)
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        if (!OpenSession(pipe)) return 1;
        AvProtocol::TlvWriter writer;
        writer.AddU64(AvProtocol::FieldType::JobId, jobId);
        DWORD error = ERROR_SUCCESS;
        const auto requestId = g_requestId++;
        AvProtocol::WriteMessage(
            pipe,
            cancel ? AvProtocol::MessageType::Cancel : AvProtocol::MessageType::Query,
            requestId,
            writer,
            error);
        AvProtocol::Message response;
        if (!AvProtocol::ReadMessage(pipe, response, error))
        {
            CloseHandle(pipe);
            return 1;
        }
        const auto type = static_cast<AvProtocol::MessageType>(response.header.type);
        if (type == AvProtocol::MessageType::Error) PrintError(response);
        else if (type == AvProtocol::MessageType::Event) PrintJobMessage(response, false);
        else
        {
            AvProtocol::TlvReader reader(response.payload);
            std::wstring text;
            reader.GetWide(AvProtocol::FieldType::Message, text);
            std::wcout << text << L'\n';
        }
        CloseHandle(pipe);
        return type == AvProtocol::MessageType::Error ? 1 : 0;
    }

    int TelemetryCommand()
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        if (!OpenSession(pipe)) return 1;
        AvProtocol::TlvWriter writer;
        DWORD error = ERROR_SUCCESS;
        AvProtocol::WriteMessage(
            pipe,
            AvProtocol::MessageType::Telemetry,
            g_requestId++,
            writer,
            error);
        AvProtocol::Message response;
        if (!AvProtocol::ReadMessage(pipe, response, error))
        {
            CloseHandle(pipe);
            return 1;
        }
        if (static_cast<AvProtocol::MessageType>(response.header.type) == AvProtocol::MessageType::Error)
        {
            PrintError(response);
            CloseHandle(pipe);
            return 1;
        }
        AvProtocol::TlvReader reader(response.payload);
        std::uint64_t received = 0, success = 0, failed = 0, cancelled = 0;
        std::uint64_t hits = 0, pending = 0, running = 0;
        double average = 0.0, p95 = 0.0;
        reader.GetU64(AvProtocol::FieldType::TotalReceived, received);
        reader.GetU64(AvProtocol::FieldType::TotalSucceeded, success);
        reader.GetU64(AvProtocol::FieldType::TotalFailed, failed);
        reader.GetU64(AvProtocol::FieldType::TotalCancelled, cancelled);
        reader.GetU64(AvProtocol::FieldType::CacheHits, hits);
        reader.GetU64(AvProtocol::FieldType::Pending, pending);
        reader.GetU64(AvProtocol::FieldType::Running, running);
        reader.GetDouble(AvProtocol::FieldType::AverageMs, average);
        reader.GetDouble(AvProtocol::FieldType::P95Ms, p95);
        std::wcout << L"received=" << received << L", success=" << success
            << L", failed=" << failed << L", cancelled=" << cancelled
            << L", cacheHits=" << hits << L", pending=" << pending
            << L", running=" << running << L", averageMs=" << average
            << L", p95Ms=" << p95 << L'\n';
        CloseHandle(pipe);
        return 0;
    }

    void Usage()
    {
        std::wcout
            << L"Usage:\n"
            << L"  ScanClient.exe scan <path> [--priority low|normal|high] [--timeout ms]\n"
            << L"  ScanClient.exe query <jobId>\n"
            << L"  ScanClient.exe cancel <jobId>\n"
            << L"  ScanClient.exe telemetry\n"
            << L"  ScanClient.exe stress <path> [--count 20] [--priority low|normal|high]\n";
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        Usage();
        return 1;
    }

    const std::wstring command = argv[1];
    if (_wcsicmp(command.c_str(), L"scan") == 0 && argc >= 3)
    {
        std::uint32_t priority = 1;
        std::uint32_t timeoutMs = 0;
        for (int i = 3; i < argc; ++i)
        {
            const std::wstring option = argv[i];
            if (option == L"--priority" && i + 1 < argc) priority = ParsePriority(argv[++i]);
            else if (option == L"--timeout" && i + 1 < argc) timeoutMs = static_cast<std::uint32_t>(_wtoi(argv[++i]));
        }
        return ScanCommand(argv[2], priority, timeoutMs);
    }
    if (_wcsicmp(command.c_str(), L"query") == 0 && argc >= 3)
    {
        return QueryOrCancelCommand(false, _wcstoui64(argv[2], nullptr, 10));
    }
    if (_wcsicmp(command.c_str(), L"cancel") == 0 && argc >= 3)
    {
        return QueryOrCancelCommand(true, _wcstoui64(argv[2], nullptr, 10));
    }
    if (_wcsicmp(command.c_str(), L"telemetry") == 0)
    {
        return TelemetryCommand();
    }
    if (_wcsicmp(command.c_str(), L"stress") == 0 && argc >= 3)
    {
        int count = 20;
        std::uint32_t priority = 0;
        for (int i = 3; i < argc; ++i)
        {
            const std::wstring option = argv[i];
            if (option == L"--count" && i + 1 < argc) count = (std::max)(1, _wtoi(argv[++i]));
            else if (option == L"--priority" && i + 1 < argc) priority = ParsePriority(argv[++i]);
        }
        std::vector<std::thread> threads;
        std::atomic_int failures{0};
        for (int i = 0; i < count; ++i)
        {
            threads.emplace_back([&, i]
            {
                UNREFERENCED_PARAMETER(i);
                if (ScanCommand(argv[2], priority, 0, true) != 0) ++failures;
            });
        }
        for (auto& thread : threads) thread.join();
        std::wcout << L"Stress completed. count=" << count << L", failures=" << failures.load() << L'\n';
        return failures.load() == 0 ? 0 : 2;
    }

    Usage();
    return 1;
}
