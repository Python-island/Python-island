#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <iphlpapi.h>
#else
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#endif

// Forward declare NVML types (dynamically loaded on both platforms)
typedef struct nvmlDevice_st* nvmlDevice_t;

// ============================================================
// Shared data structs — match README spec on all platforms
// ============================================================
struct CPUInfo {
    float usage_percent = 0.0f;
    float usage_per_core[32] = {};
    float frequency_ghz = 0.0f;
    uint64_t uptime_seconds = 0;
    std::chrono::steady_clock::time_point timestamp;
    int core_count = 0;
};

struct GPUInfo {
    float usage_percent = 0.0f;
    float memory_used_mb = 0.0f;
    float memory_total_mb = 0.0f;
    float temperature = 0.0f;
    std::string name = "Unknown";
    bool available = false;
    enum class Vendor { UNKNOWN, NVIDIA, AMD, INTEL } vendor = Vendor::UNKNOWN;
};

struct MemoryInfo {
    float usage_percent = 0.0f;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t available_bytes = 0;
};

struct BatteryInfo {
    int percent = 100;
    bool is_charging = false;
    bool is_plugged = false;
    int remaining_minutes = -1;
    std::string power_mode = "Balanced";
};

struct DisplayInfo {
    int refresh_rate_hz = 60;
    int resolution_x = 1920;
    int resolution_y = 1080;
    float dpi_scale = 1.0f;
};

struct NetworkInfo {
    float download_speed_mbps = 0.0f;
    float upload_speed_mbps = 0.0f;
    uint64_t total_download_bytes = 0;
    uint64_t total_upload_bytes = 0;
    std::string adapter_name = "Unknown";
    bool is_connected = false;
};

// ============================================================
// CPU jiffies structs (Linux /proc/stat monitoring)
#ifndef _WIN32
struct CPUJiffies {
    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    unsigned long long total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
    unsigned long long active() const { return total() - idle - iowait; }
};
struct PerCoreCPUJiffies {
    std::vector<CPUJiffies> cores;
    CPUJiffies total;
};
#endif

// System information monitoring manager
// ============================================================
class SysInfoManager {
public:
    static SysInfoManager& Instance();

    // Initialise / shutdown
    bool Initialize();
    void Shutdown();

    // Start / stop the background monitoring thread
    void StartMonitoring();
    void StopMonitoring();

    // Thread-safe data access
    CPUInfo GetCPUInfo() const;
    GPUInfo GetGPUInfo() const;
    MemoryInfo GetMemoryInfo() const;
    BatteryInfo GetBatteryInfo() const;
    DisplayInfo GetDisplayInfo() const;
    NetworkInfo GetNetworkInfo() const;

    // Convenience shortcuts
    float GetCpuUsage() const { return GetCPUInfo().usage_percent; }
    float GetMemUsage() const { return GetMemoryInfo().usage_percent; }
    float GetBatteryPercent() const { return (float)GetBatteryInfo().percent; }

    // Update display info (called on display-change events)
    void UpdateDisplayInfo();

private:
    SysInfoManager() = default;
    ~SysInfoManager() { Shutdown(); }
    SysInfoManager(const SysInfoManager&) = delete;
    SysInfoManager& operator=(const SysInfoManager&) = delete;

    // Background monitoring thread
    void MonitoringLoop();

    // Per-metric update helpers (called from the monitoring loop)
    void UpdateCPU();
    void UpdateGPU();
#ifdef _WIN32
    void UpdateGPUWMI();
#endif
    void UpdateGPUGeneric();
    void UpdateMemory();
    void UpdateBattery();
    void UpdateNetwork();

    // GPU detection / init / cleanup
    void DetectGPU();
    void InitNVIDIA();
    void CleanupGPU();

    // ---------- shared data ----------
    mutable std::mutex dataMutex;
    CPUInfo cpuData;
    GPUInfo gpuData;
    MemoryInfo memData;
    BatteryInfo batteryData;
    DisplayInfo displayData;
    NetworkInfo networkData;

    // ---------- thread control ----------
    std::atomic<bool> running{false};
    std::thread monitorThread;

    // ---------- CPU monitoring ----------
#ifdef _WIN32
    PDH_HQUERY cpuQuery = nullptr;
    PDH_HCOUNTER cpuTotalCounter = nullptr;
    std::vector<PDH_HCOUNTER> cpuCoreCounters;
#else
    PerCoreCPUJiffies prevCPUSample;
    bool prevCPUSampleValid = false;
#endif

    // ---------- network monitoring ----------
    uint64_t prevDownloadBytes = 0;
    uint64_t prevUploadBytes = 0;
    std::chrono::steady_clock::time_point prevNetworkTime;

    // ---------- GPU monitoring ----------
    bool gpuInitialized = false;

#ifdef _WIN32
    // NVIDIA NVML (dynamic load from nvml.dll)
    HMODULE nvmlHandle = nullptr;
#else
    // NVIDIA NVML (dynamic load from libnvidia-ml.so.1)
    void* nvmlHandle = nullptr;
#endif
    nvmlDevice_t nvmlDevice = nullptr;

    // NVML function-pointer typedefs
    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetCount_t)(unsigned int*);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, nvmlDevice_t*);
    typedef int (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t, void*);
    typedef int (*nvmlDeviceGetMemoryInfo_t)(nvmlDevice_t, void*);
    typedef int (*nvmlDeviceGetTemperature_t)(nvmlDevice_t, unsigned int, unsigned int*);
    typedef int (*nvmlDeviceGetName_t)(nvmlDevice_t, char*, unsigned int);
    typedef int (*nvmlDeviceGetGraphicsClockInfo_t)(nvmlDevice_t, unsigned int*);

    nvmlInit_t nvmlInit = nullptr;
    nvmlShutdown_t nvmlShutdown = nullptr;
    nvmlDeviceGetCount_t nvmlDeviceGetCount = nullptr;
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex = nullptr;
    nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates = nullptr;
    nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo = nullptr;
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature = nullptr;
    nvmlDeviceGetName_t nvmlDeviceGetName = nullptr;
    nvmlDeviceGetGraphicsClockInfo_t nvmlDeviceGetGraphicsClockInfo = nullptr;
};

// Global access macro
#define g_sysinfo SysInfoManager::Instance()
