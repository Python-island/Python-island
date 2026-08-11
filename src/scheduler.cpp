#include "scheduler.h"
#include "logging.h"

#ifdef _WIN32
// ============================================================
//  Windows implementation — Task Scheduler COM
// ============================================================
#include <sddl.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "advapi32.lib")

TaskScheduler& TaskScheduler::Instance() {
    static TaskScheduler instance;
    return instance;
}

bool TaskScheduler::Initialize() {
    if (comInitialized) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    comInitialized = true;

    hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        CoUninitialize();
        comInitialized = false;
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        pService = nullptr;
        CoUninitialize();
        comInitialized = false;
        return false;
    }

    return true;
}

void TaskScheduler::Shutdown() {
    if (pService) {
        pService->Release();
        pService = nullptr;
    }
    if (comInitialized) {
        CoUninitialize();
        comInitialized = false;
    }
}

bool TaskScheduler::IsRegistered() {
    if (!pService && !Initialize()) return false;

    ITaskFolder* pRootFolder = nullptr;
    HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) return false;

    IRegisteredTask* pTask = nullptr;
    hr = pRootFolder->GetTask(_bstr_t(GetTaskName()), &pTask);

    pRootFolder->Release();

    if (SUCCEEDED(hr) && pTask) {
        pTask->Release();
        return true;
    }

    return false;
}

bool TaskScheduler::Register(const TaskConfig& config) {
    if (!pService && !Initialize()) return false;

    ITaskFolder* pRootFolder = nullptr;
    HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) return false;

    // Remove existing task first
    if (IsRegistered()) {
        Unregister();
    }

    ITaskDefinition* pTask = nullptr;
    hr = pService->NewTask(0, &pTask);
    if (FAILED(hr)) {
        pRootFolder->Release();
        return false;
    }

    // Registration info
    IRegistrationInfo* pRegInfo = nullptr;
    hr = pTask->get_RegistrationInfo(&pRegInfo);
    if (SUCCEEDED(hr)) {
        pRegInfo->put_Author(_bstr_t(L"DynamicIsland"));
        pRegInfo->put_Description(_bstr_t(L"DynamicIsland System Monitor - Auto-start"));
        pRegInfo->Release();
    }

    // Principal (logon type)
    IPrincipal* pPrincipal = nullptr;
    hr = pTask->get_Principal(&pPrincipal);
    if (SUCCEEDED(hr)) {
        pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        if (config.runAsAdmin) {
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        } else {
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_LUA);
        }
        pPrincipal->Release();
    }

    // Trigger (logon with optional delay)
    ITriggerCollection* pTriggers = nullptr;
    hr = pTask->get_Triggers(&pTriggers);
    if (SUCCEEDED(hr)) {
        ITrigger* pTrigger = nullptr;
        hr = pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger);
        if (SUCCEEDED(hr)) {
            ILogonTrigger* pLogonTrigger = nullptr;
            hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
            if (SUCCEEDED(hr)) {
                pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));

                if (config.delayStart && config.delaySeconds > 0) {
                    std::wstring delayStr = L"PT" + std::to_wstring(config.delaySeconds) + L"S";
                    pLogonTrigger->put_Delay(_bstr_t(delayStr.c_str()));
                }

                pLogonTrigger->Release();
            }
            pTrigger->Release();
        }
        pTriggers->Release();
    }

    // Settings
    ITaskSettings* pSettings = nullptr;
    hr = pTask->get_Settings(&pSettings);
    if (SUCCEEDED(hr)) {
        pSettings->put_StartWhenAvailable(VARIANT_TRUE);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_Hidden(config.hidden ? VARIANT_TRUE : VARIANT_FALSE);
        pSettings->put_RunOnlyIfNetworkAvailable(VARIANT_FALSE);
        pSettings->put_AllowDemandStart(VARIANT_TRUE);
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
        pSettings->Release();
    }

    // Action (execute)
    IActionCollection* pActions = nullptr;
    hr = pTask->get_Actions(&pActions);
    if (SUCCEEDED(hr)) {
        IAction* pAction = nullptr;
        hr = pActions->Create(TASK_ACTION_EXEC, &pAction);
        if (SUCCEEDED(hr)) {
            IExecAction* pExecAction = nullptr;
            hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
            if (SUCCEEDED(hr)) {
                std::wstring exePath = GetExecutablePath();
                pExecAction->put_Path(_bstr_t(exePath.c_str()));
                pExecAction->put_Arguments(_bstr_t(L"/background"));
                pExecAction->put_WorkingDirectory(
                    _bstr_t(exePath.substr(0, exePath.find_last_of(L"\\")).c_str()));
                pExecAction->Release();
            }
            pAction->Release();
        }
        pActions->Release();
    }

    // Register
    IRegisteredTask* pRegisteredTask = nullptr;
    hr = pRootFolder->RegisterTaskDefinition(
        _bstr_t(GetTaskName()),
        pTask,
        TASK_CREATE_OR_UPDATE,
        _variant_t(),
        _variant_t(),
        TASK_LOGON_INTERACTIVE_TOKEN,
        _variant_t(L""),
        &pRegisteredTask
    );

    pTask->Release();
    pRootFolder->Release();

    if (SUCCEEDED(hr) && pRegisteredTask) {
        pRegisteredTask->Release();
        return true;
    }

    return false;
}

bool TaskScheduler::Unregister() {
    if (!pService && !Initialize()) return false;

    ITaskFolder* pRootFolder = nullptr;
    HRESULT hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) return false;

    hr = pRootFolder->DeleteTask(_bstr_t(GetTaskName()), 0);
    pRootFolder->Release();

    return SUCCEEDED(hr);
}

bool TaskScheduler::UpdateConfig(const TaskConfig& config) {
    if (IsRegistered()) {
        return Register(config);
    }
    return false;
}

std::wstring TaskScheduler::GetExecutablePath() const {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path);
}

std::wstring TaskScheduler::GenerateTaskXml(const TaskConfig& config) const {
    std::wstringstream xml;

    xml << L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n";
    xml << L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n";
    xml << L"  <RegistrationInfo>\n";
    xml << L"    <Description>DynamicIsland System Monitor</Description>\n";
    xml << L"  </RegistrationInfo>\n";
    xml << L"  <Triggers>\n";
    xml << L"    <LogonTrigger>\n";
    if (config.delayStart) {
        xml << L"      <Delay>PT" << config.delaySeconds << L"S</Delay>\n";
    }
    xml << L"    </LogonTrigger>\n";
    xml << L"  </Triggers>\n";
    xml << L"  <Principals>\n";
    xml << L"    <Principal>\n";
    xml << L"      <LogonType>InteractiveToken</LogonType>\n";
    xml << L"      <RunLevel>" << (config.runAsAdmin ? L"HighestAvailable" : L"LeastPrivilege") << L"</RunLevel>\n";
    xml << L"    </Principal>\n";
    xml << L"  </Principals>\n";
    xml << L"  <Settings>\n";
    xml << L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n";
    xml << L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n";
    xml << L"    <Hidden>" << (config.hidden ? L"true" : L"false") << L"</Hidden>\n";
    xml << L"  </Settings>\n";
    xml << L"  <Actions>\n";
    xml << L"    <Exec>\n";
    xml << L"      <Command>" << GetExecutablePath() << L"</Command>\n";
    xml << L"      <Arguments>/background</Arguments>\n";
    xml << L"    </Exec>\n";
    xml << L"  </Actions>\n";
    xml << L"</Task>\n";

    return xml.str();
}

// ============================================================
#else  // -----------------------------------------------------
//  Linux implementation — XDG autostart .desktop file
// ============================================================
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

TaskScheduler& TaskScheduler::Instance() {
    static TaskScheduler instance;
    return instance;
}

bool TaskScheduler::Initialize() {
    // No COM or equivalent on Linux — always succeeds
    return true;
}

void TaskScheduler::Shutdown() {
    // No-op
}

// Helper: get ~/.config/autostart directory, creating it if needed
static std::string getAutostartDir() {
    const char* configDir = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (configDir && configDir[0] != '\0') {
        base = configDir;
    } else {
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        base = std::string(home) + "/.config";
    }
    std::string autostart = base + "/autostart";
    mkdir(base.c_str(), 0755);
    mkdir(autostart.c_str(), 0755);
    return autostart;
}

// Helper: get the path of the running executable
static std::string getSelfExePath() {
    char buf[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return "dynamicisland";
}

// Helper: get the working directory of the running executable
static std::string getSelfWorkingDir() {
    std::string exe = getSelfExePath();
    size_t slash = exe.find_last_of('/');
    if (slash != std::string::npos) {
        return exe.substr(0, slash);
    }
    return ".";
}

bool TaskScheduler::IsRegistered() {
    std::string desktopPath = getAutostartDir() + "/dynamicisland.desktop";
    struct stat st;
    return stat(desktopPath.c_str(), &st) == 0;
}

bool TaskScheduler::Register(const TaskConfig& config) {
    std::string autostartDir = getAutostartDir();
    std::string desktopPath = autostartDir + "/dynamicisland.desktop";
    std::string exePath = getSelfExePath();
    std::string workDir = getSelfWorkingDir();

    std::ofstream file(desktopPath);
    if (!file.is_open()) {
        LOG_ERROR("TaskScheduler: failed to write %s", desktopPath.c_str());
        return false;
    }

    file << "[Desktop Entry]\n";
    file << "Type=Application\n";
    file << "Name=DynamicIsland\n";
    file << "Comment=DynamicIsland System Monitor\n";
    file << "Exec=" << exePath << " /background\n";
    file << "Path=" << workDir << "\n";
    file << "Terminal=false\n";
    if (config.hidden) {
        file << "StartupNotify=false\n";
    }
    file << "X-GNOME-Autostart-enabled=true\n";
    file << "X-KDE-autostart-after=panel\n";

    file.close();

    LOG_INFO("TaskScheduler: registered .desktop auto-start at %s", desktopPath.c_str());
    return true;
}

bool TaskScheduler::Unregister() {
    std::string desktopPath = getAutostartDir() + "/dynamicisland.desktop";
    if (unlink(desktopPath.c_str()) == 0) {
        LOG_INFO("TaskScheduler: removed auto-start entry %s", desktopPath.c_str());
        return true;
    }
    // File not existing is also "success"
    return (errno == ENOENT);
}

bool TaskScheduler::UpdateConfig(const TaskConfig& config) {
    // Re-register with new config
    return Register(config);
}

#endif
