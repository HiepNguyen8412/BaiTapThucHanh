#pragma once

#include <Windows.h>
#include <string>

struct ClientIdentity
{
    DWORD pid{};
    DWORD sessionId{};
    std::wstring userSid;
    std::wstring userName;
    bool interactive{false};

    bool SamePrincipal(const ClientIdentity& other) const noexcept
    {
        return sessionId == other.sessionId && userSid == other.userSid;
    }
};
