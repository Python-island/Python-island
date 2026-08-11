#include "sysinfo.h"
#include "logging.h"

#ifdef _WIN32
// ============================================================
//  Windows implementation
// ============================================================
#include <psapi.h>
#include <dxgi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "wbemuuid.lib")

// NVML constants
#define NVML_SUCCESS 0
#define NVML_TEMPERATURE_GPU 0

struct nvmlUtilization_st {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_st {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

SysInfoManager& SysInfoManager::Instance() {
    static SysInfoManager instance;
    return instance;
}

// ---------------------------------------------------------
//  Initialise / shutdown
// ---------------------------------------------------------
bool SysInfoManager::Initialize() {
    // Initialise PDH for CPU monitoring
    if (PdhOpenQueryA(nullptr, 0, &cpuQuery) != ERROR_SUCCESS) {
        cpuQuery = nullptr;
    }

    if (cpuQuery) {
        // Total CPU usage counter
        PdhAddCounterA(cpuQuery, "\\Processor(_Total)\\% Processor Time", 0, &cpuTotalCounter);

        // Per-core counters
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cpuData.core_count = si.dwNumberOfProcessors;

        for (int i = 0; i < cpuData.core_count && i < 32; i++) {
            PDH_HCOUNTER counter;
            char path[256];
            sprintf_s(path, "\\Processor(%d)\\%% Processor Time", i);
            if (PdhAddCounterA(cpuQuery, path, 0, &counter) == ERROR_SUCCESS) {
                cpuCoreCounters.push_back(counter);
            }
        }

        // First query primes the PDH cache
        PdhCollectQueryData(cpuQuery);
    }

    DetectGPU();
    UpdateDisplayInfo();
    prevNetworkTime = std::chrono::steady_clock::now();
    return true;
}

void SysInfoManager::Shutdown() {
    StopMonitoring();
    CleanupGPU();

    if (cpuQuery) {
        PdhCloseQuery(cpuQuery);
        cpuQuery = nullptr;
    }
}

// ---------------------------------------------------------
//  Background monitoring thread
// ---------------------------------------------------------
void SysInfoManager::StartMonitoring() {
    if (running.exchange(true)) return;
    monitorThread = std::thread(&SysInfoManager::MonitoringLoop, this);
}

void SysInfoManager::StopMonitoring() {
    running = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void SysInfoManager::MonitoringLoop() {
    int tick = 0;
    while (running) {
        UpdateCPU();
        UpdateMemory();

        if (tick % 2 == 0) {
            UpdateBattery();
            UpdateNetwork();
        }

        if (tick % 5 == 0) {
            UpdateGPU();
        }

        LOG_DEBUG("System data refreshed (tick=%d, CPU=%.1f%%, Mem=%.1f%%)",
                  tick, GetCpuUsage(), GetMemUsage());

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        tick++;
    }
}

// ---------------------------------------------------------
//  CPU
// ---------------------------------------------------------
void SysInfoManager::UpdateCPU() {
    if (cpuQuery) {
        PdhCollectQueryData(cpuQuery);

        PDH_FMT_COUNTERVALUE value;
        if (cpuTotalCounter && PdhGetFormattedCounterValue(cpuTotalCounter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
            std::lock_guard<std::mutex> lock(dataMutex);
            cpuData.usage_percent = static_cast<float>(value.doubleValue);
            cpuData.timestamp = std::chrono::steady_clock::now();
        }

        for (size_t i = 0; i < cpuCoreCounters.size() && i < 32; i++) {
            if (PdhGetFormattedCounterValue(cpuCoreCounters[i], PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
                std::lock_guard<std::mutex> lock(dataMutex);
                cpuData.usage_per_core[i] = static_cast<float>(value.doubleValue);
            }
        }
    }

    ULONGLONG uptime = GetTickCount64() / 1000;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        cpuData.uptime_seconds = uptime;
    }
}

// ---------------------------------------------------------
//  GPU detection
// ---------------------------------------------------------
void SysInfoManager::DetectGPU() {
    // Try NVIDIA NVML first
    nvmlHandle = LoadLibraryA("nvml.dll");
    if (nvmlHandle) {
        nvmlInit = (nvmlInit_t)GetProcAddress(nvmlHandle, "nvmlInit_v2");
        if (!nvmlInit) nvmlInit = (nvmlInit_t)GetProcAddress(nvmlHandle, "nvmlInit");
        nvmlShutdown = (nvmlShutdown_t)GetProcAddress(nvmlHandle, "nvmlShutdown");
        nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetCount");
        nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetHandleByIndex_v2");
        if (!nvmlDeviceGetHandleByIndex) nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetHandleByIndex");
        nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetUtilizationRates");
        nvmlDeviceGetMemoryInfo = (nvmlDeviceGetMemoryInfo_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetMemoryInfo");
        nvmlDeviceGetTemperature = (nvmlDeviceGetTemperature_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetTemperature");
        nvmlDeviceGetName = (nvmlDeviceGetName_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetName");

        if (nvmlInit && nvmlInit() == NVML_SUCCESS) {
            unsigned int count = 0;
            if (nvmlDeviceGetCount && nvmlDeviceGetCount(&count) == NVML_SUCCESS && count > 0) {
                if (nvmlDeviceGetHandleByIndex && nvmlDeviceGetHandleByIndex(0, &nvmlDevice) == NVML_SUCCESS) {
                    gpuData.vendor = GPUInfo::Vendor::NVIDIA;
                    gpuData.available = true;
                    gpuInitialized = true;

                    if (nvmlDeviceGetName) {
                        char name[256] = {};
                        if (nvmlDeviceGetName(nvmlDevice, name, sizeof(name)) == NVML_SUCCESS) {
                            gpuData.name = name;
                        }
                    }
                    return;
                }
            }
        }
    }

    // Fallback: DXGI enumeration
    IDXGIFactory* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        IDXGIAdapter* pAdapter = nullptr;
        if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                char name[256];
                WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, name, 256, nullptr, nullptr);
                gpuData.name = name;
                gpuData.memory_total_mb = desc.DedicatedVideoMemory / (1024.0f * 1024.0f);
                gpuData.available = true;

                if (wcsstr(desc.Description, L"NVIDIA") || wcsstr(desc.Description, L"GeForce") || wcsstr(desc.Description, L"RTX")) {
                    gpuData.vendor = GPUInfo::Vendor::NVIDIA;
                } else if (wcsstr(desc.Description, L"AMD") || wcsstr(desc.Description, L"Radeon")) {
                    gpuData.vendor = GPUInfo::Vendor::AMD;
                } else if (wcsstr(desc.Description, L"Intel")) {
                    gpuData.vendor = GPUInfo::Vendor::INTEL;
                }
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }
}

// ---------------------------------------------------------
//  GPU update (NVML for NVIDIA, WMI for others)
// ---------------------------------------------------------
void SysInfoManager::UpdateGPU() {
    if (!gpuData.available) return;

    std::lock_guard<std::mutex> lock(dataMutex);

    if (gpuData.vendor == GPUInfo::Vendor::NVIDIA && nvmlDevice && nvmlDeviceGetUtilizationRates) {
        nvmlUtilization_st util;
        if (nvmlDeviceGetUtilizationRates(nvmlDevice, &util) == NVML_SUCCESS) {
            gpuData.usage_percent = static_cast<float>(util.gpu);
        }

        if (nvmlDeviceGetMemoryInfo) {
            nvmlMemory_st mem;
            if (nvmlDeviceGetMemoryInfo(nvmlDevice, &mem) == NVML_SUCCESS) {
                gpuData.memory_used_mb = mem.used / (1024.0f * 1024.0f);
                gpuData.memory_total_mb = mem.total / (1024.0f * 1024.0f);
            }
        }

        if (nvmlDeviceGetTemperature) {
            unsigned int temp;
            if (nvmlDeviceGetTemperature(nvmlDevice, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
                gpuData.temperature = static_cast<float>(temp);
            }
        }
    } else {
        UpdateGPUWMI();
    }
}

void SysInfoManager::UpdateGPUWMI() {
    HRESULT hres;

    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return;

    hres = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL
    );

    if (FAILED(hres)) {
        CoUninitialize();
        return;
    }

    IWbemLocator *pLoc = NULL;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, (LPVOID *)&pLoc);
    if (FAILED(hres)) { CoUninitialize(); return; }

    IWbemServices *pSvc = NULL;
    BSTR bstrNamespace = SysAllocString(L"ROOT\\CIMV2");
    hres = pLoc->ConnectServer(bstrNamespace, NULL, NULL, NULL, 0, NULL, NULL, &pSvc);
    SysFreeString(bstrNamespace);
    if (FAILED(hres)) { pLoc->Release(); CoUninitialize(); return; }

    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                             NULL, EOAC_NONE);
    if (FAILED(hres)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); return; }

    IEnumWbemClassObject* pEnumerator = NULL;
    BSTR bstrQueryLanguage = SysAllocString(L"WQL");
    BSTR bstrQuery = SysAllocString(L"SELECT * FROM Win32_VideoController");
    hres = pSvc->ExecQuery(bstrQueryLanguage, bstrQuery,
                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                           NULL, &pEnumerator);
    SysFreeString(bstrQueryLanguage);
    SysFreeString(bstrQuery);

    if (SUCCEEDED(hres)) {
        IWbemClassObject *pclsObj = NULL;
        ULONG uReturn = 0;

        while (pEnumerator) {
            hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
            if (0 == uReturn) break;

            VARIANT vtProp;
            VariantInit(&vtProp);

            BSTR bstrName = SysAllocString(L"Name");
            hres = pclsObj->Get(bstrName, 0, &vtProp, 0, 0);
            SysFreeString(bstrName);
            if (SUCCEEDED(hres) && vtProp.vt == VT_BSTR) {
                char name[256] = {};
                WideCharToMultiByte(CP_ACP, 0, vtProp.bstrVal, -1, name, 256, nullptr, nullptr);
                gpuData.name = name;
            }
            VariantClear(&vtProp);

            VariantInit(&vtProp);
            BSTR bstrRAM = SysAllocString(L"AdapterRAM");
            hres = pclsObj->Get(bstrRAM, 0, &vtProp, 0, 0);
            SysFreeString(bstrRAM);
            if (SUCCEEDED(hres) && vtProp.vt == VT_I4) {
                gpuData.memory_total_mb = vtProp.lVal / (1024.0f * 1024.0f);
            }
            VariantClear(&vtProp);

            pclsObj->Release();
        }
    }

    if (pEnumerator) pEnumerator->Release();
    if (pSvc) pSvc->Release();
    if (pLoc) pLoc->Release();
    CoUninitialize();
}

void SysInfoManager::UpdateGPUGeneric() {
    // No generic fallback needed on Windows; WMI covers it.
}

// ---------------------------------------------------------
//  Memory
// ---------------------------------------------------------
void SysInfoManager::UpdateMemory() {
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        std::lock_guard<std::mutex> lock(dataMutex);
        memData.total_bytes = mem.ullTotalPhys;
        memData.available_bytes = mem.ullAvailPhys;
        memData.used_bytes = mem.ullTotalPhys - mem.ullAvailPhys;
        memData.usage_percent = 100.0f * memData.used_bytes / memData.total_bytes;
    }
}

// ---------------------------------------------------------
//  Battery
// ---------------------------------------------------------
void SysInfoManager::UpdateBattery() {
    SYSTEM_POWER_STATUS pwr;
    if (GetSystemPowerStatus(&pwr)) {
        std::lock_guard<std::mutex> lock(dataMutex);
        batteryData.percent = (pwr.BatteryLifePercent == 255) ? 100 : pwr.BatteryLifePercent;
        batteryData.is_charging = (pwr.BatteryFlag & 8) != 0;
        batteryData.is_plugged = (pwr.ACLineStatus == 1);

        if (pwr.BatteryLifeTime != -1) {
            batteryData.remaining_minutes = pwr.BatteryLifeTime / 60;
        } else {
            batteryData.remaining_minutes = -1;
        }
    }
}

// ---------------------------------------------------------
//  Display
// ---------------------------------------------------------
void SysInfoManager::UpdateDisplayInfo() {
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;

    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        std::lock_guard<std::mutex> lock(dataMutex);
        displayData.refresh_rate_hz = dm.dmDisplayFrequency;
        displayData.resolution_x = dm.dmPelsWidth;
        displayData.resolution_y = dm.dmPelsHeight;
    }

    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        std::lock_guard<std::mutex> lock(dataMutex);
        displayData.dpi_scale = dpi / 96.0f;
    }
}

// ---------------------------------------------------------
//  Network (GetIfTable API)
// ---------------------------------------------------------
void SysInfoManager::UpdateNetwork() {
    ULONG ulSize = 0;
    DWORD dwResult = GetIfTable(nullptr, &ulSize, FALSE);
    if (dwResult != ERROR_INSUFFICIENT_BUFFER) return;

    MIB_IFTABLE* pIfTable = (MIB_IFTABLE*)malloc(ulSize);
    if (!pIfTable) return;

    dwResult = GetIfTable(pIfTable, &ulSize, FALSE);
    if (dwResult == NO_ERROR) {
        uint64_t totalDownload = 0;
        uint64_t totalUpload = 0;
        bool found = false;

        for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
            MIB_IFROW& row = pIfTable->table[i];
            if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL &&
                (row.dwType == IF_TYPE_ETHERNET_CSMACD || row.dwType == IF_TYPE_IEEE80211)) {
                totalDownload += row.dwInOctets;
                totalUpload += row.dwOutOctets;
                found = true;

                if (networkData.adapter_name == "Unknown") {
                    char name[256] = {};
                    WideCharToMultiByte(CP_ACP, 0, row.wszName, -1, name, 256, nullptr, nullptr);
                    networkData.adapter_name = name;
                }
            }
        }

        if (found) {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - prevNetworkTime).count();

            if (duration > 0 && prevDownloadBytes > 0 && prevUploadBytes > 0) {
                float seconds = duration / 1000.0f;
                uint64_t downloadDiff = totalDownload - prevDownloadBytes;
                uint64_t uploadDiff = totalUpload - prevUploadBytes;

                float downloadSpeed = (downloadDiff * 8.0f) / (seconds * 1000000.0f);
                float uploadSpeed = (uploadDiff * 8.0f) / (seconds * 1000000.0f);

                std::lock_guard<std::mutex> lock(dataMutex);
                // Moving-average smoothing
                networkData.download_speed_mbps = networkData.download_speed_mbps * 0.7f + downloadSpeed * 0.3f;
                networkData.upload_speed_mbps = networkData.upload_speed_mbps * 0.7f + uploadSpeed * 0.3f;
                networkData.total_download_bytes = totalDownload;
                networkData.total_upload_bytes = totalUpload;
                networkData.is_connected = true;
            }

            prevDownloadBytes = totalDownload;
            prevUploadBytes = totalUpload;
            prevNetworkTime = now;
        } else {
            std::lock_guard<std::mutex> lock(dataMutex);
            networkData.is_connected = false;
        }
    }

    free(pIfTable);
}

// ---------------------------------------------------------
//  GPU cleanup
// ---------------------------------------------------------
void SysInfoManager::CleanupGPU() {
    if (nvmlHandle) {
        if (nvmlShutdown) nvmlShutdown();
        FreeLibrary(nvmlHandle);
        nvmlHandle = nullptr;
        nvmlDevice = nullptr;
    }
}

// ---------------------------------------------------------
//  Thread-safe getters
// ---------------------------------------------------------
CPUInfo SysInfoManager::GetCPUInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return cpuData;
}

GPUInfo SysInfoManager::GetGPUInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return gpuData;
}

MemoryInfo SysInfoManager::GetMemoryInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return memData;
}

BatteryInfo SysInfoManager::GetBatteryInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return batteryData;
}

DisplayInfo SysInfoManager::GetDisplayInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return displayData;
}

NetworkInfo SysInfoManager::GetNetworkInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return networkData;
}

// ============================================================
#else  // -----------------------------------------------------
//  Linux implementation
// ============================================================
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/utsname.h>

// NVML constants
#define NVML_SUCCESS             0
#define NVML_TEMPERATURE_GPU     0
#define NVML_CLOCK_GRAPHICS      0

struct nvmlUtilization_st {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_st {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

// ---------------------------------------------------------
//  Singleton
// ---------------------------------------------------------
SysInfoManager& SysInfoManager::Instance() {
    static SysInfoManager instance;
    return instance;
}

// ============================================================
//  Helpers – file reading
// ============================================================
static std::string readFileLine(const std::string& path, int lineIdx = 0) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    for (int i = 0; i <= lineIdx; i++) {
        if (!std::getline(f, line)) return {};
    }
    return line;
}

static std::string readFileAll(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// ---------------------------------------------------------
//  Initialise / shutdown
// ---------------------------------------------------------
bool SysInfoManager::Initialize() {
    // Count CPU cores
    cpuData.core_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpuData.core_count <= 0) cpuData.core_count = 1;
    if (cpuData.core_count > 32) cpuData.core_count = 32;

    DetectGPU();
    UpdateDisplayInfo();
    prevNetworkTime = std::chrono::steady_clock::now();
    return true;
}

void SysInfoManager::Shutdown() {
    StopMonitoring();
    CleanupGPU();
}

// ---------------------------------------------------------
//  Background monitoring thread
// ---------------------------------------------------------
void SysInfoManager::StartMonitoring() {
    if (running.exchange(true)) return;
    monitorThread = std::thread(&SysInfoManager::MonitoringLoop, this);
}

void SysInfoManager::StopMonitoring() {
    running = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void SysInfoManager::MonitoringLoop() {
    int tick = 0;
    while (running) {
        UpdateCPU();
        UpdateMemory();

        if (tick % 2 == 0) {
            UpdateBattery();
            UpdateNetwork();
        }

        if (tick % 5 == 0) {
            UpdateGPU();
        }

        LOG_DEBUG("System data refreshed (tick=%d, CPU=%.1f%%, Mem=%.1f%%)",
                  tick, GetCpuUsage(), GetMemUsage());

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        tick++;
    }
}

// ============================================================
//  CPU — read /proc/stat, delta between samples
// ============================================================
static bool parseCPUJiffiesLine(const std::string& line, CPUJiffies& out) {
    // Expected format: "cpu  user nice system idle iowait irq softirq steal ..."
    // or "cpu0 user ..."
    std::istringstream iss(line);
    std::string label;
    iss >> label;
    iss >> out.user >> out.nice >> out.system >> out.idle
        >> out.iowait >> out.irq >> out.softirq >> out.steal;
    return !iss.fail();
}

void SysInfoManager::UpdateCPU() {
    std::ifstream statFile("/proc/stat");
    if (!statFile.is_open()) return;

    PerCoreCPUJiffies current;
    std::string line;
    CPUJiffies totalAccum;
    int coreIdx = 0;

    while (std::getline(statFile, line)) {
        if (line.rfind("cpu", 0) != 0) break;  // stop when we leave cpu* lines

        if (line[3] == ' ') {
            // "cpu  ..."  = aggregate
            CPUJiffies j;
            if (parseCPUJiffiesLine(line, j)) {
                totalAccum = j;
            }
        } else {
            // "cpuN ..."  = per-core
            CPUJiffies j;
            if (parseCPUJiffiesLine(line, j)) {
                current.cores.push_back(j);
            }
        }
    }
    current.total = totalAccum;

    if (!prevCPUSampleValid) {
        // First sample – store baseline and return
        prevCPUSample = current;
        prevCPUSampleValid = true;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(dataMutex);
        cpuData.timestamp = std::chrono::steady_clock::now();

        // Total usage
        unsigned long long prevTotal = prevCPUSample.total.total();
        unsigned long long prevActive = prevCPUSample.total.active();
        unsigned long long curTotal = current.total.total();
        unsigned long long curActive = current.total.active();

        unsigned long long totalDelta = curTotal - prevTotal;
        unsigned long long activeDelta = curActive - prevActive;

        if (totalDelta > 0) {
            cpuData.usage_percent = 100.0f * activeDelta / (float)totalDelta;
        }

        // Per-core usage
        size_t ncores = std::min(current.cores.size(), (size_t)32);
        for (size_t i = 0; i < ncores && i < prevCPUSample.cores.size(); i++) {
            unsigned long long pTot = prevCPUSample.cores[i].total();
            unsigned long long pAct = prevCPUSample.cores[i].active();
            unsigned long long cTot = current.cores[i].total();
            unsigned long long cAct = current.cores[i].active();

            unsigned long long dtot = cTot - pTot;
            unsigned long long dact = cAct - pAct;

            if (dtot > 0) {
                cpuData.usage_per_core[i] = 100.0f * dact / (float)dtot;
            } else {
                cpuData.usage_per_core[i] = 0.0f;
            }
        }
    }

    prevCPUSample = current;

    // Uptime from /proc/uptime
    std::string uptimeStr = readFileLine("/proc/uptime");
    if (!uptimeStr.empty()) {
        std::istringstream iss(uptimeStr);
        double up;
        iss >> up;
        if (!iss.fail()) {
            std::lock_guard<std::mutex> lock(dataMutex);
            cpuData.uptime_seconds = (uint64_t)up;
        }
    }
}

// ============================================================
//  GPU detection — NVML first, then /sys/class/drm/
// ============================================================
void SysInfoManager::DetectGPU() {
    // 1) Try NVML via dlopen
    InitNVIDIA();
    if (gpuInitialized) return;

    // 2) Probe /sys/class/drm/ for card info
    const char* drmBase = "/sys/class/drm";
    DIR* dir = opendir(drmBase);
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        // Look for "cardN" entries (not "cardN-*" sub-nodes)
        if (strncmp(ent->d_name, "card", 4) == 0 && ent->d_name[4] >= '0' && ent->d_name[4] <= '9') {
            // Verify it has a device/vendor sub-file
            std::string cardPath = std::string(drmBase) + "/" + ent->d_name + "/device";
            if (!fileExists(cardPath + "/vendor")) continue;

            gpuData.available = true;

            // Vendor detection
            std::string vendorHex = readFileLine(cardPath + "/vendor");
            if (vendorHex.size() > 2 && vendorHex[0] == '0' && vendorHex[1] == 'x') {
                unsigned int vid = (unsigned int)strtoul(vendorHex.c_str() + 2, nullptr, 16);
                if (vid == 0x10de) {
                    gpuData.vendor = GPUInfo::Vendor::NVIDIA;
                } else if (vid == 0x1002) {
                    gpuData.vendor = GPUInfo::Vendor::AMD;
                } else if (vid == 0x8086) {
                    gpuData.vendor = GPUInfo::Vendor::INTEL;
                }
            }

            // GPU name: try device/uevent for DRIVER
            std::string driver = "Unknown";
            if (fileExists(cardPath + "/uevent")) {
                std::ifstream uevent(cardPath + "/uevent");
                std::string ueLine;
                while (std::getline(uevent, ueLine)) {
                    if (ueLine.rfind("DRIVER=", 0) == 0) {
                        driver = ueLine.substr(7);
                        break;
                    }
                }
            }
            gpuData.name = driver;

            // VRAM: try device/mem_info_vram_total if available
            std::string memPath = cardPath + "/mem_info_vram_total";
            if (fileExists(memPath)) {
                std::string memStr = readFileLine(memPath);
                unsigned long long bytes = strtoull(memStr.c_str(), nullptr, 10);
                gpuData.memory_total_mb = bytes / (1024.0f * 1024.0f);
            }

            break;  // Use first GPU card
        }
    }
    closedir(dir);
}

void SysInfoManager::InitNVIDIA() {
    nvmlHandle = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!nvmlHandle) nvmlHandle = dlopen("libnvidia-ml.so", RTLD_LAZY);
    if (!nvmlHandle) return;

    nvmlInit                       = (nvmlInit_t)dlsym(nvmlHandle, "nvmlInit_v2");
    if (!nvmlInit) nvmlInit        = (nvmlInit_t)dlsym(nvmlHandle, "nvmlInit");
    nvmlShutdown                   = (nvmlShutdown_t)dlsym(nvmlHandle, "nvmlShutdown");
    nvmlDeviceGetCount             = (nvmlDeviceGetCount_t)dlsym(nvmlHandle, "nvmlDeviceGetCount");
    nvmlDeviceGetHandleByIndex     = (nvmlDeviceGetHandleByIndex_t)dlsym(nvmlHandle, "nvmlDeviceGetHandleByIndex_v2");
    if (!nvmlDeviceGetHandleByIndex)
        nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)dlsym(nvmlHandle, "nvmlDeviceGetHandleByIndex");
    nvmlDeviceGetUtilizationRates  = (nvmlDeviceGetUtilizationRates_t)dlsym(nvmlHandle, "nvmlDeviceGetUtilizationRates");
    nvmlDeviceGetMemoryInfo        = (nvmlDeviceGetMemoryInfo_t)dlsym(nvmlHandle, "nvmlDeviceGetMemoryInfo");
    nvmlDeviceGetTemperature       = (nvmlDeviceGetTemperature_t)dlsym(nvmlHandle, "nvmlDeviceGetTemperature");
    nvmlDeviceGetName              = (nvmlDeviceGetName_t)dlsym(nvmlHandle, "nvmlDeviceGetName");

    if (nvmlInit && nvmlInit() == NVML_SUCCESS) {
        unsigned int count = 0;
        if (nvmlDeviceGetCount && nvmlDeviceGetCount(&count) == NVML_SUCCESS && count > 0) {
            if (nvmlDeviceGetHandleByIndex && nvmlDeviceGetHandleByIndex(0, &nvmlDevice) == NVML_SUCCESS) {
                gpuData.vendor = GPUInfo::Vendor::NVIDIA;
                gpuData.available = true;
                gpuInitialized = true;

                if (nvmlDeviceGetName) {
                    char name[256] = {};
                    if (nvmlDeviceGetName(nvmlDevice, name, sizeof(name)) == NVML_SUCCESS) {
                        gpuData.name = name;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------
//  GPU update
// ---------------------------------------------------------
void SysInfoManager::UpdateGPU() {
    if (!gpuData.available) return;

    std::lock_guard<std::mutex> lock(dataMutex);

    if (gpuData.vendor == GPUInfo::Vendor::NVIDIA && nvmlDevice && nvmlDeviceGetUtilizationRates) {
        nvmlUtilization_st util;
        if (nvmlDeviceGetUtilizationRates(nvmlDevice, &util) == NVML_SUCCESS) {
            gpuData.usage_percent = static_cast<float>(util.gpu);
        }

        if (nvmlDeviceGetMemoryInfo) {
            nvmlMemory_st mem;
            if (nvmlDeviceGetMemoryInfo(nvmlDevice, &mem) == NVML_SUCCESS) {
                gpuData.memory_used_mb = mem.used / (1024.0f * 1024.0f);
                gpuData.memory_total_mb = mem.total / (1024.0f * 1024.0f);
            }
        }

        if (nvmlDeviceGetTemperature) {
            unsigned int temp;
            if (nvmlDeviceGetTemperature(nvmlDevice, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
                gpuData.temperature = static_cast<float>(temp);
            }
        }
    } else {
        UpdateGPUGeneric();
    }
}

void SysInfoManager::UpdateGPUGeneric() {
    // Non-NVIDIA — rough usage via DRM fdinfo if available
    // (Most non-NVIDIA GPUs on Linux don't expose usage via a simple sysfs file.
    //  The NVML path above handles NVIDIA; for AMD/Intel we leave the previous
    //  cached value.)
    // As a lightweight fallback, probe /sys/class/drm/card*/device/
    // but there's no standardised usage file outside of fdinfo, so this is
    // intentionally minimal.
}

// ============================================================
//  Memory — /proc/meminfo
// ============================================================
void SysInfoManager::UpdateMemory() {
    std::string content = readFileAll("/proc/meminfo");
    if (content.empty()) return;

    std::istringstream iss(content);
    std::string key;
    unsigned long long value = 0;
    unsigned long long memTotal = 0, memAvailable = 0;

    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            sscanf(line.c_str(), "MemTotal: %llu kB", &memTotal);
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            sscanf(line.c_str(), "MemAvailable: %llu kB", &memAvailable);
        }
    }

    if (memTotal > 0) {
        std::lock_guard<std::mutex> lock(dataMutex);
        memData.total_bytes = memTotal * 1024;
        memData.available_bytes = memAvailable * 1024;
        memData.used_bytes = memData.total_bytes - memData.available_bytes;
        memData.usage_percent = 100.0f * memData.used_bytes / memData.total_bytes;
    }
}

// ============================================================
//  Battery — /sys/class/power_supply/BAT*/
// ============================================================
static bool batteryStat(const std::string& batPath, BatteryInfo& out) {
    // capacity / energy_now / energy_full
    std::string capPath = batPath + "/capacity";
    double chargePercent = 100.0;

    if (fileExists(capPath)) {
        std::string s = readFileLine(capPath, 0);
        chargePercent = atof(s.c_str());
    } else {
        std::string nowPath = batPath + "/energy_now";
        std::string fullPath = batPath + "/energy_full";
        if (fileExists(nowPath) && fileExists(fullPath)) {
            double now = atof(readFileLine(nowPath).c_str());
            double full = atof(readFileLine(fullPath).c_str());
            if (full > 0) chargePercent = 100.0 * now / full;
        }
    }
    out.percent = (int)chargePercent;
    if (out.percent < 0) out.percent = 0;
    if (out.percent > 100) out.percent = 100;

    // Status: "Charging", "Discharging", "Full", "Unknown"
    std::string status = readFileLine(batPath + "/status");
    // Trim
    status.erase(std::remove_if(status.begin(), status.end(), ::isspace), status.end());
    out.is_charging = (status == "Charging");
    out.is_plugged = (status == "Charging" || status == "Full" || status == "Unknown");

    out.remaining_minutes = -1;

    // If discharging, try energy_now / power_now for remaining time
    if (!out.is_charging) {
        std::string pnowStr = readFileLine(batPath + "/power_now");
        std::string enowStr = readFileLine(batPath + "/energy_now");
        if (!pnowStr.empty() && !enowStr.empty()) {
            double powerNow = atof(pnowStr.c_str());   // microwatts
            double energyNow = atof(enowStr.c_str());  // micro-watt-hours
            if (powerNow > 0) {
                double hoursRemaining = energyNow / powerNow;
                out.remaining_minutes = (int)(hoursRemaining * 60.0);
            }
        }
    }

    return true;
}

void SysInfoManager::UpdateBattery() {
    const char* base = "/sys/class/power_supply";
    DIR* dir = opendir(base);
    if (!dir) return;

    struct dirent* ent;
    BatteryInfo best;
    best.percent = -1;

    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "BAT", 3) == 0) {
            std::string batPath = std::string(base) + "/" + ent->d_name;
            BatteryInfo bi;
            if (batteryStat(batPath, bi)) {
                // Use the first battery (or highest percentage)
                if (bi.percent > best.percent) best = bi;
            }
        }
    }
    closedir(dir);

    if (best.percent >= 0) {
        std::lock_guard<std::mutex> lock(dataMutex);
        batteryData = best;
    }
}

// ============================================================
//  Display — read via system tools (resolution from GLFW callers,
//  or /sys/class/drm for refresh; fallbacks below)
// ============================================================
void SysInfoManager::UpdateDisplayInfo() {
    // Read resolution from /sys/class/drm/card*-*/modes
    const char* drmBase = "/sys/class/drm";
    DIR* dir = opendir(drmBase);
    if (!dir) return;

    struct dirent* ent;
    bool found = false;
    while ((ent = readdir(dir)) != nullptr && !found) {
        if (strncmp(ent->d_name, "card", 4) != 0) continue;
        // Skip cardN-* sub-nodes, only read cardN directly
        // Actually we want the connector nodes, e.g. card0-HDMI-A-1
        std::string entryName(ent->d_name);
        if (entryName.find("-") == std::string::npos) continue;

        std::string modesPath = std::string(drmBase) + "/" + entryName + "/modes";
        if (!fileExists(modesPath)) continue;

        std::ifstream mf(modesPath);
        std::string modeLine;
        if (std::getline(mf, modeLine)) {
            // Format: "1920x1080"
            int rx, ry;
            if (sscanf(modeLine.c_str(), "%dx%d", &rx, &ry) == 2) {
                std::lock_guard<std::mutex> lock(dataMutex);
                displayData.resolution_x = rx;
                displayData.resolution_y = ry;
                found = true;
            }
        }
        // Refresh rate: not directly in sysfs; keep previous or assume 60 Hz
    }
    closedir(dir);

    // DPI: try Xft.dpi via xrdb if available, otherwise default 96
    // (GLFW users may call glfwGetMonitorContentScale instead)
    displayData.dpi_scale = 1.0f;

    // Try reading Xft.dpi from Xresources
    FILE* xrdb = popen("xrdb -query 2>/dev/null | grep '^Xft.dpi:'", "r");
    if (xrdb) {
        char buf[128] = {};
        if (fgets(buf, sizeof(buf), xrdb) && buf[0] != '\0') {
            float dpi = 96.0f;
            sscanf(buf, "Xft.dpi: %f", &dpi);
            if (dpi > 0) {
                std::lock_guard<std::mutex> lock(dataMutex);
                displayData.dpi_scale = dpi / 96.0f;
            }
        }
        pclose(xrdb);
    }
}

// ============================================================
//  Network — /proc/net/dev with moving-average smoothing
// ============================================================
void SysInfoManager::UpdateNetwork() {
    std::ifstream netDev("/proc/net/dev");
    if (!netDev.is_open()) return;

    uint64_t totalRx = 0, totalTx = 0;
    bool found = false;
    std::string firstIface;

    std::string line;
    // Skip two header lines
    std::getline(netDev, line);
    std::getline(netDev, line);

    while (std::getline(netDev, line)) {
        // Parse: "  iface: rx_bytes rx_packets ... tx_bytes tx_packets ..."
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string iface = line.substr(0, colon);
        // Trim leading whitespace
        size_t start = iface.find_first_not_of(" \t");
        if (start != std::string::npos) iface = iface.substr(start);
        // Trim trailing whitespace
        size_t end = iface.find_last_not_of(" \t");
        if (end != std::string::npos) iface = iface.substr(0, end + 1);
        if (iface.empty()) continue;

        // Skip loopback
        if (iface == "lo") continue;

        uint64_t rx, rxPkt, rxErr, rxDrop, rxFifo, rxFrame, rxComp, rxMcast;
        uint64_t tx, txPkt, txErr, txDrop, txFifo, txColl, txCarrier, txComp;
        std::istringstream fields(line.substr(colon + 1));
        fields >> rx >> rxPkt >> rxErr >> rxDrop >> rxFifo >> rxFrame >> rxComp >> rxMcast
               >> tx >> txPkt >> txErr >> txDrop >> txFifo >> txColl >> txCarrier >> txComp;

        if (fields.fail()) continue;

        // Only count non-virtual interfaces (no "@", no "veth", no "docker", no "virbr")
        if (iface.find('@') != std::string::npos) continue;
        if (iface.rfind("veth", 0) == 0) continue;
        if (iface.rfind("docker", 0) == 0) continue;
        if (iface == "virbr0") continue;

        // Prefer Ethernet / WiFi (en*, eth*, wl*)
        bool isPrimary = (iface.rfind("en", 0) == 0 || iface.rfind("eth", 0) == 0 || iface.rfind("wl", 0) == 0);

        if (isPrimary || firstIface.empty()) {
            if (isPrimary && found) {
                // Already have a primary interface; add stats (multi-NIC)
                totalRx += rx;
                totalTx += tx;
            } else if (!found) {
                found = true;
                firstIface = iface;
                totalRx = rx;
                totalTx = tx;
            }
        }
    }

    if (found) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - prevNetworkTime).count();

        if (duration > 0 && prevDownloadBytes > 0 && prevUploadBytes > 0) {
            float seconds = duration / 1000.0f;
            uint64_t rxDelta = (totalRx >= prevDownloadBytes) ? (totalRx - prevDownloadBytes) : 0;
            uint64_t txDelta = (totalTx >= prevUploadBytes) ? (totalTx - prevUploadBytes) : 0;

            float dlSpeed = (rxDelta * 8.0f) / (seconds * 1000000.0f);  // Mbps
            float ulSpeed = (txDelta * 8.0f) / (seconds * 1000000.0f);

            std::lock_guard<std::mutex> lock(dataMutex);
            // Moving-average smoothing (same algorithm as Windows side)
            networkData.download_speed_mbps = networkData.download_speed_mbps * 0.7f + dlSpeed * 0.3f;
            networkData.upload_speed_mbps   = networkData.upload_speed_mbps   * 0.7f + ulSpeed * 0.3f;
            networkData.total_download_bytes = totalRx;
            networkData.total_upload_bytes   = totalTx;
            networkData.is_connected = true;
        } else if (prevDownloadBytes == 0 && prevUploadBytes == 0) {
            // First valid sample — prime the counters
            // (no speed yet, but mark it connected)
            std::lock_guard<std::mutex> lock(dataMutex);
            networkData.is_connected = true;
        }

        prevDownloadBytes = totalRx;
        prevUploadBytes   = totalTx;
        prevNetworkTime   = now;

        // Update adapter name once
        if (networkData.adapter_name == "Unknown" || networkData.adapter_name.empty()) {
            std::lock_guard<std::mutex> lock(dataMutex);
            networkData.adapter_name = firstIface;
        }
    } else {
        std::lock_guard<std::mutex> lock(dataMutex);
        networkData.is_connected = false;
    }
}

// ---------------------------------------------------------
//  GPU cleanup
// ---------------------------------------------------------
void SysInfoManager::CleanupGPU() {
    if (nvmlHandle) {
        if (nvmlShutdown) nvmlShutdown();
        dlclose(nvmlHandle);
        nvmlHandle = nullptr;
        nvmlDevice = nullptr;
    }
}

// ---------------------------------------------------------
//  Thread-safe getters
// ---------------------------------------------------------
CPUInfo SysInfoManager::GetCPUInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return cpuData;
}

GPUInfo SysInfoManager::GetGPUInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return gpuData;
}

MemoryInfo SysInfoManager::GetMemoryInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return memData;
}

BatteryInfo SysInfoManager::GetBatteryInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return batteryData;
}

DisplayInfo SysInfoManager::GetDisplayInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return displayData;
}

NetworkInfo SysInfoManager::GetNetworkInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return networkData;
}

#endif
