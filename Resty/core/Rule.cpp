#include "Rule.h"

#include <algorithm>
#include <cwctype>
#include <ctime>
#include <sstream>

namespace resty
{
namespace
{
constexpr int kMinOpacity = 40;
constexpr int kMaxOpacity = 255;
constexpr int kMinDurationMinutes = 1;
constexpr int kMaxDurationMinutes = 240;
constexpr int kDefaultShortDurationMinutes = 5;
constexpr int kDefaultLongDurationMinutes = 15;

bool IsLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

bool IsValidDate(int year, int month, int day)
{
    if (year < 2000 || month < 1 || month > 12 || day < 1)
    {
        return false;
    }

    static const int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int maxDays = daysInMonth[month - 1];
    if (month == 2 && IsLeapYear(year))
    {
        maxDays = 29;
    }
    return day <= maxDays;
}

bool ParseTimeText(const std::wstring& value, int& hour, int& minute)
{
    hour = 0;
    minute = 0;
    if (swscanf_s(value.c_str(), L"%d:%d", &hour, &minute) != 2)
    {
        return false;
    }
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

bool ParseDurationText(const std::wstring& value, int& minutes)
{
    minutes = 0;
    if (swscanf_s(value.c_str(), L"%d", &minutes) != 1)
    {
        return false;
    }
    return minutes >= kMinDurationMinutes && minutes <= kMaxDurationMinutes;
}

bool ParseDateText(const std::wstring& value, int& year, int& month, int& day)
{
    year = 0;
    month = 0;
    day = 0;
    if (swscanf_s(value.c_str(), L"%d-%d-%d", &year, &month, &day) != 3)
    {
        return false;
    }
    return IsValidDate(year, month, day);
}

std::wstring Pad2(int value)
{
    wchar_t buffer[8] = {};
    swprintf_s(buffer, L"%02d", value);
    return buffer;
}

int GetWeekdayBit(int dayOfWeek)
{
    return 1 << dayOfWeek;
}

std::wstring WeekdayText(int dayOfWeek)
{
    static const wchar_t* names[] = { L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六" };
    if (dayOfWeek < 0 || dayOfWeek > 6)
    {
        return L"";
    }
    return names[dayOfWeek];
}

bool ParseWeekdayToken(const std::wstring& token, int& bit)
{
    const std::wstring value = ToLower(Trim(token));
    if (value == L"sun" || value == L"sunday" || value == L"周日" || value == L"星期日" || value == L"7")
    {
        bit = GetWeekdayBit(0);
        return true;
    }
    if (value == L"mon" || value == L"monday" || value == L"周一" || value == L"星期一" || value == L"1")
    {
        bit = GetWeekdayBit(1);
        return true;
    }
    if (value == L"tue" || value == L"tuesday" || value == L"周二" || value == L"星期二" || value == L"2")
    {
        bit = GetWeekdayBit(2);
        return true;
    }
    if (value == L"wed" || value == L"wednesday" || value == L"周三" || value == L"星期三" || value == L"3")
    {
        bit = GetWeekdayBit(3);
        return true;
    }
    if (value == L"thu" || value == L"thursday" || value == L"周四" || value == L"星期四" || value == L"4")
    {
        bit = GetWeekdayBit(4);
        return true;
    }
    if (value == L"fri" || value == L"friday" || value == L"周五" || value == L"星期五" || value == L"5")
    {
        bit = GetWeekdayBit(5);
        return true;
    }
    if (value == L"sat" || value == L"saturday" || value == L"周六" || value == L"星期六" || value == L"6")
    {
        bit = GetWeekdayBit(6);
        return true;
    }
    return false;
}

std::wstring WeekdayMaskText(int mask)
{
    std::wstring text;
    for (int day = 1; day <= 6; ++day)
    {
        if ((mask & GetWeekdayBit(day)) != 0)
        {
            if (!text.empty())
            {
                text += L",";
            }
            text += WeekdayText(day);
        }
    }

    if ((mask & GetWeekdayBit(0)) != 0)
    {
        if (!text.empty())
        {
            text += L",";
        }
        text += WeekdayText(0);
    }
    return text;
}

__time64_t ToTimeT(const SYSTEMTIME& st)
{
    std::tm localTime = {};
    localTime.tm_year = st.wYear - 1900;
    localTime.tm_mon = st.wMonth - 1;
    localTime.tm_mday = st.wDay;
    localTime.tm_hour = st.wHour;
    localTime.tm_min = st.wMinute;
    localTime.tm_sec = st.wSecond;
    localTime.tm_isdst = -1;
    return _mktime64(&localTime);
}

SYSTEMTIME FromTimeT(__time64_t value)
{
    std::tm localTime = {};
    _localtime64_s(&localTime, &value);

    SYSTEMTIME st = {};
    st.wYear = static_cast<WORD>(localTime.tm_year + 1900);
    st.wMonth = static_cast<WORD>(localTime.tm_mon + 1);
    st.wDay = static_cast<WORD>(localTime.tm_mday);
    st.wHour = static_cast<WORD>(localTime.tm_hour);
    st.wMinute = static_cast<WORD>(localTime.tm_min);
    st.wSecond = static_cast<WORD>(localTime.tm_sec);
    st.wDayOfWeek = static_cast<WORD>(localTime.tm_wday);
    return st;
}

bool GetNextOccurrence(const ScheduleRule& rule, const SYSTEMTIME& now, __time64_t& when)
{
    const __time64_t nowTime = ToTimeT(now);

    if (rule.mode == RuleMode::Daily)
    {
        SYSTEMTIME candidate = now;
        candidate.wHour = static_cast<WORD>(rule.hour);
        candidate.wMinute = static_cast<WORD>(rule.minute);
        candidate.wSecond = 0;
        candidate.wMilliseconds = 0;
        when = ToTimeT(candidate);
        if (when < nowTime - 59)
        {
            when += 24 * 60 * 60;
        }
        return true;
    }

    if (rule.mode == RuleMode::Weekly)
    {
        for (int dayOffset = 0; dayOffset <= 14; ++dayOffset)
        {
            const __time64_t dayValue = nowTime + static_cast<__time64_t>(dayOffset) * 24 * 60 * 60;
            SYSTEMTIME candidate = FromTimeT(dayValue);
            if ((rule.weekdaysMask & GetWeekdayBit(candidate.wDayOfWeek)) == 0)
            {
                continue;
            }

            candidate.wHour = static_cast<WORD>(rule.hour);
            candidate.wMinute = static_cast<WORD>(rule.minute);
            candidate.wSecond = 0;
            candidate.wMilliseconds = 0;
            when = ToTimeT(candidate);
            if (when >= nowTime - 59)
            {
                return true;
            }
        }
        return false;
    }

    SYSTEMTIME candidate = {};
    candidate.wYear = static_cast<WORD>(rule.year);
    candidate.wMonth = static_cast<WORD>(rule.month);
    candidate.wDay = static_cast<WORD>(rule.day);
    candidate.wHour = static_cast<WORD>(rule.hour);
    candidate.wMinute = static_cast<WORD>(rule.minute);
    when = ToTimeT(candidate);
    return when >= nowTime - 59;
}
}

std::wstring Trim(const std::wstring& value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) { return std::iswspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) { return std::iswspace(ch) != 0; }).base();
    if (begin >= end)
    {
        return L"";
    }
    return std::wstring(begin, end);
}

std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::vector<std::wstring> Split(const std::wstring& value, wchar_t delimiter)
{
    std::vector<std::wstring> result;
    std::wstringstream stream(value);
    std::wstring item;
    while (std::getline(stream, item, delimiter))
    {
        result.push_back(Trim(item));
    }
    return result;
}

int ClampOpacity(int value)
{
    return std::max(kMinOpacity, std::min(kMaxOpacity, value));
}

int ClampDurationMinutes(int value)
{
    return std::max(kMinDurationMinutes, std::min(kMaxDurationMinutes, value));
}

int GetDefaultDurationMinutes(RestKind kind)
{
    return kind == RestKind::Short ? kDefaultShortDurationMinutes : kDefaultLongDurationMinutes;
}

std::wstring FormatColor(COLORREF color)
{
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return buffer;
}

bool ParseColor(const std::wstring& text, COLORREF& color)
{
    std::wstring value = Trim(text);
    if (!value.empty() && value.front() == L'#')
    {
        value.erase(value.begin());
    }
    if (value.size() != 6)
    {
        return false;
    }

    unsigned int red = 0;
    unsigned int green = 0;
    unsigned int blue = 0;
    if (swscanf_s(value.c_str(), L"%02x%02x%02x", &red, &green, &blue) != 3)
    {
        return false;
    }

    color = RGB(red, green, blue);
    return true;
}

std::wstring GetRestKindText(RestKind kind)
{
    return kind == RestKind::Short ? L"小休息" : L"大休息";
}

bool ParseRuleLine(const std::wstring& line, ScheduleRule& rule, std::wstring& error)
{
    rule = ScheduleRule{};
    const auto parts = Split(line, L'|');
    if (parts.size() < 3)
    {
        error = L"规则格式应为 kind|mode|...";
        return false;
    }

    const std::wstring kindText = ToLower(parts[0]);
    if (kindText == L"short" || kindText == L"小休息")
    {
        rule.kind = RestKind::Short;
    }
    else if (kindText == L"long" || kindText == L"大休息")
    {
        rule.kind = RestKind::Long;
    }
    else
    {
        error = L"休息类型仅支持 short/long";
        return false;
    }
    rule.durationMinutes = GetDefaultDurationMinutes(rule.kind);

    const std::wstring modeText = ToLower(parts[1]);
    if (modeText == L"daily" || modeText == L"每天")
    {
        if ((parts.size() != 3 && parts.size() != 4) || !ParseTimeText(parts[2], rule.hour, rule.minute))
        {
            error = L"每天规则格式应为 short|daily|10:30|5";
            return false;
        }
        if (parts.size() == 4 && !ParseDurationText(parts[3], rule.durationMinutes))
        {
            error = L"休息时长必须是 1 到 240 之间的分钟数。";
            return false;
        }
        rule.mode = RuleMode::Daily;
        return true;
    }

    if (modeText == L"weekly" || modeText == L"每周")
    {
        if ((parts.size() != 4 && parts.size() != 5) || !ParseTimeText(parts[3], rule.hour, rule.minute))
        {
            error = L"每周规则格式应为 short|weekly|Mon,Tue,Fri|10:30|5";
            return false;
        }

        int mask = 0;
        for (const auto& token : Split(parts[2], L','))
        {
            int bit = 0;
            if (!ParseWeekdayToken(token, bit))
            {
                error = L"每周规则中的星期格式无效：" + token;
                return false;
            }
            mask |= bit;
        }
        if (mask == 0)
        {
            error = L"每周规则至少需要一个星期";
            return false;
        }
        if (parts.size() == 5 && !ParseDurationText(parts[4], rule.durationMinutes))
        {
            error = L"休息时长必须是 1 到 240 之间的分钟数。";
            return false;
        }
        rule.mode = RuleMode::Weekly;
        rule.weekdaysMask = mask;
        return true;
    }

    if (modeText == L"date" || modeText == L"固定日期")
    {
        if ((parts.size() != 4 && parts.size() != 5) || !ParseDateText(parts[2], rule.year, rule.month, rule.day) || !ParseTimeText(parts[3], rule.hour, rule.minute))
        {
            error = L"固定日期规则格式应为 long|date|2026-05-01|15:00|15";
            return false;
        }
        if (parts.size() == 5 && !ParseDurationText(parts[4], rule.durationMinutes))
        {
            error = L"休息时长必须是 1 到 240 之间的分钟数。";
            return false;
        }
        rule.mode = RuleMode::Date;
        return true;
    }

    error = L"规则模式仅支持 daily / weekly / date";
    return false;
}

std::wstring RuleToLine(const ScheduleRule& rule)
{
    std::wstring line = rule.kind == RestKind::Short ? L"short|" : L"long|";
    if (rule.mode == RuleMode::Daily)
    {
        return line + L"daily|" + Pad2(rule.hour) + L":" + Pad2(rule.minute) + L"|" + std::to_wstring(ClampDurationMinutes(rule.durationMinutes));
    }

    if (rule.mode == RuleMode::Weekly)
    {
        line += L"weekly|";
        const wchar_t* labels[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
        bool first = true;
        for (int day = 0; day < 7; ++day)
        {
            if ((rule.weekdaysMask & GetWeekdayBit(day)) != 0)
            {
                if (!first)
                {
                    line += L",";
                }
                line += labels[day];
                first = false;
            }
        }
        return line + L"|" + Pad2(rule.hour) + L":" + Pad2(rule.minute) + L"|" + std::to_wstring(ClampDurationMinutes(rule.durationMinutes));
    }

    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"date|%04d-%02d-%02d|%02d:%02d|%d", rule.year, rule.month, rule.day, rule.hour, rule.minute, ClampDurationMinutes(rule.durationMinutes));
    return line + buffer;
}

std::wstring FormatRuleSummary(const ScheduleRule& rule)
{
    wchar_t buffer[128] = {};
    if (rule.mode == RuleMode::Daily)
    {
        swprintf_s(buffer, L"每天 %02d:%02d · %s · %d 分钟", rule.hour, rule.minute, GetRestKindText(rule.kind).c_str(), ClampDurationMinutes(rule.durationMinutes));
        return buffer;
    }

    if (rule.mode == RuleMode::Weekly)
    {
        swprintf_s(buffer, L"每周 %s %02d:%02d · %s · %d 分钟", WeekdayMaskText(rule.weekdaysMask).c_str(), rule.hour, rule.minute, GetRestKindText(rule.kind).c_str(), ClampDurationMinutes(rule.durationMinutes));
        return buffer;
    }

    swprintf_s(buffer, L"固定日期 %04d-%02d-%02d %02d:%02d · %s · %d 分钟", rule.year, rule.month, rule.day, rule.hour, rule.minute, GetRestKindText(rule.kind).c_str(), ClampDurationMinutes(rule.durationMinutes));
    return buffer;
}

AppSettings CreateDefaultSettings()
{
    AppSettings settings;
    settings.launchAtStartup = false;
    settings.openMainWindowOnLaunch = true;
    settings.minimizeToTray = true;
    settings.shortRest.opacity = 220;
    settings.shortRest.color = RGB(30, 64, 175);
    settings.shortRest.message = L"站起来，活动肩颈和眼睛。";
    settings.longRest.opacity = 235;
    settings.longRest.color = RGB(127, 29, 29);
    settings.longRest.message = L"离开工位，走动几分钟，真正休息一下。";

    settings.rules.push_back({ RestKind::Short, RuleMode::Weekly, 10, 30, 5, GetWeekdayBit(1) | GetWeekdayBit(2) | GetWeekdayBit(3) | GetWeekdayBit(4) | GetWeekdayBit(5), 0, 0, 0 });
    settings.rules.push_back({ RestKind::Short, RuleMode::Weekly, 15, 30, 5, GetWeekdayBit(1) | GetWeekdayBit(2) | GetWeekdayBit(3) | GetWeekdayBit(4) | GetWeekdayBit(5), 0, 0, 0 });
    settings.rules.push_back({ RestKind::Long, RuleMode::Weekly, 12, 0, 15, GetWeekdayBit(1) | GetWeekdayBit(2) | GetWeekdayBit(3) | GetWeekdayBit(4) | GetWeekdayBit(5), 0, 0, 0 });
    return settings;
}

bool IsRuleDueNow(const ScheduleRule& rule, const SYSTEMTIME& now)
{
    if (rule.hour != now.wHour || rule.minute != now.wMinute)
    {
        return false;
    }

    if (rule.mode == RuleMode::Daily)
    {
        return true;
    }

    if (rule.mode == RuleMode::Weekly)
    {
        return (rule.weekdaysMask & GetWeekdayBit(now.wDayOfWeek)) != 0;
    }

    return rule.year == now.wYear && rule.month == now.wMonth && rule.day == now.wDay;
}

ScheduledRest FindNextRest(const AppSettings& settings, const SYSTEMTIME& now)
{
    ScheduledRest next;
    for (const auto& rule : settings.rules)
    {
        __time64_t when = 0;
        if (!GetNextOccurrence(rule, now, when))
        {
            continue;
        }

        if (!next.valid || when < next.when)
        {
            next.valid = true;
            next.when = when;
            next.kind = rule.kind;
            next.durationMinutes = ClampDurationMinutes(rule.durationMinutes);
        }
    }

    if (next.valid)
    {
        const SYSTEMTIME nextTime = FromTimeT(next.when);
        wchar_t buffer[160] = {};
        swprintf_s(buffer, L"%04d-%02d-%02d %s %02d:%02d · %s",
            nextTime.wYear,
            nextTime.wMonth,
            nextTime.wDay,
            WeekdayText(nextTime.wDayOfWeek).c_str(),
            nextTime.wHour,
            nextTime.wMinute,
            (GetRestKindText(next.kind) + L" · " + std::to_wstring(next.durationMinutes) + L" 分钟").c_str());
        next.description = buffer;
    }

    return next;
}

std::wstring FormatCountdown(__time64_t seconds)
{
    if (seconds < 0)
    {
        seconds = 0;
    }

    const __time64_t hours = seconds / 3600;
    const __time64_t minutes = (seconds % 3600) / 60;
    const __time64_t remainSeconds = seconds % 60;

    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%02lld:%02lld:%02lld", hours, minutes, remainSeconds);
    return buffer;
}

std::wstring BuildDueKey(const ScheduleRule& rule, const SYSTEMTIME& now)
{
    wchar_t buffer[96] = {};
    swprintf_s(buffer, L"%d|%d|%04d%02d%02d%02d%02d",
        static_cast<int>(rule.kind),
        static_cast<int>(rule.mode),
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute);
    return buffer;
}
}
