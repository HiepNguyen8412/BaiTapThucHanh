#pragma once

#include "Security/ClientIdentity.h"

#include <string>
#include <vector>

class PolicyManager
{
public:
    PolicyManager();
    bool CanScan(
        const ClientIdentity& identity,
        const std::wstring& inputPath,
        std::wstring& normalizedPath,
        std::wstring& reason) const;

private:
    std::vector<std::wstring> deniedRoots_;
};
