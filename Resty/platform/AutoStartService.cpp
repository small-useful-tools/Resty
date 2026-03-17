#include "AutoStartService.h"

#include "../core/PathService.h"

#include <windows.h>
#include <winreg.h>

namespace resty
{
namespace
{
constexpr wchar_t kRunRegistryPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunRegistryName[] = L"Resty";
}

void SetAutoStart(bool enabled)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunRegistryPath, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    if (enabled)
    {
        const std::wstring path = QuotePath(GetModulePath());
        RegSetValueExW(key, kRunRegistryName, 0, REG_SZ, reinterpret_cast<const BYTE*>(path.c_str()), static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, kRunRegistryName);
    }

    RegCloseKey(key);
}
}
