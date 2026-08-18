#include "Security/PipeSecurity.h"

#include <Sddl.h>
#include <algorithm>
#include <iterator>
#include <vector>

namespace
{
    // Doc SID/user/integrity tu access token cua client that su dang ket noi Pipe.
    // Khong tin vao truong User ma client tu gui len.
    bool ReadTokenIdentity(HANDLE token, ClientIdentity& identity)
    {
        DWORD required = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &required);
        if (required == 0) return false;
        std::vector<std::uint8_t> buffer(required);
        if (!GetTokenInformation(token, TokenUser, buffer.data(), required, &required)) return false;
        const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(buffer.data());

        LPWSTR sidText = nullptr;
        if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText)) return false;
        identity.userSid = sidText;
        LocalFree(sidText);

        DWORD tokenSession = 0;
        required = sizeof(tokenSession);
        if (!GetTokenInformation(token, TokenSessionId, &tokenSession, required, &required)) return false;
        identity.sessionId = tokenSession;

        wchar_t account[256]{};
        wchar_t domain[256]{};
        DWORD accountSize = static_cast<DWORD>(std::size(account));
        DWORD domainSize = static_cast<DWORD>(std::size(domain));
        SID_NAME_USE use{};
        if (LookupAccountSidW(
                nullptr,
                tokenUser->User.Sid,
                account,
                &accountSize,
                domain,
                &domainSize,
                &use))
        {
            identity.userName = domain[0] == L'\0'
                ? std::wstring(account)
                : std::wstring(domain) + L"\\" + account;
        }
        else
        {
            identity.userName = identity.userSid;
        }

        BYTE interactiveSidBuffer[SECURITY_MAX_SID_SIZE]{};
        DWORD sidSize = sizeof(interactiveSidBuffer);
        if (CreateWellKnownSid(WinInteractiveSid, nullptr, interactiveSidBuffer, &sidSize))
        {
            BOOL isMember = FALSE;
            if (CheckTokenMembership(token, interactiveSidBuffer, &isMember))
            {
                identity.interactive = isMember != FALSE;
            }
        }
        return true;
    }

    // So sanh ten user client khai bao trong HELLO voi danh tinh lay tu token he thong.
    bool ClaimedUserMatches(const std::wstring& claimed, const std::wstring& actual)
    {
        if (claimed.empty()) return true;
        if (_wcsicmp(claimed.c_str(), actual.c_str()) == 0) return true;
        const auto slash = actual.find_last_of(L'\\');
        const std::wstring account = slash == std::wstring::npos ? actual : actual.substr(slash + 1);
        return _wcsicmp(claimed.c_str(), account.c_str()) == 0;
    }
}

PipeSecurityDescriptor::PipeSecurityDescriptor()
{
    attributes_.nLength = sizeof(attributes_);
    attributes_.bInheritHandle = FALSE;
}

PipeSecurityDescriptor::~PipeSecurityDescriptor()
{
    if (descriptor_ != nullptr) LocalFree(descriptor_);
}

// Tao Security Descriptor/ACL cho Named Pipe de han che doi tuong co quyen ket noi.
bool PipeSecurityDescriptor::Build()
{
    if (descriptor_ != nullptr)
    {
        LocalFree(descriptor_);
        descriptor_ = nullptr;
    }

    // SYSTEM + Administrators = full; Interactive Users = read/write.
    // Authentication below further binds the connection to the real PID/SID/session.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)",
            SDDL_REVISION_1,
            &descriptor_,
            nullptr))
    {
        return false;
    }
    attributes_.lpSecurityDescriptor = descriptor_;
    return true;
}

SECURITY_ATTRIBUTES* PipeSecurityDescriptor::Attributes() noexcept
{
    return descriptor_ == nullptr ? nullptr : &attributes_;
}

bool PipeSecurity::Authenticate(
    HANDLE pipe,
    DWORD claimedPid,
    const std::wstring& claimedUser,
    ClientIdentity& identity,
    AvProtocol::ServiceErrorCode& errorCode,
    std::wstring& errorMessage)
{
    identity = {};
    errorCode = AvProtocol::ServiceErrorCode::Ok;
    errorMessage.clear();

    ULONG actualPid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &actualPid))
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthTokenFailed;
        errorMessage = L"Cannot obtain Named Pipe client PID";
        return false;
    }
    if (claimedPid == 0 || actualPid != claimedPid)
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthPidMismatch;
        errorMessage = L"HELLO PID does not match the real Named Pipe client PID";
        return false;
    }

    if (!ImpersonateNamedPipeClient(pipe))
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthTokenFailed;
        errorMessage = L"ImpersonateNamedPipeClient failed";
        return false;
    }

    HANDLE pipeToken = nullptr;
    const BOOL tokenOpened = OpenThreadToken(
        GetCurrentThread(), TOKEN_QUERY, TRUE, &pipeToken);
    if (!tokenOpened)
    {
        RevertToSelf();
        errorCode = AvProtocol::ServiceErrorCode::AuthTokenFailed;
        errorMessage = L"Cannot open impersonated client token";
        return false;
    }

    ClientIdentity pipeIdentity{};
    const bool pipeIdentityOk = ReadTokenIdentity(pipeToken, pipeIdentity);
    CloseHandle(pipeToken);
    RevertToSelf();
    if (!pipeIdentityOk)
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthTokenFailed;
        errorMessage = L"Cannot read client token identity";
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, actualPid);
    if (process == nullptr)
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthPidMismatch;
        errorMessage = L"Client process no longer exists";
        return false;
    }
    HANDLE processToken = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &processToken))
    {
        CloseHandle(process);
        errorCode = AvProtocol::ServiceErrorCode::AuthTokenFailed;
        errorMessage = L"Cannot open client process token";
        return false;
    }

    ClientIdentity processIdentity{};
    const bool processIdentityOk = ReadTokenIdentity(processToken, processIdentity);
    CloseHandle(processToken);
    CloseHandle(process);
    if (!processIdentityOk)
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthTokenFailed;
        errorMessage = L"Cannot read client process identity";
        return false;
    }

    if (!pipeIdentity.SamePrincipal(processIdentity))
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthSessionMismatch;
        errorMessage = L"Pipe token and process token do not belong to the same SID/session";
        return false;
    }
    if (!ClaimedUserMatches(claimedUser, pipeIdentity.userName))
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthUserMismatch;
        errorMessage = L"HELLO user does not match the authenticated token user";
        return false;
    }
    if (!pipeIdentity.interactive)
    {
        errorCode = AvProtocol::ServiceErrorCode::AuthGroupDenied;
        errorMessage = L"Client is not an Interactive Users token";
        return false;
    }

    identity = pipeIdentity;
    identity.pid = actualPid;
    return true;
}
