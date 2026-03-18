#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

namespace resty
{
enum class RestKind
{
    Short,
    Long,
};

enum class RuleMode
{
    Daily,
    Weekly,
    Date,
};

struct RestStyle
{
    int opacity = 220;
    COLORREF color = RGB(42, 67, 101);
    std::wstring message;
};

struct ScheduleRule
{
    RestKind kind = RestKind::Short;
    RuleMode mode = RuleMode::Daily;
    int hour = 9;
    int minute = 0;
    int durationMinutes = 5;
    int weekdaysMask = 0;
    int year = 0;
    int month = 0;
    int day = 0;
};

struct ScheduledRest
{
    bool valid = false;
    RestKind kind = RestKind::Short;
    __time64_t when = 0;
    int durationMinutes = 0;
    std::wstring description;
};

struct AppSettings
{
    bool launchAtStartup = false;
    bool openMainWindowOnLaunch = true;
    bool minimizeToTray = true;
    RestStyle shortRest;
    RestStyle longRest;
    std::vector<ScheduleRule> rules;
};

std::wstring Trim(const std::wstring& value);
std::wstring ToLower(std::wstring value);
std::vector<std::wstring> Split(const std::wstring& value, wchar_t delimiter);

int ClampOpacity(int value);
int ClampDurationMinutes(int value);
int GetDefaultDurationMinutes(RestKind kind);
std::wstring FormatColor(COLORREF color);
bool ParseColor(const std::wstring& text, COLORREF& color);
std::wstring GetRestKindText(RestKind kind);

bool ParseRuleLine(const std::wstring& line, ScheduleRule& rule, std::wstring& error);
std::wstring RuleToLine(const ScheduleRule& rule);
std::wstring FormatRuleSummary(const ScheduleRule& rule);

AppSettings CreateDefaultSettings();
bool IsRuleDueNow(const ScheduleRule& rule, const SYSTEMTIME& now);
ScheduledRest FindNextRest(const AppSettings& settings, const SYSTEMTIME& now);
std::wstring FormatCountdown(__time64_t seconds);
std::wstring BuildDueKey(const ScheduleRule& rule, const SYSTEMTIME& now);
}
