#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#include "resource.h"
#include "core/PathService.h"
#include "core/Rule.h"
#include "core/Storage.h"
#include "platform/AutoStartService.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "UxTheme.lib")

namespace
{
constexpr wchar_t kMainClassName[] = L"RestyMainWindow";
constexpr wchar_t kSettingsClassName[] = L"RestySettingsWindow";
constexpr wchar_t kOverlayClassName[] = L"RestyOverlayWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kMainTimerId = 1;
constexpr UINT_PTR kOverlayTimerId = 2;
constexpr int kShortOverlaySeconds = 20;
constexpr int kLongOverlaySeconds = 60;
constexpr COLORREF kWindowBackground = RGB(244, 247, 252);
constexpr COLORREF kCardBackground = RGB(255, 255, 255);
constexpr COLORREF kCardBorder = RGB(224, 231, 255);
constexpr COLORREF kAccentColor = RGB(59, 130, 246);
constexpr COLORREF kPrimaryText = RGB(30, 41, 59);
constexpr COLORREF kSecondaryText = RGB(100, 116, 139);

void SetWindowTextSafe(HWND hwnd, const std::wstring& value)
{
    if (hwnd != nullptr)
    {
        const int length = GetWindowTextLengthW(hwnd);
        std::wstring current(static_cast<size_t>(std::max(length, 0)) + 1, L'\0');
        if (length > 0)
        {
            GetWindowTextW(hwnd, &current[0], length + 1);
            current.resize(length);
        }
        else
        {
            current.clear();
        }

        if (current != value)
        {
            SetWindowTextW(hwnd, value.c_str());
        }
    }
}

std::wstring GetWindowTextString(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
    {
        return L"";
    }

    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, &value[0], length + 1);
    value.resize(length);
    return value;
}

void SetControlFont(HWND control, HFONT font)
{
    if (control != nullptr)
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

COLORREF ChooseTextColor(COLORREF background)
{
    const int brightness = (GetRValue(background) * 299 + GetGValue(background) * 587 + GetBValue(background) * 114) / 1000;
    return brightness > 135 ? RGB(32, 32, 32) : RGB(255, 255, 255);
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawRoundedCard(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void ApplyEditTheme(HWND edit, const wchar_t* cueBanner)
{
    if (edit == nullptr)
    {
        return;
    }

    SetWindowTheme(edit, L"Explorer", nullptr);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
    SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cueBanner));
}

HICON LoadAppIcon(HINSTANCE instance)
{
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_RESTY_APP));
    if (icon == nullptr)
    {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return icon;
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
}

class RestyApp
{
public:
    bool Initialize(HINSTANCE instance)
    {
        instance_ = instance;

        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);

        resty::EnsureStorageDirectories();
        settings_ = resty::LoadSettings();
        resty::SetAutoStart(settings_.launchAtStartup);

        if (!RegisterWindowClasses())
        {
            return false;
        }
        if (!CreateFonts())
        {
            return false;
        }
        if (!CreateMainWindow())
        {
            return false;
        }

        CreateTrayIcon();
        UpdateHomeDisplay();
        return true;
    }

    int Run(int nCmdShow)
    {
        ShowWindow(mainWindow_, nCmdShow);
        UpdateWindow(mainWindow_);

        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return static_cast<int>(message.wParam);
    }

    static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        RestyApp* app = reinterpret_cast<RestyApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<RestyApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        return app != nullptr ? app->HandleMainWindowMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        RestyApp* app = reinterpret_cast<RestyApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<RestyApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        return app != nullptr ? app->HandleSettingsWindowMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        RestyApp* app = reinterpret_cast<RestyApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<RestyApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        return app != nullptr ? app->HandleOverlayWindowMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

private:
    enum ControlId
    {
        IdCountdownValue = 1001,
        IdNextValue,
        IdSummaryValue,
        IdOpenSettings,
        IdPreviewShort,
        IdPreviewLong,
        IdSettingsStartup,
        IdSettingsTray,
        IdSettingsShortOpacity,
        IdSettingsShortMessage,
        IdSettingsShortColor,
        IdSettingsLongOpacity,
        IdSettingsLongMessage,
        IdSettingsLongColor,
        IdSettingsRules,
        IdRuleList,
        IdRuleKind,
        IdRuleMode,
        IdRuleTime,
        IdRuleDuration,
        IdRuleDate,
        IdRuleWeekdaySun,
        IdRuleWeekdayMon,
        IdRuleWeekdayTue,
        IdRuleWeekdayWed,
        IdRuleWeekdayThu,
        IdRuleWeekdayFri,
        IdRuleWeekdaySat,
        IdRuleAdd,
        IdRuleUpdate,
        IdRuleDelete,
        IdSettingsSave,
        IdSettingsClose,
        IdTrayShowMain,
        IdTrayShowSettings,
        IdTrayExit,
    };

    bool RegisterWindowClasses() const
    {
        WNDCLASSEXW mainClass = {};
        mainClass.cbSize = sizeof(mainClass);
        mainClass.hInstance = instance_;
        mainClass.lpfnWndProc = MainWindowProc;
        mainClass.lpszClassName = kMainClassName;
        mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        mainClass.hIcon = LoadAppIcon(instance_);
        mainClass.hIconSm = LoadAppIcon(instance_);
        if (RegisterClassExW(&mainClass) == 0)
        {
            return false;
        }

        WNDCLASSEXW settingsClass = {};
        settingsClass.cbSize = sizeof(settingsClass);
        settingsClass.hInstance = instance_;
        settingsClass.lpfnWndProc = SettingsWindowProc;
        settingsClass.lpszClassName = kSettingsClassName;
        settingsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        settingsClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        settingsClass.hIcon = LoadAppIcon(instance_);
        settingsClass.hIconSm = LoadAppIcon(instance_);
        if (RegisterClassExW(&settingsClass) == 0)
        {
            return false;
        }

        WNDCLASSEXW overlayClass = {};
        overlayClass.cbSize = sizeof(overlayClass);
        overlayClass.hInstance = instance_;
        overlayClass.lpfnWndProc = OverlayWindowProc;
        overlayClass.lpszClassName = kOverlayClassName;
        overlayClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
        overlayClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        return RegisterClassExW(&overlayClass) != 0;
    }

    bool CreateFonts()
    {
        baseFont_ = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        titleFont_ = CreateFontW(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        sectionFont_ = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        captionFont_ = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        countdownFont_ = CreateFontW(42, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        overlayTitleFont_ = CreateFontW(46, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        overlayCountdownFont_ = CreateFontW(34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        overlayMessageFont_ = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

        return baseFont_ != nullptr && titleFont_ != nullptr && sectionFont_ != nullptr && captionFont_ != nullptr
            && countdownFont_ != nullptr && overlayTitleFont_ != nullptr && overlayCountdownFont_ != nullptr && overlayMessageFont_ != nullptr;
    }

    bool CreateMainWindow()
    {
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect = { 0, 0, 860, 500 };
        AdjustWindowRect(&rect, style, FALSE);
        mainWindow_ = CreateWindowExW(0,
            kMainClassName,
            L"Resty - 工作休息提醒",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance_,
            this);
        return mainWindow_ != nullptr;
    }

    void CreateMainControls(HWND hwnd)
    {
        mainTitleLabel_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"专注工作，按时休息", WS_CHILD | WS_VISIBLE, 32, 28, 320, 34, hwnd, nullptr, instance_, nullptr);
        mainSubtitleLabel_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"Resty 会根据规则自动提醒你离开屏幕、活动肩颈与放松眼睛。", WS_CHILD | WS_VISIBLE, 32, 66, 520, 22, hwnd, nullptr, instance_, nullptr);
        countdownLabel_ = CreateWindowExW(0, L"STATIC", L"距离下次休息", WS_CHILD | WS_VISIBLE, 56, 122, 180, 24, hwnd, nullptr, instance_, nullptr);
        countdownValue_ = CreateWindowExW(0, L"STATIC", L"00:00:00", WS_CHILD | WS_VISIBLE, 56, 150, 320, 78, hwnd, reinterpret_cast<HMENU>(IdCountdownValue), instance_, nullptr);
        countdownHintLabel_ = CreateWindowExW(0, L"STATIC", L"倒计时会实时刷新，命中规则后自动弹出提醒遮罩。", WS_CHILD | WS_VISIBLE, 56, 236, 300, 20, hwnd, nullptr, instance_, nullptr);

        nextLabel_ = CreateWindowExW(0, L"STATIC", L"下一条提醒", WS_CHILD | WS_VISIBLE, 444, 122, 180, 24, hwnd, nullptr, instance_, nullptr);
        nextValue_ = CreateWindowExW(0, L"STATIC", L"未找到规则", WS_CHILD | WS_VISIBLE, 444, 150, 340, 72, hwnd, reinterpret_cast<HMENU>(IdNextValue), instance_, nullptr);
        nextHintLabel_ = CreateWindowExW(0, L"STATIC", L"支持 daily / weekly / date，多段规则会自动取最近的一次。", WS_CHILD | WS_VISIBLE, 444, 236, 340, 20, hwnd, nullptr, instance_, nullptr);

        storageLabel_ = CreateWindowExW(0, L"STATIC", L"本地存储", WS_CHILD | WS_VISIBLE, 56, 344, 180, 24, hwnd, nullptr, instance_, nullptr);
        summaryValue_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 56, 376, 472, 68, hwnd, reinterpret_cast<HMENU>(IdSummaryValue), instance_, nullptr);
        actionLabel_ = CreateWindowExW(0, L"STATIC", L"快速操作", WS_CHILD | WS_VISIBLE, 580, 344, 180, 24, hwnd, nullptr, instance_, nullptr);

        openSettingsButton_ = CreateWindowExW(0, L"BUTTON", L"打开设置", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 580, 376, 108, 40, hwnd, reinterpret_cast<HMENU>(IdOpenSettings), instance_, nullptr);
        previewShortButton_ = CreateWindowExW(0, L"BUTTON", L"预览小休息", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 700, 376, 108, 40, hwnd, reinterpret_cast<HMENU>(IdPreviewShort), instance_, nullptr);
        previewLongButton_ = CreateWindowExW(0, L"BUTTON", L"预览大休息", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 580, 424, 228, 40, hwnd, reinterpret_cast<HMENU>(IdPreviewLong), instance_, nullptr);

        for (HWND child : { mainTitleLabel_, mainSubtitleLabel_, countdownLabel_, countdownValue_, countdownHintLabel_, nextLabel_, nextValue_,
            nextHintLabel_, storageLabel_, summaryValue_, actionLabel_, openSettingsButton_, previewShortButton_, previewLongButton_ })
        {
            SetControlFont(child, baseFont_);
        }

        SetControlFont(mainTitleLabel_, titleFont_);
        SetControlFont(mainSubtitleLabel_, captionFont_);
        SetControlFont(countdownLabel_, sectionFont_);
        SetControlFont(countdownValue_, countdownFont_);
        SetControlFont(countdownHintLabel_, captionFont_);
        SetControlFont(nextLabel_, sectionFont_);
        SetControlFont(nextHintLabel_, captionFont_);
        SetControlFont(storageLabel_, sectionFont_);
        SetControlFont(actionLabel_, sectionFont_);
    }

    void DrawMainWindow(HWND hwnd) const
    {
        RECT client = {};
        GetClientRect(hwnd, &client);

        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(hwnd, &paint);
        FillSolidRect(dc, client, kWindowBackground);

        const RECT accentPill = { 32, 18, 140, 24 };
        DrawRoundedCard(dc, accentPill, kAccentColor, kAccentColor, 8);

        const RECT countdownCard = { 32, 104, 392, 280 };
        const RECT nextCard = { 420, 104, 828, 280 };
        const RECT footerCard = { 32, 326, 828, 474 };
        DrawRoundedCard(dc, countdownCard, kCardBackground, kCardBorder, 20);
        DrawRoundedCard(dc, nextCard, kCardBackground, kCardBorder, 20);
        DrawRoundedCard(dc, footerCard, kCardBackground, kCardBorder, 20);

        EndPaint(hwnd, &paint);
    }

    void CenterWindowOnScreen(HWND hwnd) const
    {
        RECT rect = {};
        GetWindowRect(hwnd, &rect);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 3;
        SetWindowPos(hwnd, nullptr, std::max(0, x), std::max(0, y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    void CreateTrayIcon()
    {
        trayIcon_.cbSize = sizeof(trayIcon_);
        trayIcon_.hWnd = mainWindow_;
        trayIcon_.uID = 1;
        trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        trayIcon_.uCallbackMessage = kTrayMessage;
        trayIcon_.hIcon = LoadAppIcon(instance_);
        wcscpy_s(trayIcon_.szTip, L"Resty - 工作休息提醒");
        Shell_NotifyIconW(NIM_ADD, &trayIcon_);
    }

    void RemoveTrayIcon()
    {
        if (trayIcon_.hWnd != nullptr)
        {
            Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
            trayIcon_.hWnd = nullptr;
        }
    }

    void ShowTrayMenu()
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IdTrayShowMain, L"显示主页");
        AppendMenuW(menu, MF_STRING, IdTrayShowSettings, L"打开设置");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IdTrayExit, L"退出");

        POINT point = {};
        GetCursorPos(&point);
        SetForegroundWindow(mainWindow_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, mainWindow_, nullptr);
        DestroyMenu(menu);
    }

    void ShowMainWindow()
    {
        ShowWindow(mainWindow_, SW_SHOWNORMAL);
        SetForegroundWindow(mainWindow_);
    }

    void HideMainWindow()
    {
        ShowWindow(mainWindow_, SW_HIDE);
    }

    void EnsureSettingsWindow()
    {
        if (settingsWindow_ != nullptr)
        {
            return;
        }

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect = { 0, 0, 860, 820 };
        AdjustWindowRect(&rect, style, FALSE);
        settingsWindow_ = CreateWindowExW(WS_EX_APPWINDOW,
            kSettingsClassName,
            L"Resty 设置",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance_,
            this);
    }

    void CreateSettingsControls(HWND hwnd)
    {
        settingsTitleLabel_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"Resty 设置", WS_CHILD | WS_VISIBLE, 24, 20, 220, 34, hwnd, nullptr, instance_, nullptr);
        settingsSubtitleLabel_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"系统行为、提醒样式和规则文件都可以在这里维护。", WS_CHILD | WS_VISIBLE, 24, 56, 420, 22, hwnd, nullptr, instance_, nullptr);

        CreateWindowExW(0, L"BUTTON", L"常规", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 24, 92, 792, 84, hwnd, nullptr, instance_, nullptr);
        startupCheck_ = CreateWindowExW(0, L"BUTTON", L"开机自启动", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 48, 124, 160, 22, hwnd, reinterpret_cast<HMENU>(IdSettingsStartup), instance_, nullptr);
        trayCheck_ = CreateWindowExW(0, L"BUTTON", L"关闭主窗口时最小化到托盘", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 248, 124, 280, 22, hwnd, reinterpret_cast<HMENU>(IdSettingsTray), instance_, nullptr);

        CreateWindowExW(0, L"BUTTON", L"小休息提醒", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 24, 190, 792, 124, hwnd, nullptr, instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"透明度(40-255)", WS_CHILD | WS_VISIBLE, 48, 226, 120, 20, hwnd, nullptr, instance_, nullptr);
        shortOpacityEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 170, 220, 94, 32, hwnd, reinterpret_cast<HMENU>(IdSettingsShortOpacity), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"颜色(#RRGGBB)", WS_CHILD | WS_VISIBLE, 294, 226, 110, 20, hwnd, nullptr, instance_, nullptr);
        shortColorEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 406, 220, 128, 32, hwnd, reinterpret_cast<HMENU>(IdSettingsShortColor), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"提示词", WS_CHILD | WS_VISIBLE, 48, 266, 60, 20, hwnd, nullptr, instance_, nullptr);
        shortMessageEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 108, 260, 684, 32, hwnd, reinterpret_cast<HMENU>(IdSettingsShortMessage), instance_, nullptr);

        CreateWindowExW(0, L"BUTTON", L"大休息提醒", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 24, 328, 792, 124, hwnd, nullptr, instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"透明度(40-255)", WS_CHILD | WS_VISIBLE, 48, 364, 120, 20, hwnd, nullptr, instance_, nullptr);
        longOpacityEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 170, 358, 94, 32, hwnd, reinterpret_cast<HMENU>(IdSettingsLongOpacity), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"颜色(#RRGGBB)", WS_CHILD | WS_VISIBLE, 294, 364, 110, 20, hwnd, nullptr, instance_, nullptr);
        longColorEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 406, 358, 128, 32, hwnd, reinterpret_cast<HMENU>(IdSettingsLongColor), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"提示词", WS_CHILD | WS_VISIBLE, 48, 404, 60, 20, hwnd, nullptr, instance_, nullptr);
        longMessageEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 108, 398, 684, 32, hwnd, reinterpret_cast<HMENU>(IdSettingsLongMessage), instance_, nullptr);

        CreateWindowExW(0, L"BUTTON", L"休息规则", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 24, 466, 792, 256, hwnd, nullptr, instance_, nullptr);
        settingsHelpLabel_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"使用下方时间段编辑器维护规则，保存时仍会写回 .resty/data/rest.txt。", WS_CHILD | WS_VISIBLE, 48, 500, 700, 20, hwnd, nullptr, instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"已配置时间段", WS_CHILD | WS_VISIBLE, 48, 532, 130, 20, hwnd, nullptr, instance_, nullptr);
        ruleListBox_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            48, 558, 314, 120, hwnd, reinterpret_cast<HMENU>(IdRuleList), instance_, nullptr);

        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"休息类型", WS_CHILD | WS_VISIBLE, 390, 532, 80, 20, hwnd, nullptr, instance_, nullptr);
        ruleKindCombo_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            390, 556, 112, 240, hwnd, reinterpret_cast<HMENU>(IdRuleKind), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"规则类型", WS_CHILD | WS_VISIBLE, 522, 532, 80, 20, hwnd, nullptr, instance_, nullptr);
        ruleModeCombo_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            522, 556, 128, 240, hwnd, reinterpret_cast<HMENU>(IdRuleMode), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"时间(HH:MM)", WS_CHILD | WS_VISIBLE, 658, 532, 84, 20, hwnd, nullptr, instance_, nullptr);
        ruleTimeEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            658, 556, 84, 32, hwnd, reinterpret_cast<HMENU>(IdRuleTime), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"时长(分钟)", WS_CHILD | WS_VISIBLE, 748, 532, 72, 20, hwnd, nullptr, instance_, nullptr);
        ruleDurationEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            748, 556, 44, 32, hwnd, reinterpret_cast<HMENU>(IdRuleDuration), instance_, nullptr);

        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"固定日期", WS_CHILD | WS_VISIBLE, 390, 598, 80, 20, hwnd, nullptr, instance_, nullptr);
        ruleDateEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            390, 622, 170, 32, hwnd, reinterpret_cast<HMENU>(IdRuleDate), instance_, nullptr);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"星期重复", WS_CHILD | WS_VISIBLE, 584, 598, 80, 20, hwnd, nullptr, instance_, nullptr);

        ruleWeekdayChecks_[0] = CreateWindowExW(0, L"BUTTON", L"日", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 584, 624, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdaySun), instance_, nullptr);
        ruleWeekdayChecks_[1] = CreateWindowExW(0, L"BUTTON", L"一", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 620, 624, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdayMon), instance_, nullptr);
        ruleWeekdayChecks_[2] = CreateWindowExW(0, L"BUTTON", L"二", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 656, 624, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdayTue), instance_, nullptr);
        ruleWeekdayChecks_[3] = CreateWindowExW(0, L"BUTTON", L"三", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 692, 624, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdayWed), instance_, nullptr);
        ruleWeekdayChecks_[4] = CreateWindowExW(0, L"BUTTON", L"四", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 728, 624, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdayThu), instance_, nullptr);
        ruleWeekdayChecks_[5] = CreateWindowExW(0, L"BUTTON", L"五", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 584, 650, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdayFri), instance_, nullptr);
        ruleWeekdayChecks_[6] = CreateWindowExW(0, L"BUTTON", L"六", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 620, 650, 34, 20, hwnd, reinterpret_cast<HMENU>(IdRuleWeekdaySat), instance_, nullptr);

        ruleAddButton_ = CreateWindowExW(0, L"BUTTON", L"新增时间段", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 390, 682, 120, 36, hwnd, reinterpret_cast<HMENU>(IdRuleAdd), instance_, nullptr);
        ruleUpdateButton_ = CreateWindowExW(0, L"BUTTON", L"更新选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 520, 682, 120, 36, hwnd, reinterpret_cast<HMENU>(IdRuleUpdate), instance_, nullptr);
        ruleDeleteButton_ = CreateWindowExW(0, L"BUTTON", L"删除选中", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 650, 682, 120, 36, hwnd, reinterpret_cast<HMENU>(IdRuleDelete), instance_, nullptr);

        settingsPathHintLabel_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"支持每天、每周、固定日期；最终仍会按原文本格式保存。", WS_CHILD | WS_VISIBLE, 48, 686, 320, 20, hwnd, nullptr, instance_, nullptr);

        saveButton_ = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 616, 738, 92, 40, hwnd, reinterpret_cast<HMENU>(IdSettingsSave), instance_, nullptr);
        closeButton_ = CreateWindowExW(0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 720, 738, 92, 40, hwnd, reinterpret_cast<HMENU>(IdSettingsClose), instance_, nullptr);

        for (HWND child : { settingsTitleLabel_, settingsSubtitleLabel_, startupCheck_, trayCheck_, shortOpacityEdit_, shortColorEdit_, shortMessageEdit_,
            longOpacityEdit_, longColorEdit_, longMessageEdit_, settingsHelpLabel_, ruleListBox_, ruleKindCombo_, ruleModeCombo_, ruleTimeEdit_, ruleDurationEdit_, ruleDateEdit_,
            ruleWeekdayChecks_[0], ruleWeekdayChecks_[1], ruleWeekdayChecks_[2], ruleWeekdayChecks_[3], ruleWeekdayChecks_[4], ruleWeekdayChecks_[5], ruleWeekdayChecks_[6],
            ruleAddButton_, ruleUpdateButton_, ruleDeleteButton_, settingsPathHintLabel_, saveButton_, closeButton_ })
        {
            SetControlFont(child, baseFont_);
        }

        SetControlFont(settingsTitleLabel_, titleFont_);
        SetControlFont(settingsSubtitleLabel_, captionFont_);
        SetControlFont(settingsHelpLabel_, captionFont_);
        SetControlFont(settingsPathHintLabel_, captionFont_);

        ApplyEditTheme(shortOpacityEdit_, L"220");
        ApplyEditTheme(shortColorEdit_, L"#1E40AF");
        ApplyEditTheme(shortMessageEdit_, L"站起来，活动肩颈和眼睛。");
        ApplyEditTheme(longOpacityEdit_, L"235");
        ApplyEditTheme(longColorEdit_, L"#7F1D1D");
        ApplyEditTheme(longMessageEdit_, L"离开工位，走动几分钟，真正休息一下。");
        ApplyEditTheme(ruleTimeEdit_, L"15:30");
        ApplyEditTheme(ruleDurationEdit_, L"5");
        ApplyEditTheme(ruleDateEdit_, L"2026-05-01");
        SetWindowTheme(ruleListBox_, L"Explorer", nullptr);
        SetWindowTheme(ruleKindCombo_, L"Explorer", nullptr);
        SetWindowTheme(ruleModeCombo_, L"Explorer", nullptr);

        SendMessageW(ruleKindCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"小休息"));
        SendMessageW(ruleKindCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"大休息"));
        SendMessageW(ruleModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"每天"));
        SendMessageW(ruleModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"每周"));
        SendMessageW(ruleModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"固定日期"));
    }

    void DrawSettingsWindow(HWND hwnd) const
    {
        RECT client = {};
        GetClientRect(hwnd, &client);

        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(hwnd, &paint);
        FillSolidRect(dc, client, kWindowBackground);
        const RECT accentPill = { 24, 14, 132, 20 };
        DrawRoundedCard(dc, accentPill, kAccentColor, kAccentColor, 8);
        EndPaint(hwnd, &paint);
    }

    int GetWeekdayMaskFromEditor() const
    {
        int mask = 0;
        for (int day = 0; day < 7; ++day)
        {
            if (SendMessageW(ruleWeekdayChecks_[day], BM_GETCHECK, 0, 0) == BST_CHECKED)
            {
                mask |= (1 << day);
            }
        }
        return mask;
    }

    void SetWeekdayMaskToEditor(int mask)
    {
        for (int day = 0; day < 7; ++day)
        {
            SendMessageW(ruleWeekdayChecks_[day], BM_SETCHECK, (mask & (1 << day)) != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }

    void UpdateRuleEditorEnabledState()
    {
        const int modeIndex = static_cast<int>(SendMessageW(ruleModeCombo_, CB_GETCURSEL, 0, 0));
        const bool isWeekly = modeIndex == 1;
        const bool isDate = modeIndex == 2;
        EnableWindow(ruleDateEdit_, isDate ? TRUE : FALSE);
        for (HWND checkbox : ruleWeekdayChecks_)
        {
            EnableWindow(checkbox, isWeekly ? TRUE : FALSE);
        }
    }

    void ResetRuleEditor()
    {
        SendMessageW(ruleKindCombo_, CB_SETCURSEL, 0, 0);
        SendMessageW(ruleModeCombo_, CB_SETCURSEL, 1, 0);
        SetWindowTextW(ruleTimeEdit_, L"10:30");
        SetWindowTextW(ruleDurationEdit_, std::to_wstring(resty::GetDefaultDurationMinutes(resty::RestKind::Short)).c_str());
        SetWindowTextW(ruleDateEdit_, L"");
        SetWeekdayMaskToEditor((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
        UpdateRuleEditorEnabledState();
    }

    int GetRuleSortPriority(resty::RuleMode mode) const
    {
        switch (mode)
        {
        case resty::RuleMode::Date:
            return 0;
        case resty::RuleMode::Daily:
            return 1;
        case resty::RuleMode::Weekly:
            return 2;
        default:
            return 3;
        }
    }

    bool AreRulesEquivalent(const resty::ScheduleRule& left, const resty::ScheduleRule& right) const
    {
        return left.kind == right.kind
            && left.mode == right.mode
            && left.hour == right.hour
            && left.minute == right.minute
            && left.durationMinutes == right.durationMinutes
            && left.weekdaysMask == right.weekdaysMask
            && left.year == right.year
            && left.month == right.month
            && left.day == right.day;
    }

    void SortRuleDrafts()
    {
        std::stable_sort(ruleDrafts_.begin(), ruleDrafts_.end(), [this](const resty::ScheduleRule& left, const resty::ScheduleRule& right)
        {
            const int leftPriority = GetRuleSortPriority(left.mode);
            const int rightPriority = GetRuleSortPriority(right.mode);
            if (leftPriority != rightPriority)
            {
                return leftPriority < rightPriority;
            }

            if (left.hour != right.hour)
            {
                return left.hour < right.hour;
            }
            if (left.minute != right.minute)
            {
                return left.minute < right.minute;
            }

            if (left.mode == resty::RuleMode::Date)
            {
                if (left.year != right.year)
                {
                    return left.year < right.year;
                }
                if (left.month != right.month)
                {
                    return left.month < right.month;
                }
                if (left.day != right.day)
                {
                    return left.day < right.day;
                }
            }

            if (left.mode == resty::RuleMode::Weekly && left.weekdaysMask != right.weekdaysMask)
            {
                return left.weekdaysMask < right.weekdaysMask;
            }

            if (left.kind != right.kind)
            {
                return static_cast<int>(left.kind) < static_cast<int>(right.kind);
            }

            return left.durationMinutes < right.durationMinutes;
        });
    }

    int FindRuleIndex(const resty::ScheduleRule& target) const
    {
        for (int index = 0; index < static_cast<int>(ruleDrafts_.size()); ++index)
        {
            if (AreRulesEquivalent(ruleDrafts_[index], target))
            {
                return index;
            }
        }
        return -1;
    }

    void RefreshRuleList()
    {
        SendMessageW(ruleListBox_, LB_RESETCONTENT, 0, 0);
        for (const auto& rule : ruleDrafts_)
        {
            SendMessageW(ruleListBox_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(resty::FormatRuleSummary(rule).c_str()));
        }
    }

    void LoadRuleIntoEditor(int index)
    {
        if (index < 0 || index >= static_cast<int>(ruleDrafts_.size()))
        {
            ResetRuleEditor();
            return;
        }

        const auto& rule = ruleDrafts_[index];
        SendMessageW(ruleKindCombo_, CB_SETCURSEL, rule.kind == resty::RestKind::Short ? 0 : 1, 0);
        SendMessageW(ruleModeCombo_, CB_SETCURSEL, rule.mode == resty::RuleMode::Daily ? 0 : (rule.mode == resty::RuleMode::Weekly ? 1 : 2), 0);

        wchar_t timeBuffer[16] = {};
        swprintf_s(timeBuffer, L"%02d:%02d", rule.hour, rule.minute);
        SetWindowTextW(ruleTimeEdit_, timeBuffer);
        SetWindowTextW(ruleDurationEdit_, std::to_wstring(resty::ClampDurationMinutes(rule.durationMinutes)).c_str());

        wchar_t dateBuffer[24] = {};
        if (rule.mode == resty::RuleMode::Date)
        {
            swprintf_s(dateBuffer, L"%04d-%02d-%02d", rule.year, rule.month, rule.day);
        }
        SetWindowTextW(ruleDateEdit_, dateBuffer);
        SetWeekdayMaskToEditor(rule.weekdaysMask);
        UpdateRuleEditorEnabledState();
    }

    bool BuildRuleFromEditor(resty::ScheduleRule& rule, std::wstring& error) const
    {
        const int kindIndex = static_cast<int>(SendMessageW(ruleKindCombo_, CB_GETCURSEL, 0, 0));
        const int modeIndex = static_cast<int>(SendMessageW(ruleModeCombo_, CB_GETCURSEL, 0, 0));
        if (kindIndex < 0 || modeIndex < 0)
        {
            error = L"请选择休息类型与规则类型。";
            return false;
        }

        std::wstring line = kindIndex == 0 ? L"short|" : L"long|";
        const std::wstring timeText = resty::Trim(GetWindowTextString(ruleTimeEdit_));
        const std::wstring durationText = resty::Trim(GetWindowTextString(ruleDurationEdit_));
        if (timeText.empty())
        {
            error = L"时间不能为空，格式例如 10:30。";
            return false;
        }
        if (durationText.empty())
        {
            error = L"休息时长不能为空，单位为分钟。";
            return false;
        }

        int durationMinutes = 0;
        try
        {
            durationMinutes = std::stoi(durationText);
        }
        catch (...)
        {
            error = L"休息时长必须是 1 到 240 之间的整数分钟。";
            return false;
        }
        if (durationMinutes < 1 || durationMinutes > 240)
        {
            error = L"休息时长必须是 1 到 240 之间的整数分钟。";
            return false;
        }

        if (modeIndex == 0)
        {
            line += L"daily|" + timeText + L"|" + std::to_wstring(durationMinutes);
        }
        else if (modeIndex == 1)
        {
            const wchar_t* labels[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
            const int mask = GetWeekdayMaskFromEditor();
            if (mask == 0)
            {
                error = L"每周规则至少选择一个星期。";
                return false;
            }

            line += L"weekly|";
            bool first = true;
            for (int day = 0; day < 7; ++day)
            {
                if ((mask & (1 << day)) != 0)
                {
                    if (!first)
                    {
                        line += L",";
                    }
                    line += labels[day];
                    first = false;
                }
            }
            line += L"|" + timeText + L"|" + std::to_wstring(durationMinutes);
        }
        else
        {
            const std::wstring dateText = resty::Trim(GetWindowTextString(ruleDateEdit_));
            if (dateText.empty())
            {
                error = L"固定日期规则需要填写日期，格式例如 2026-05-01。";
                return false;
            }
            line += L"date|" + dateText + L"|" + timeText + L"|" + std::to_wstring(durationMinutes);
        }

        return resty::ParseRuleLine(line, rule, error);
    }

    void PopulateSettingsControls()
    {
        if (settingsWindow_ == nullptr)
        {
            return;
        }

        SendMessageW(startupCheck_, BM_SETCHECK, settings_.launchAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(trayCheck_, BM_SETCHECK, settings_.minimizeToTray ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(shortOpacityEdit_, std::to_wstring(settings_.shortRest.opacity).c_str());
        SetWindowTextW(shortColorEdit_, resty::FormatColor(settings_.shortRest.color).c_str());
        SetWindowTextW(shortMessageEdit_, settings_.shortRest.message.c_str());
        SetWindowTextW(longOpacityEdit_, std::to_wstring(settings_.longRest.opacity).c_str());
        SetWindowTextW(longColorEdit_, resty::FormatColor(settings_.longRest.color).c_str());
        SetWindowTextW(longMessageEdit_, settings_.longRest.message.c_str());

        ruleDrafts_ = settings_.rules;
        SortRuleDrafts();
        RefreshRuleList();
        if (!ruleDrafts_.empty())
        {
            SendMessageW(ruleListBox_, LB_SETCURSEL, 0, 0);
            LoadRuleIntoEditor(0);
        }
        else
        {
            ResetRuleEditor();
        }
    }

    bool SaveSettingsFromUi()
    {
        resty::AppSettings updated = settings_;
        updated.launchAtStartup = SendMessageW(startupCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        updated.minimizeToTray = SendMessageW(trayCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;

        try
        {
            updated.shortRest.opacity = resty::ClampOpacity(std::stoi(resty::Trim(GetWindowTextString(shortOpacityEdit_))));
            updated.longRest.opacity = resty::ClampOpacity(std::stoi(resty::Trim(GetWindowTextString(longOpacityEdit_))));
        }
        catch (...)
        {
            MessageBoxW(settingsWindow_, L"透明度必须是 40 到 255 之间的整数。", L"保存失败", MB_ICONWARNING);
            return false;
        }

        updated.shortRest.message = resty::Trim(GetWindowTextString(shortMessageEdit_));
        updated.longRest.message = resty::Trim(GetWindowTextString(longMessageEdit_));
        if (updated.shortRest.message.empty() || updated.longRest.message.empty())
        {
            MessageBoxW(settingsWindow_, L"提示词不能为空。", L"保存失败", MB_ICONWARNING);
            return false;
        }

        if (!resty::ParseColor(GetWindowTextString(shortColorEdit_), updated.shortRest.color) || !resty::ParseColor(GetWindowTextString(longColorEdit_), updated.longRest.color))
        {
            MessageBoxW(settingsWindow_, L"颜色必须使用 #RRGGBB 格式，例如 #1F3B8A。", L"保存失败", MB_ICONWARNING);
            return false;
        }

        if (ruleDrafts_.empty())
        {
            MessageBoxW(settingsWindow_, L"至少保留一条休息规则。", L"保存失败", MB_ICONWARNING);
            return false;
        }

        updated.rules = ruleDrafts_;
        settings_ = std::move(updated);
        resty::SaveSettings(settings_);
        resty::SetAutoStart(settings_.launchAtStartup);
        UpdateHomeDisplay();
        MessageBoxW(settingsWindow_, L"设置已保存。", L"Resty", MB_ICONINFORMATION);
        return true;
    }

    void OpenSettingsWindow()
    {
        EnsureSettingsWindow();
        PopulateSettingsControls();
        CenterWindowOnScreen(settingsWindow_);
        ShowWindow(settingsWindow_, SW_SHOWNORMAL);
        SetForegroundWindow(settingsWindow_);
    }

    void UpdateHomeDisplay()
    {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        const resty::ScheduledRest next = resty::FindNextRest(settings_, now);
        const __time64_t current = ToTimeT(now);

        if (next.valid)
        {
            SetWindowTextSafe(countdownValue_, resty::FormatCountdown(next.when - current));
            SetWindowTextSafe(nextValue_, next.description);
        }
        else
        {
            SetWindowTextSafe(countdownValue_, L"--:--:--");
            SetWindowTextSafe(nextValue_, L"未找到未来休息规则");
        }

        const std::wstring summary = L"已加载 " + std::to_wstring(settings_.rules.size()) + L" 条规则\r\n配置：" + resty::GetConfigPath() + L"\r\n规则：" + resty::GetRestDataPath();
        SetWindowTextSafe(summaryValue_, summary);
        CheckDueReminders(now);
    }

    void CheckDueReminders(const SYSTEMTIME& now)
    {
        const resty::ScheduleRule* dueRule = nullptr;
        for (const auto& rule : settings_.rules)
        {
            if (!resty::IsRuleDueNow(rule, now))
            {
                continue;
            }
            if (dueRule == nullptr || rule.kind == resty::RestKind::Long)
            {
                dueRule = &rule;
            }
        }

        if (dueRule == nullptr)
        {
            return;
        }

        const std::wstring dueKey = resty::BuildDueKey(*dueRule, now);
        if (dueKey == lastDueKey_)
        {
            return;
        }

        lastDueKey_ = dueKey;
        ShowOverlay(dueRule->kind, resty::ClampDurationMinutes(dueRule->durationMinutes) * 60, false, resty::ClampDurationMinutes(dueRule->durationMinutes));
    }

    void ShowOverlay(resty::RestKind kind, int durationSeconds, bool previewMode, int plannedDurationMinutes)
    {
        overlayKind_ = kind;
        overlayStyle_ = kind == resty::RestKind::Short ? settings_.shortRest : settings_.longRest;
        overlayDurationSeconds_ = std::max(1, durationSeconds);
        overlayPlannedDurationMinutes_ = std::max(1, plannedDurationMinutes);
        overlayPreviewMode_ = previewMode;

        SYSTEMTIME now = {};
        GetLocalTime(&now);
        overlayDeadline_ = ToTimeT(now) + overlayDurationSeconds_;

        if (overlayWindow_ != nullptr)
        {
            DestroyWindow(overlayWindow_);
            overlayWindow_ = nullptr;
        }

        const bool fullScreen = kind == resty::RestKind::Long;
        const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        int x = screenX;
        int y = screenY;
        int width = screenWidth;
        int height = screenHeight;
        if (!fullScreen)
        {
            width = std::max(700, std::min(980, screenWidth - 120));
            height = std::max(420, std::min(500, screenHeight - 120));
            x = screenX + (screenWidth - width) / 2;
            y = screenY + (screenHeight - height) / 2;
        }

        overlayWindow_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            kOverlayClassName,
            L"RestyOverlay",
            WS_POPUP,
            x,
            y,
            width,
            height,
            nullptr,
            nullptr,
            instance_,
            this);

        if (overlayWindow_ == nullptr)
        {
            return;
        }

        SetLayeredWindowAttributes(overlayWindow_, 0, static_cast<BYTE>(resty::ClampOpacity(overlayStyle_.opacity)), LWA_ALPHA);
        ShowWindow(overlayWindow_, SW_SHOWNORMAL);
        UpdateWindow(overlayWindow_);
        SetForegroundWindow(overlayWindow_);
        SetTimer(overlayWindow_, kOverlayTimerId, 1000, nullptr);
    }

    void DrawOverlay(HWND hwnd) const
    {
        RECT client = {};
        GetClientRect(hwnd, &client);

        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(hwnd, &paint);
        HBRUSH background = CreateSolidBrush(overlayStyle_.color);
        FillRect(dc, &client, background);
        DeleteObject(background);

        const COLORREF textColor = ChooseTextColor(overlayStyle_.color);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor);

        SYSTEMTIME now = {};
        GetLocalTime(&now);
        const __time64_t remaining = std::max(static_cast<__time64_t>(0), overlayDeadline_ - ToTimeT(now));
        const std::wstring title = overlayKind_ == resty::RestKind::Short ? L"小休息时间" : L"大休息时间";
        const std::wstring countdownText = (overlayPreviewMode_ ? L"预览剩余 " : L"本次休息剩余 ") + resty::FormatCountdown(remaining);
        const std::wstring footer = (overlayPreviewMode_ ? L"预览模式" : (L"建议休息 " + std::to_wstring(overlayPlannedDurationMinutes_) + L" 分钟")) + L" · 点击任意位置关闭";

        RECT panel = client;
        if (overlayKind_ == resty::RestKind::Long)
        {
            panel.left += client.right / 8;
            panel.right -= client.right / 8;
            panel.top += client.bottom / 4;
            panel.bottom -= client.bottom / 4;
        }
        else
        {
            panel.left += 40;
            panel.right -= 40;
            panel.top += 30;
            panel.bottom -= 30;
        }

        HPEN borderPen = CreatePen(PS_SOLID, 2, textColor);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, 28, 28);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);

        const int titleHeight = overlayKind_ == resty::RestKind::Short ? 64 : 74;
        const int countdownTop = overlayKind_ == resty::RestKind::Short ? 88 : 100;
        const int countdownHeight = overlayKind_ == resty::RestKind::Short ? 48 : 56;
        const int progressTop = overlayKind_ == resty::RestKind::Short ? 150 : 176;
        const int messageTop = overlayKind_ == resty::RestKind::Short ? 186 : 214;
        const int footerHeight = overlayKind_ == resty::RestKind::Short ? 32 : 40;
        const int bottomPadding = overlayKind_ == resty::RestKind::Short ? 58 : 74;

        RECT titleRect = panel;
        titleRect.top += 20;
        titleRect.bottom = titleRect.top + titleHeight;
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, overlayTitleFont_));
        DrawTextW(dc, title.c_str(), -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT countdownRect = panel;
        countdownRect.top += countdownTop;
        countdownRect.bottom = countdownRect.top + countdownHeight;
        SelectObject(dc, overlayCountdownFont_);
        DrawTextW(dc, countdownText.c_str(), -1, &countdownRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const int availableBarWidth = static_cast<int>(panel.right - panel.left) - 120;
        const int barWidth = std::min(360, availableBarWidth);
        RECT progressTrack = { (panel.left + panel.right - barWidth) / 2, panel.top + progressTop, (panel.left + panel.right + barWidth) / 2, panel.top + progressTop + 14 };
        DrawRoundedCard(dc, progressTrack, RGB(255, 255, 255), RGB(255, 255, 255), 10);
        if (overlayDurationSeconds_ > 0 && remaining > 0)
        {
            RECT progressFill = progressTrack;
            progressFill.right = progressFill.left + static_cast<int>((progressTrack.right - progressTrack.left) * (static_cast<double>(remaining) / overlayDurationSeconds_));
            if (progressFill.right < progressFill.left + 10)
            {
                progressFill.right = std::min(progressTrack.right, progressFill.left + 10);
            }
            DrawRoundedCard(dc, progressFill, textColor, textColor, 10);
        }

        RECT messageRect = panel;
        messageRect.left += 40;
        messageRect.right -= 40;
        messageRect.top += messageTop;
        messageRect.bottom -= bottomPadding;
        SelectObject(dc, overlayMessageFont_);
        DrawTextW(dc, overlayStyle_.message.c_str(), -1, &messageRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);

        RECT footerRect = panel;
        footerRect.bottom -= 22;
        footerRect.top = footerRect.bottom - footerHeight;
        SelectObject(dc, baseFont_);
        DrawTextW(dc, footer.c_str(), -1, &footerRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);

        EndPaint(hwnd, &paint);
    }

    bool IsPrimaryButton(UINT controlId) const
    {
        return controlId == IdOpenSettings || controlId == IdSettingsSave || controlId == IdPreviewLong;
    }

    LRESULT HandleDrawItem(LPARAM lParam) const
    {
        const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (drawItem == nullptr || drawItem->CtlType != ODT_BUTTON)
        {
            return FALSE;
        }

        const bool isPrimary = IsPrimaryButton(drawItem->CtlID);
        const bool isPressed = (drawItem->itemState & ODS_SELECTED) != 0;
        const bool isFocused = (drawItem->itemState & ODS_FOCUS) != 0;
        const bool isDisabled = (drawItem->itemState & ODS_DISABLED) != 0;

        COLORREF fillColor = isPrimary ? RGB(59, 130, 246) : RGB(255, 255, 255);
        COLORREF borderColor = isPrimary ? RGB(37, 99, 235) : RGB(203, 213, 225);
        COLORREF textColor = isPrimary ? RGB(255, 255, 255) : kPrimaryText;

        if (isPressed)
        {
            fillColor = isPrimary ? RGB(37, 99, 235) : RGB(241, 245, 249);
        }
        if (isDisabled)
        {
            fillColor = RGB(226, 232, 240);
            borderColor = RGB(203, 213, 225);
            textColor = RGB(148, 163, 184);
        }

        RECT rect = drawItem->rcItem;
        HDC dc = drawItem->hDC;
        SetBkMode(dc, TRANSPARENT);
        DrawRoundedCard(dc, rect, fillColor, borderColor, 16);

        if (isFocused)
        {
            RECT focusRect = rect;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(dc, &focusRect);
        }

        wchar_t text[64] = {};
        GetWindowTextW(drawItem->hwndItem, text, static_cast<int>(std::size(text)));
        SetTextColor(dc, textColor);
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, sectionFont_));
        DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);
        return TRUE;
    }

    LRESULT HandleMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            mainWindow_ = hwnd;
            CreateMainControls(hwnd);
            CenterWindowOnScreen(hwnd);
            SetTimer(hwnd, kMainTimerId, 1000, nullptr);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            DrawMainWindow(hwnd);
            return 0;

        case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, OPAQUE);

            if (control == mainTitleLabel_)
            {
                SetTextColor(dc, kPrimaryText);
                SetBkColor(dc, kWindowBackground);
                return reinterpret_cast<INT_PTR>(windowBrush_);
            }
            else if (control == mainSubtitleLabel_)
            {
                SetTextColor(dc, kSecondaryText);
                SetBkColor(dc, kWindowBackground);
                return reinterpret_cast<INT_PTR>(windowBrush_);
            }
            else if (control == countdownLabel_ || control == nextLabel_ || control == storageLabel_ || control == actionLabel_)
            {
                SetTextColor(dc, kPrimaryText);
                SetBkColor(dc, kCardBackground);
                return reinterpret_cast<INT_PTR>(cardBrush_);
            }
            else if (control == countdownValue_)
            {
                SetTextColor(dc, kAccentColor);
                SetBkColor(dc, kCardBackground);
                return reinterpret_cast<INT_PTR>(cardBrush_);
            }
            else if (control == nextValue_)
            {
                SetTextColor(dc, kPrimaryText);
                SetBkColor(dc, kCardBackground);
                return reinterpret_cast<INT_PTR>(cardBrush_);
            }
            else
            {
                SetTextColor(dc, kSecondaryText);
                SetBkColor(dc, kCardBackground);
                return reinterpret_cast<INT_PTR>(cardBrush_);
            }
        }

        case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, OPAQUE);
            SetTextColor(dc, kPrimaryText);
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<INT_PTR>(editBrush_);
        }

        case WM_CTLCOLORLISTBOX:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, OPAQUE);
            SetTextColor(dc, kPrimaryText);
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<INT_PTR>(editBrush_);
        }

        case WM_DRAWITEM:
            return HandleDrawItem(lParam);

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IdOpenSettings:
            case IdTrayShowSettings:
                OpenSettingsWindow();
                return 0;
            case IdPreviewShort:
                ShowOverlay(resty::RestKind::Short, kShortOverlaySeconds, true, resty::GetDefaultDurationMinutes(resty::RestKind::Short));
                return 0;
            case IdPreviewLong:
                ShowOverlay(resty::RestKind::Long, kLongOverlaySeconds, true, resty::GetDefaultDurationMinutes(resty::RestKind::Long));
                return 0;
            case IdTrayShowMain:
                ShowMainWindow();
                return 0;
            case IdTrayExit:
                isExiting_ = true;
                DestroyWindow(hwnd);
                return 0;
            default:
                break;
            }
            break;

        case WM_TIMER:
            if (wParam == kMainTimerId)
            {
                UpdateHomeDisplay();
                return 0;
            }
            break;

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED && settings_.minimizeToTray)
            {
                HideMainWindow();
                return 0;
            }
            break;

        case WM_CLOSE:
            if (!isExiting_ && settings_.minimizeToTray)
            {
                HideMainWindow();
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            {
                ShowTrayMenu();
                return 0;
            }
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK)
            {
                ShowMainWindow();
                return 0;
            }
            break;

        case WM_DESTROY:
            KillTimer(hwnd, kMainTimerId);
            RemoveTrayIcon();
            if (overlayWindow_ != nullptr)
            {
                DestroyWindow(overlayWindow_);
            }
            if (settingsWindow_ != nullptr)
            {
                DestroyWindow(settingsWindow_);
            }
            DestroyFonts();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleSettingsWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            settingsWindow_ = hwnd;
            CreateSettingsControls(hwnd);
            PopulateSettingsControls();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            DrawSettingsWindow(hwnd);
            return 0;

        case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, OPAQUE);
            if (control == settingsTitleLabel_)
            {
                SetTextColor(dc, kPrimaryText);
                SetBkColor(dc, kWindowBackground);
            }
            else
            {
                SetTextColor(dc, kSecondaryText);
                SetBkColor(dc, kWindowBackground);
            }
            return reinterpret_cast<INT_PTR>(windowBrush_);
        }

        case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, OPAQUE);
            SetTextColor(dc, kPrimaryText);
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<INT_PTR>(editBrush_);
        }

        case WM_CTLCOLORLISTBOX:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, OPAQUE);
            SetTextColor(dc, kPrimaryText);
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<INT_PTR>(editBrush_);
        }

        case WM_DRAWITEM:
            return HandleDrawItem(lParam);

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IdRuleList:
                if (HIWORD(wParam) == LBN_SELCHANGE)
                {
                    LoadRuleIntoEditor(static_cast<int>(SendMessageW(ruleListBox_, LB_GETCURSEL, 0, 0)));
                    return 0;
                }
                break;
            case IdRuleMode:
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    UpdateRuleEditorEnabledState();
                    return 0;
                }
                break;
            case IdRuleAdd:
            {
                resty::ScheduleRule rule;
                std::wstring error;
                if (!BuildRuleFromEditor(rule, error))
                {
                    MessageBoxW(hwnd, error.c_str(), L"新增失败", MB_ICONWARNING);
                    return 0;
                }
                ruleDrafts_.push_back(rule);
                SortRuleDrafts();
                RefreshRuleList();
                const int index = FindRuleIndex(rule);
                SendMessageW(ruleListBox_, LB_SETCURSEL, index, 0);
                LoadRuleIntoEditor(index);
                return 0;
            }
            case IdRuleUpdate:
            {
                const int index = static_cast<int>(SendMessageW(ruleListBox_, LB_GETCURSEL, 0, 0));
                if (index < 0 || index >= static_cast<int>(ruleDrafts_.size()))
                {
                    MessageBoxW(hwnd, L"请先在左侧选择一个时间段。", L"更新失败", MB_ICONWARNING);
                    return 0;
                }
                resty::ScheduleRule rule;
                std::wstring error;
                if (!BuildRuleFromEditor(rule, error))
                {
                    MessageBoxW(hwnd, error.c_str(), L"更新失败", MB_ICONWARNING);
                    return 0;
                }
                ruleDrafts_[index] = rule;
                SortRuleDrafts();
                RefreshRuleList();
                const int sortedIndex = FindRuleIndex(rule);
                SendMessageW(ruleListBox_, LB_SETCURSEL, sortedIndex, 0);
                LoadRuleIntoEditor(sortedIndex);
                return 0;
            }
            case IdRuleDelete:
            {
                const int index = static_cast<int>(SendMessageW(ruleListBox_, LB_GETCURSEL, 0, 0));
                if (index < 0 || index >= static_cast<int>(ruleDrafts_.size()))
                {
                    MessageBoxW(hwnd, L"请先在左侧选择一个时间段。", L"删除失败", MB_ICONWARNING);
                    return 0;
                }
                ruleDrafts_.erase(ruleDrafts_.begin() + index);
                RefreshRuleList();
                if (!ruleDrafts_.empty())
                {
                    const int nextIndex = std::min(index, static_cast<int>(ruleDrafts_.size()) - 1);
                    SendMessageW(ruleListBox_, LB_SETCURSEL, nextIndex, 0);
                    LoadRuleIntoEditor(nextIndex);
                }
                else
                {
                    ResetRuleEditor();
                }
                return 0;
            }
            case IdSettingsSave:
                if (SaveSettingsFromUi())
                {
                    ShowWindow(hwnd, SW_HIDE);
                }
                return 0;
            case IdSettingsClose:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            default:
                break;
            }
            break;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            settingsWindow_ = nullptr;
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleOverlayWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            DrawOverlay(hwnd);
            return 0;

        case WM_TIMER:
            if (wParam == kOverlayTimerId)
            {
                SYSTEMTIME now = {};
                GetLocalTime(&now);
                if (ToTimeT(now) >= overlayDeadline_)
                {
                    DestroyWindow(hwnd);
                }
                else
                {
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0;
            }
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            DestroyWindow(hwnd);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_DESTROY:
            KillTimer(hwnd, kOverlayTimerId);
            if (overlayWindow_ == hwnd)
            {
                overlayWindow_ = nullptr;
            }
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void DestroyFonts()
    {
        if (baseFont_ != nullptr)
        {
            DeleteObject(baseFont_);
            baseFont_ = nullptr;
        }
        if (titleFont_ != nullptr)
        {
            DeleteObject(titleFont_);
            titleFont_ = nullptr;
        }
        if (sectionFont_ != nullptr)
        {
            DeleteObject(sectionFont_);
            sectionFont_ = nullptr;
        }
        if (captionFont_ != nullptr)
        {
            DeleteObject(captionFont_);
            captionFont_ = nullptr;
        }
        if (countdownFont_ != nullptr)
        {
            DeleteObject(countdownFont_);
            countdownFont_ = nullptr;
        }
        if (overlayTitleFont_ != nullptr)
        {
            DeleteObject(overlayTitleFont_);
            overlayTitleFont_ = nullptr;
        }
        if (overlayCountdownFont_ != nullptr)
        {
            DeleteObject(overlayCountdownFont_);
            overlayCountdownFont_ = nullptr;
        }
        if (overlayMessageFont_ != nullptr)
        {
            DeleteObject(overlayMessageFont_);
            overlayMessageFont_ = nullptr;
        }
        if (windowBrush_ != nullptr)
        {
            DeleteObject(windowBrush_);
            windowBrush_ = nullptr;
        }
        if (cardBrush_ != nullptr)
        {
            DeleteObject(cardBrush_);
            cardBrush_ = nullptr;
        }
        if (editBrush_ != nullptr)
        {
            DeleteObject(editBrush_);
            editBrush_ = nullptr;
        }
    }

    HINSTANCE instance_ = nullptr;
    HWND mainWindow_ = nullptr;
    HWND settingsWindow_ = nullptr;
    HWND overlayWindow_ = nullptr;

    HWND mainTitleLabel_ = nullptr;
    HWND mainSubtitleLabel_ = nullptr;
    HWND countdownLabel_ = nullptr;
    HWND countdownValue_ = nullptr;
    HWND countdownHintLabel_ = nullptr;
    HWND nextLabel_ = nullptr;
    HWND nextValue_ = nullptr;
    HWND nextHintLabel_ = nullptr;
    HWND storageLabel_ = nullptr;
    HWND summaryValue_ = nullptr;
    HWND actionLabel_ = nullptr;
    HWND openSettingsButton_ = nullptr;
    HWND previewShortButton_ = nullptr;
    HWND previewLongButton_ = nullptr;

    HWND settingsTitleLabel_ = nullptr;
    HWND settingsSubtitleLabel_ = nullptr;
    HWND settingsHelpLabel_ = nullptr;
    HWND settingsPathHintLabel_ = nullptr;
    HWND startupCheck_ = nullptr;
    HWND trayCheck_ = nullptr;
    HWND shortOpacityEdit_ = nullptr;
    HWND shortMessageEdit_ = nullptr;
    HWND shortColorEdit_ = nullptr;
    HWND longOpacityEdit_ = nullptr;
    HWND longMessageEdit_ = nullptr;
    HWND longColorEdit_ = nullptr;
    HWND ruleListBox_ = nullptr;
    HWND ruleKindCombo_ = nullptr;
    HWND ruleModeCombo_ = nullptr;
    HWND ruleTimeEdit_ = nullptr;
    HWND ruleDurationEdit_ = nullptr;
    HWND ruleDateEdit_ = nullptr;
    HWND ruleWeekdayChecks_[7] = {};
    HWND ruleAddButton_ = nullptr;
    HWND ruleUpdateButton_ = nullptr;
    HWND ruleDeleteButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND closeButton_ = nullptr;

    HFONT baseFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT sectionFont_ = nullptr;
    HFONT captionFont_ = nullptr;
    HFONT countdownFont_ = nullptr;
    HFONT overlayTitleFont_ = nullptr;
    HFONT overlayCountdownFont_ = nullptr;
    HFONT overlayMessageFont_ = nullptr;
    HBRUSH windowBrush_ = CreateSolidBrush(kWindowBackground);
    HBRUSH cardBrush_ = CreateSolidBrush(kCardBackground);
    HBRUSH editBrush_ = CreateSolidBrush(RGB(255, 255, 255));

    NOTIFYICONDATAW trayIcon_ = {};
    resty::AppSettings settings_;
    std::vector<resty::ScheduleRule> ruleDrafts_;
    resty::RestStyle overlayStyle_;
    resty::RestKind overlayKind_ = resty::RestKind::Short;
    __time64_t overlayDeadline_ = 0;
    int overlayDurationSeconds_ = 0;
    int overlayPlannedDurationMinutes_ = 0;
    bool overlayPreviewMode_ = false;
    std::wstring lastDueKey_;
    bool isExiting_ = false;
};

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int nCmdShow)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\RestySingleInstanceMutex");
    if (mutex == nullptr)
    {
        return 0;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(kMainClassName, nullptr);
        if (existing != nullptr)
        {
            ShowWindow(existing, SW_SHOWNORMAL);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    RestyApp app;
    if (!app.Initialize(instance))
    {
        CloseHandle(mutex);
        return 0;
    }

    const int result = app.Run(nCmdShow);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
