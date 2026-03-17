#include "PathService.h"

#include <windows.h>
#include <ShlObj.h>

namespace resty
{
namespace
{
void EnsureDirectory(const std::wstring& path)
{
    if (path.empty())
    {
        return;
    }

    CreateDirectoryW(path.c_str(), nullptr);
}
}

std::wstring GetUserProfileDirectory()
{
    wchar_t buffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, buffer)))
    {
        return buffer;
    }

    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
    {
        return buffer;
    }

    return L".";
}

std::wstring GetStorageRootDirectory()
{
    return GetUserProfileDirectory() + L"\\.resty";
}

std::wstring GetDataDirectory()
{
    return GetStorageRootDirectory() + L"\\data";
}

std::wstring GetConfigPath()
{
    return GetStorageRootDirectory() + L"\\config.ini";
}

std::wstring GetRestDataPath()
{
    return GetDataDirectory() + L"\\rest.txt";
}

std::wstring GetModulePath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring QuotePath(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

void EnsureStorageDirectories()
{
    EnsureDirectory(GetStorageRootDirectory());
    EnsureDirectory(GetDataDirectory());
}
}
