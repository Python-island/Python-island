#include "trayicon.h"
#include "logging.h"

#ifdef _WIN32
// ============================================================
//  Windows implementation
// ============================================================
#include <shellapi.h>
#include <strsafe.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

TrayIcon g_trayIcon;

// Load icon from PNG file using GDI+
static HICON LoadIconFromPNG(const wchar_t* path) {
    HICON hIcon = nullptr;

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    try {
        Bitmap bitmap(path);
        if (bitmap.GetLastStatus() == Ok) {
            bitmap.GetHICON(&hIcon);
        }
    } catch (...) {
        // Exception handling
    }

    GdiplusShutdown(gdiplusToken);
    return hIcon;
}

TrayIcon::TrayIcon() {}

TrayIcon::~TrayIcon() {
    Shutdown();
}

bool TrayIcon::Initialize(HWND hwnd, UINT callbackMessage) {
    this->hwnd = hwnd;
    this->callbackMsg = callbackMessage;

    // Set up NOTIFYICONDATA
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = callbackMessage;

    // Try to load icon from PNG file
    HICON hIcon = LoadIconFromPNG(L"icon.png");
    if (hIcon) {
        nid.hIcon = hIcon;
    } else {
        nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }

    StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), TEXT("DynamicIsland System Monitor"));

    // Add tray icon
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        if (hIcon) DestroyIcon(hIcon);
        return false;
    }

    // Set modern notification version
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    CreateMenu();
    return true;
}

void TrayIcon::Shutdown() {
    DestroyMenu();
    if (hwnd) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        if (nid.hIcon) {
            DestroyIcon(nid.hIcon);
            nid.hIcon = nullptr;
        }
        hwnd = nullptr;
    }
}

void TrayIcon::CreateMenu() {
    hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::EXPAND_PANEL, TEXT("Expand Panel"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::SETTINGS, TEXT("Settings"));

    // Performance mode sub-menu
    hPerfMenu = CreatePopupMenu();
    AppendMenu(hPerfMenu, MF_STRING, (UINT)TrayCommand::PERF_POWER_SAVE, TEXT("Power Save (5s)"));
    AppendMenu(hPerfMenu, MF_STRING, (UINT)TrayCommand::PERF_BALANCED, TEXT("Balanced (1s)"));
    AppendMenu(hPerfMenu, MF_STRING, (UINT)TrayCommand::PERF_PERFORMANCE, TEXT("Performance (0.5s)"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hPerfMenu, TEXT("Performance Mode"));

    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::STARTUP_TOGGLE, TEXT("Start with Windows"));
    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::EXIT, TEXT("Exit"));
}

void TrayIcon::DestroyMenu() {
    if (hMenu) {
        ::DestroyMenu(hMenu);
        hMenu = nullptr;
    }
    hPerfMenu = nullptr;
    hPosMenu = nullptr;
}

void TrayIcon::ShowContextMenu() {
    if (!hMenu || !hwnd) return;

    UpdateMenuState(isVisible, isExpanded, currentPerfMode, currentPosition, startupEnabled);

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);

    // Required PostMessage to fix menu-dismiss bug
    PostMessage(hwnd, WM_NULL, 0, 0);
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    if (LOWORD(lParam) == WM_RBUTTONUP) {
        ShowContextMenu();
    } else if (LOWORD(lParam) == WM_LBUTTONUP) {
        if (onShowHide) onShowHide();
    } else if (LOWORD(lParam) == WM_COMMAND || HIWORD(lParam) == 0) {
        UINT cmd = LOWORD(wParam);
        switch ((TrayCommand)cmd) {
            case TrayCommand::EXPAND_PANEL:
                if (onExpand) onExpand();
                break;
            case TrayCommand::SETTINGS:
                if (onSettings) onSettings();
                break;
            case TrayCommand::PERF_POWER_SAVE:
                currentPerfMode = PerformanceMode::POWER_SAVE;
                if (onPerformanceChange) onPerformanceChange(currentPerfMode);
                break;
            case TrayCommand::PERF_BALANCED:
                currentPerfMode = PerformanceMode::BALANCED;
                if (onPerformanceChange) onPerformanceChange(currentPerfMode);
                break;
            case TrayCommand::PERF_PERFORMANCE:
                currentPerfMode = PerformanceMode::PERFORMANCE;
                if (onPerformanceChange) onPerformanceChange(currentPerfMode);
                break;
            case TrayCommand::STARTUP_TOGGLE:
                startupEnabled = !startupEnabled;
                if (onStartupToggle) onStartupToggle(startupEnabled);
                break;
            case TrayCommand::EXIT:
                if (onExit) onExit();
                break;
            default:
                break;
        }
    }
}

void TrayIcon::UpdateMenuState(bool visible, bool expanded, PerformanceMode perfMode,
                                IslandPosition position, bool startup) {
    isVisible = visible;
    isExpanded = expanded;
    currentPerfMode = perfMode;
    currentPosition = position;
    startupEnabled = startup;

    if (!hMenu) return;

    // Performance mode checkmarks
    CheckMenuItem(hPerfMenu, (UINT)TrayCommand::PERF_POWER_SAVE,
                  MF_BYCOMMAND | (perfMode == PerformanceMode::POWER_SAVE ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hPerfMenu, (UINT)TrayCommand::PERF_BALANCED,
                  MF_BYCOMMAND | (perfMode == PerformanceMode::BALANCED ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hPerfMenu, (UINT)TrayCommand::PERF_PERFORMANCE,
                  MF_BYCOMMAND | (perfMode == PerformanceMode::PERFORMANCE ? MF_CHECKED : MF_UNCHECKED));

    // Expand/collapse menu text
    ModifyMenu(hMenu, (UINT)TrayCommand::EXPAND_PANEL, MF_BYCOMMAND | MF_STRING,
               (UINT)TrayCommand::EXPAND_PANEL,
               expanded ? TEXT("Collapse Panel") : TEXT("Expand Panel"));

    // Startup checkmark
    CheckMenuItem(hMenu, (UINT)TrayCommand::STARTUP_TOGGLE,
                  MF_BYCOMMAND | (startup ? MF_CHECKED : MF_UNCHECKED));
}

void TrayIcon::ShowBalloonTip(const std::string& title, const std::string& message,
                               unsigned int infoFlags) {
    if (!hwnd) return;

    NOTIFYICONDATA nidCopy = nid;
    nidCopy.uFlags |= NIF_INFO;

    wchar_t wtitle[64] = {};
    wchar_t wmessage[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wtitle, 64);
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wmessage, 256);

    wcsncpy_s(nidCopy.szInfoTitle, wtitle, _TRUNCATE);
    wcsncpy_s(nidCopy.szInfo, wmessage, _TRUNCATE);

    nidCopy.dwInfoFlags = infoFlags;
    nidCopy.uTimeout = 3000;

    Shell_NotifyIcon(NIM_MODIFY, &nidCopy);
}

void TrayIcon::SetIcon(HICON hIcon) {
    if (!hwnd || !hIcon) return;

    nid.hIcon = hIcon;
    nid.uFlags |= NIF_ICON;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void TrayIcon::SetIconByCPUUsage(float usage) {
    // Choose icon based on CPU load (green/yellow/red)
    HICON hIcon = nullptr;
    if (usage < 50.0f) {
        hIcon = LoadIcon(nullptr, IDI_INFORMATION);
    } else if (usage < 80.0f) {
        hIcon = LoadIcon(nullptr, IDI_WARNING);
    } else {
        hIcon = LoadIcon(nullptr, IDI_ERROR);
    }
    SetIcon(hIcon);
}

// ============================================================
#else  // -----------------------------------------------------
//  Linux stub implementation
// ============================================================

TrayIcon g_trayIcon;

TrayIcon::TrayIcon()  {}
TrayIcon::~TrayIcon() {}

bool TrayIcon::Initialize(HWND hwnd, UINT callbackMessage) {
    this->hwnd = hwnd;
    this->callbackMsg = callbackMessage;
    LOG_INFO("TrayIcon initialized (Linux stub — system tray not available)");
    return true;
}

void TrayIcon::Shutdown() {
    LOG_INFO("TrayIcon shutdown (Linux stub)");
    hwnd = nullptr;
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    // No tray messages on Linux; dispatch simulated commands
    LOG_DEBUG("TrayIcon::HandleMessage wParam=%llu lParam=%lld (Linux stub)", wParam, lParam);
    // On Linux the callbacks can still be triggered programmatically
}

void TrayIcon::UpdateMenuState(bool visible, bool expanded,
                                PerformanceMode perfMode,
                                IslandPosition position,
                                bool startup) {
    isVisible = visible;
    isExpanded = expanded;
    currentPerfMode = perfMode;
    currentPosition = position;
    startupEnabled = startup;
}

void TrayIcon::ShowBalloonTip(const std::string& title,
                              const std::string& message,
                              unsigned int infoFlags) {
    LOG_INFO("[Tray] %s — %s", title.c_str(), message.c_str());
}

void TrayIcon::SetIcon(HICON /*hIcon*/) {
    // No icon operations on Linux
}

void TrayIcon::SetIconByCPUUsage(float usage) {
    LOG_DEBUG("SetIconByCPUUsage(%.1f%%) — Linux stub", usage);
}

#endif
