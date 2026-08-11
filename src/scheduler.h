#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#include <comdef.h>
#include <taskschd.h>

// MinGW ILogonTrigger compatibility layer
#include "mingw_compat.h"

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")
#endif

// ============================================================
//  Shared task configuration
// ============================================================
struct TaskConfig {
    bool delayStart     = true;   // Delay startup by N seconds
    int  delaySeconds   = 30;     // Delay duration in seconds
    bool acPowerOnly    = false;  // Only start on AC power
    bool hidden         = true;   // Start hidden
    bool runAsAdmin     = false;  // Run with elevated privileges
};

// ============================================================
//  Task scheduler manager
// ============================================================
class TaskScheduler {
public:
    static TaskScheduler& Instance();

    // Init / shutdown (COM init on Windows, no-op on Linux)
    bool Initialize();
    void Shutdown();

    // Check whether auto-start task exists
    bool IsRegistered();

    // Register auto-start task
    // (First-run or user enabling; may require UAC elevation on Windows)
    bool Register(const TaskConfig& config = TaskConfig());

    // Unregister auto-start task
    bool Unregister();

    // Update existing task configuration
    bool UpdateConfig(const TaskConfig& config);

#ifdef _WIN32
    static const wchar_t* GetTaskName()   { return L"DynamicIslandStartup"; }
    static const wchar_t* GetTaskFolder() { return L"\\"; }
#else
    static const char* GetTaskName()      { return "dynamicisland"; }
#endif

private:
    TaskScheduler() = default;
    ~TaskScheduler() { Shutdown(); }
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // Get the current executable path
#ifdef _WIN32
    std::wstring GetExecutablePath() const;
    std::wstring GenerateTaskXml(const TaskConfig& config) const;
#endif

#ifdef _WIN32
    bool comInitialized = false;
    ITaskService* pService = nullptr;
#endif
};

// Global access macro
#define g_scheduler TaskScheduler::Instance()
