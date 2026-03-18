#include "Storage.h"

#include "PathService.h"

#include <fstream>
#include <string>

namespace resty
{
namespace
{
std::wstring ReadIniString(const std::wstring& section, const std::wstring& key, const std::wstring& fallback)
{
    wchar_t buffer[2048] = {};
    GetPrivateProfileStringW(section.c_str(), key.c_str(), fallback.c_str(), buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), GetConfigPath().c_str());
    return buffer;
}

int ReadIniInt(const std::wstring& section, const std::wstring& key, int fallback)
{
    return GetPrivateProfileIntW(section.c_str(), key.c_str(), fallback, GetConfigPath().c_str());
}

void WriteIniString(const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
    WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), GetConfigPath().c_str());
}

std::wstring WidenAscii(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}

std::string NarrowAscii(const std::wstring& value)
{
    std::string result;
    result.reserve(value.size());
    for (wchar_t ch : value)
    {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

void WriteRestRules(const std::vector<ScheduleRule>& rules)
{
    std::ofstream stream(GetRestDataPath(), std::ios::binary | std::ios::trunc);
    for (size_t index = 0; index < rules.size(); ++index)
    {
        if (index > 0)
        {
            stream << "\n";
        }
        stream << NarrowAscii(RuleToLine(rules[index]));
    }
}

std::vector<ScheduleRule> ReadRestRules()
{
    std::vector<ScheduleRule> rules;
    std::ifstream stream(GetRestDataPath(), std::ios::binary);
    if (!stream.is_open())
    {
        return rules;
    }

    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const std::wstring wideLine = Trim(WidenAscii(line));
        if (wideLine.empty() || wideLine.front() == L'#')
        {
            continue;
        }

        ScheduleRule rule;
        std::wstring error;
        if (ParseRuleLine(wideLine, rule, error))
        {
            rules.push_back(rule);
        }
    }

    return rules;
}
}

void SaveSettings(const AppSettings& settings)
{
    EnsureStorageDirectories();

    WriteIniString(L"app", L"auto_start", settings.launchAtStartup ? L"1" : L"0");
    WriteIniString(L"app", L"open_main_window_on_launch", settings.openMainWindowOnLaunch ? L"1" : L"0");
    WriteIniString(L"app", L"minimize_to_tray", settings.minimizeToTray ? L"1" : L"0");

    WriteIniString(L"short_rest", L"opacity", std::to_wstring(ClampOpacity(settings.shortRest.opacity)));
    WriteIniString(L"short_rest", L"message", settings.shortRest.message);
    WriteIniString(L"short_rest", L"color", FormatColor(settings.shortRest.color));

    WriteIniString(L"long_rest", L"opacity", std::to_wstring(ClampOpacity(settings.longRest.opacity)));
    WriteIniString(L"long_rest", L"message", settings.longRest.message);
    WriteIniString(L"long_rest", L"color", FormatColor(settings.longRest.color));

    WriteRestRules(settings.rules);
}

AppSettings LoadSettings()
{
    EnsureStorageDirectories();

    AppSettings settings = CreateDefaultSettings();
    const DWORD attributes = GetFileAttributesW(GetConfigPath().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        SaveSettings(settings);
        return settings;
    }

    settings.launchAtStartup = ReadIniInt(L"app", L"auto_start", settings.launchAtStartup ? 1 : 0) != 0;
    settings.openMainWindowOnLaunch = ReadIniInt(L"app", L"open_main_window_on_launch", settings.openMainWindowOnLaunch ? 1 : 0) != 0;
    settings.minimizeToTray = ReadIniInt(L"app", L"minimize_to_tray", settings.minimizeToTray ? 1 : 0) != 0;

    settings.shortRest.opacity = ClampOpacity(ReadIniInt(L"short_rest", L"opacity", settings.shortRest.opacity));
    settings.shortRest.message = ReadIniString(L"short_rest", L"message", settings.shortRest.message);
    ParseColor(ReadIniString(L"short_rest", L"color", FormatColor(settings.shortRest.color)), settings.shortRest.color);

    settings.longRest.opacity = ClampOpacity(ReadIniInt(L"long_rest", L"opacity", settings.longRest.opacity));
    settings.longRest.message = ReadIniString(L"long_rest", L"message", settings.longRest.message);
    ParseColor(ReadIniString(L"long_rest", L"color", FormatColor(settings.longRest.color)), settings.longRest.color);

    const std::vector<ScheduleRule> rules = ReadRestRules();
    if (!rules.empty())
    {
        settings.rules = rules;
    }

    return settings;
}
}
