#pragma once

#include "Security/ClientIdentity.h"
#include "Protocol/Protocol.h"

#include <Windows.h>
#include <string>

class PipeSecurityDescriptor
{
public:
    PipeSecurityDescriptor();
    ~PipeSecurityDescriptor();
    PipeSecurityDescriptor(const PipeSecurityDescriptor&) = delete;
    PipeSecurityDescriptor& operator=(const PipeSecurityDescriptor&) = delete;

    bool Build();
    SECURITY_ATTRIBUTES* Attributes() noexcept;

private:
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    SECURITY_ATTRIBUTES attributes_{};
};

class PipeSecurity
{
public:
    static bool Authenticate(
        HANDLE pipe,
        DWORD claimedPid,
        const std::wstring& claimedUser,
        ClientIdentity& identity,
        AvProtocol::ServiceErrorCode& errorCode,
        std::wstring& errorMessage);
};
