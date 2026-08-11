#pragma once

#include <functional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdint>
// Type aliases so the shared API compiles on Linux unchanged
typedef unsigned long long WPARAM;
typedef long long LPARAM;
typedef void* HWND;
typedef void* HICON;
typedef unsigned int UINT;
#define NIIF_INFO      0x00000001
#define NIIF_WARNING   0x00000002
#define NIIF_ERROR     0x00000003
#endif

// ============================================================
//  Shared enums
// ============================================================

// Tray menu command IDs
enum class TrayCommand : unsigned int {
    SHOW_HIDE    = 1001,
    EXPAND_PANEL = 1002,
    PERF_POWER_SAVE   = 1101,
    PERF_BALANCED     = 1102,
    PERF_PERFORMANCE  = 1103,
    POS_TOP_CENTER    = 1201,
    POS_TOP_LEFT      = 1202,
    POS_FOLLOW_TASKBAR = 1203,
    STARTUP_TOGGLE    = 1301,
    SETTINGS          = 1401,
    EXIT              = 1501
};

// Performance mode
enum class PerformanceMode {
    POWER_SAVE,   // 5 s refresh
    BALANCED,     // 1 s refresh (default)
    PERFORMANCE   // 0.5 s refresh
};

// Island position
enum class IslandPosition {
    TOP_CENTER,
    TOP_LEFT,
    FOLLOW_TASKBAR
};

// ============================================================
//  Tray icon manager
// ============================================================
class TrayIcon {
public:
    using StateChangeCallback   = std::function<void()>;
    using PerformanceCallback   = std::function<void(PerformanceMode)>;
    using PositionCallback      = std::function<void(IslandPosition)>;
    using BoolCallback          = std::function<void(bool)>;

    TrayIcon();
    ~TrayIcon();

    // Init / shutdown
    bool Initialize(HWND hwnd, UINT callbackMessage);
    void Shutdown();

    // Callback setters
    void SetShowHideCallback(StateChangeCallback cb)    { onShowHide = cb; }
    void SetExpandCallback(StateChangeCallback cb)       { onExpand = cb; }
    void SetPerformanceCallback(PerformanceCallback cb)  { onPerformanceChange = cb; }
    void SetPositionCallback(PositionCallback cb)        { onPositionChange = cb; }
    void SetStartupCallback(BoolCallback cb)             { onStartupToggle = cb; }
    void SetSettingsCallback(StateChangeCallback cb)     { onSettings = cb; }
    void SetExitCallback(StateChangeCallback cb)         { onExit = cb; }

    // Handle tray messages
    void HandleMessage(WPARAM wParam, LPARAM lParam);

    // Update menu checkmarks / text
    void UpdateMenuState(bool isVisible, bool isExpanded,
                         PerformanceMode perfMode,
                         IslandPosition position,
                         bool startupEnabled);

    // Show balloon tip
    void ShowBalloonTip(const std::string& title,
                        const std::string& message,
                        unsigned int infoFlags = 0);

    // Icon management
    void SetIcon(HICON hIcon);
    void SetIconByCPUUsage(float usage);

private:
#ifdef _WIN32
    void ShowContextMenu();
    void CreateMenu();
    void DestroyMenu();

    HWND hwnd = nullptr;
    UINT callbackMsg = 0;
    NOTIFYICONDATA nid{};
    HMENU hMenu = nullptr;
    HMENU hPerfMenu = nullptr;
    HMENU hPosMenu = nullptr;
#else
    // Linux: only store the opaque window handle; menus are no-ops
    HWND hwnd = nullptr;
    unsigned int callbackMsg = 0;
#endif

    // Shared callbacks (both platforms)
    StateChangeCallback onShowHide;
    StateChangeCallback onExpand;
    PerformanceCallback onPerformanceChange;
    PositionCallback onPositionChange;
    BoolCallback onStartupToggle;
    StateChangeCallback onSettings;
    StateChangeCallback onExit;

    // Shared state
    bool isVisible = true;
    bool isExpanded = false;
    PerformanceMode currentPerfMode = PerformanceMode::BALANCED;
    IslandPosition currentPosition = IslandPosition::TOP_CENTER;
    bool startupEnabled = true;
};

// Global instance (optional)
extern TrayIcon g_trayIcon;
