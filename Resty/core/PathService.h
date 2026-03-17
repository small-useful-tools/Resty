#pragma once

#include <string>

namespace resty
{
std::wstring GetUserProfileDirectory();
std::wstring GetStorageRootDirectory();
std::wstring GetDataDirectory();
std::wstring GetConfigPath();
std::wstring GetRestDataPath();
std::wstring GetModulePath();
std::wstring QuotePath(const std::wstring& value);
void EnsureStorageDirectories();
}
