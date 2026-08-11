// ===================================================================
// main.cpp — Cross-platform entry point & main loop
//   Windows: Win32 + Direct3D 11, single-instance via named mutex
//   Linux:   GLFW + OpenGL 3, single-instance via flock()
// ===================================================================

#ifdef _WIN32
// ========================== WINDOWS ================================
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <chrono>
#include <cstdio>
#include <shlobj.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "logging.h"
#include "config.h"
#include "sysinfo.h"
#include "trayicon.h"
#include "scheduler.h"
#include "window.h"
#include "ui.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

static bool InitializeApp(bool silentStart) {
    FILE* f = fopen("log/dynamicisland.log", "w");
    if (f) fclose(f);

    LOG_INFO("=== DynamicIsland Starting (Windows) ===");
    LOG_INFO("silentStart=%d", silentStart);
    Logger::Instance().SetConsoleOutput(true);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { LOG_ERROR("COM init failed: 0x%X", hr); return false; }

    if (!g_config.Load()) { LOG_INFO("Config not found, creating default"); g_config.Save(); }
    LOG_INFO("Config loaded: opacity=%.2f", g_config.GetAppearance().opacity);

    if (!g_sysinfo.Initialize()) { LOG_ERROR("Sysinfo init failed"); return false; }
    g_sysinfo.StartMonitoring();

    g_scheduler.Initialize();
    if (g_config.GetBehavior().start_with_windows && !g_scheduler.IsRegistered()) {
        TaskConfig tc; tc.delayStart = true; tc.delaySeconds = 30; tc.hidden = true;
        g_scheduler.Register(tc);
    }

    g_mainWindow = CreateMainWindow();
    if (!g_mainWindow) { LOG_ERROR("Window creation failed"); return false; }

    if (!CreateDeviceD3D(g_mainWindow)) {
        CleanupDeviceD3D();
        ::DestroyWindow(g_mainWindow);
        g_mainWindow = nullptr;
        return false;
    }

    if (!g_trayIcon.Initialize(g_mainWindow, WM_TRAYICON))
        LOG_ERROR("Tray icon init failed");

    g_trayIcon.SetShowHideCallback(   []() { g_islandVisible = !g_islandVisible; });
    g_trayIcon.SetExpandCallback(     []() { g_islandExpanded = !g_islandExpanded; });
    g_trayIcon.SetSettingsCallback(   []() { g_showSettings = true; });
    g_trayIcon.SetExitCallback(       []() { g_running = false; PostQuitMessage(0); });
    g_trayIcon.SetStartupCallback(    [](bool en) {
        g_config.GetBehavior().start_with_windows = en; g_config.Save();
        if (en) { TaskConfig tc; tc.delayStart = true; tc.delaySeconds = 30; tc.hidden = true; g_scheduler.Register(tc); }
        else g_scheduler.Unregister();
    });
    g_trayIcon.SetPerformanceCallback([](PerformanceMode) {});
    g_trayIcon.SetPositionCallback(   [](IslandPosition pos) {
        g_config.GetIsland().position = (pos == IslandPosition::TOP_CENTER) ? "top-center" :
                                        (pos == IslandPosition::TOP_LEFT) ? "top-left" : "follow-taskbar";
        g_config.Save();
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 20.0f; style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f; style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f; style.TabRounding = 8.0f;
    style.Alpha = g_config.GetAppearance().opacity;

    ImGui_ImplWin32_Init(g_mainWindow);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool startMin = g_config.GetBehavior().start_minimized;
    if (!silentStart && !startMin) { ::ShowWindow(g_mainWindow, SW_SHOW); g_windowVisible = true; }
    else if (startMin) { g_windowVisible = false; }
    else { ::ShowWindow(g_mainWindow, SW_SHOW); g_windowVisible = true; }

    RegisterHotKey(g_mainWindow, 1, MOD_CONTROL | MOD_SHIFT, 'Z');

    g_trayIcon.UpdateMenuState(g_windowVisible, g_islandExpanded,
        PerformanceMode::BALANCED, IslandPosition::TOP_CENTER,
        g_config.GetBehavior().start_with_windows);

    return true;
}

static void ShutdownApp() {
    g_config.Save();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_trayIcon.Shutdown();
    g_sysinfo.Shutdown();
    g_scheduler.Shutdown();
    CleanupDeviceD3D();
    if (g_mainWindow) { ::DestroyWindow(g_mainWindow); g_mainWindow = nullptr; }
    UnregisterHotKey(g_mainWindow, 1);
    ::UnregisterClass(L"DynamicIsland", GetModuleHandle(nullptr));
    CoUninitialize();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    FILE* el = fopen("dynamicisland_early.log", "w");
    if (el) { fprintf(el, "WinMain started\n"); fclose(el); }

    bool silentStart = false;
    for (int i = 1; i < __argc; ++i) {
        char arg[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, __wargv[i], -1, arg, 256, nullptr, nullptr);
        if (_stricmp(arg, "/background") == 0) silentStart = true;
    }

    HANDLE hMutex = CreateMutex(nullptr, FALSE, TEXT("Global\\DynamicIsland"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindow(L"DynamicIsland", nullptr);
        if (w) { ShowWindow(w, SW_SHOW); SetForegroundWindow(w); }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    if (!InitializeApp(silentStart)) { if (hMutex) CloseHandle(hMutex); return 1; }

    LOG_INFO("Entering main loop");
    MSG msg = {};
    auto lastTime = std::chrono::steady_clock::now();
    float animationY = 20.0f, targetY = 20.0f;

    while (g_running) {
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }
        if (!g_running) break;

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        const float animSpeed = 8.0f;
        animationY += (targetY - animationY) * dt * animSpeed;

        // Desktop / fullscreen detection
        bool isDesktop = false;
        HWND fgw = GetForegroundWindow();
        if (!fgw || fgw == GetDesktopWindow() || fgw == GetShellWindow()) isDesktop = true;
        else { wchar_t cn[256]; GetClassNameW(fgw, cn, 256);
               if (wcscmp(cn, L"Progman")==0 || wcscmp(cn, L"WorkerW")==0) isDesktop = true; }

        bool isFullscreen = false;
        if (!isDesktop && fgw) {
            WINDOWPLACEMENT wp; wp.length = sizeof(wp);
            if (GetWindowPlacement(fgw, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) isFullscreen = true;
            else { RECT r; if (GetWindowRect(fgw, &r)) {
                if (r.right-r.left >= GetSystemMetrics(SM_CXSCREEN)-10 &&
                    r.bottom-r.top >= GetSystemMetrics(SM_CYSCREEN)-10) isFullscreen = true;
            }}
        }
        if (isDesktop) isFullscreen = false;

        bool isMouseOver = IsMouseOverIsland();
        if (isFullscreen && !isMouseOver) {
            ImVec2 sz = g_islandExpanded ? ImVec2(600,300) : ImVec2(400,80);
            targetY = -sz.y + 10;
        } else targetY = 20.0f;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_islandVisible)
            DrawIslandUI(isDesktop, isFullscreen, isMouseOver, animationY, dt);
        DrawSettingsWindow();

        ImGui::Render();
        const float clear[4] = {0,0,0,0};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
        UpdateWindowGeometry();
    }

    ShutdownApp();
    if (hMutex) CloseHandle(hMutex);
    return 0;
}

#else // ======================== LINUX ================================
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "logging.h"
#include "config.h"
#include "sysinfo.h"
#include "trayicon.h"
#include "scheduler.h"
#include "window.h"
#include "ui.h"

static int g_lockFd = -1;

static bool AcquireSingleInstanceLock() {
    g_lockFd = open("/tmp/dynamicisland.lock", O_CREAT | O_RDWR, 0666);
    if (g_lockFd < 0) return false;
    struct flock fl = {};
    fl.l_type = F_WRLCK; fl.l_whence = SEEK_SET;
    if (fcntl(g_lockFd, F_SETLK, &fl) < 0) { close(g_lockFd); g_lockFd = -1; return false; }
    return true;
}

static void ReleaseSingleInstanceLock() {
    if (g_lockFd >= 0) {
        struct flock fl = {}; fl.l_type = F_UNLCK; fl.l_whence = SEEK_SET;
        fcntl(g_lockFd, F_SETLK, &fl); close(g_lockFd); g_lockFd = -1;
    }
    unlink("/tmp/dynamicisland.lock");
}

static bool InitializeApp(bool silentStart) {
    FILE* f = fopen("log/dynamicisland.log", "w");
    if (f) fclose(f);

    LOG_INFO("=== DynamicIsland Starting (Linux) ===");
    LOG_INFO("silentStart=%d", silentStart);
    Logger::Instance().SetConsoleOutput(true);

    if (!g_config.Load()) { LOG_INFO("Config not found, creating default"); g_config.Save(); }

    if (!g_sysinfo.Initialize()) { LOG_ERROR("Sysinfo init failed"); return false; }
    g_sysinfo.StartMonitoring();

    g_scheduler.Initialize();

    g_mainWindow = CreateMainWindow();
    if (!g_mainWindow) { LOG_ERROR("Window creation failed"); return false; }

    g_trayIcon.Initialize(g_mainWindow, 0);
    g_trayIcon.SetShowHideCallback(   []() { g_islandVisible = !g_islandVisible; });
    g_trayIcon.SetExpandCallback(     []() { g_islandExpanded = !g_islandExpanded; });
    g_trayIcon.SetSettingsCallback(   []() { g_showSettings = true; });
    g_trayIcon.SetExitCallback(       []() { g_running = false; });
    g_trayIcon.SetStartupCallback(    [](bool en) {
        g_config.GetBehavior().start_with_windows = en; g_config.Save();
        if (en) g_scheduler.Register(); else g_scheduler.Unregister();
    });

    bool startMin = g_config.GetBehavior().start_minimized;
    if (!silentStart && !startMin) { glfwShowWindow(g_mainWindow); g_windowVisible = true; }
    else { g_windowVisible = false; }

    LOG_INFO("Initialization complete");
    return true;
}

static void ShutdownApp() {
    g_config.Save();
    DestroySettingsWindow();
    ShutdownImGuiForWindow(g_mainCtx); g_mainCtx = nullptr;
    if (g_mainWindow) { glfwDestroyWindow(g_mainWindow); g_mainWindow = nullptr; }
    g_trayIcon.Shutdown();
    g_sysinfo.Shutdown();
    g_scheduler.Shutdown();
    glfwTerminate();
    ReleaseSingleInstanceLock();
}

static void RenderFrame(float animationY, float deltaTime, bool isFullscreen) {
    ImGui::SetCurrentContext(g_mainCtx);
    glfwMakeContextCurrent(g_mainWindow);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    bool isMouseOver = glfwGetWindowAttrib(g_mainWindow, GLFW_HOVERED);
    DrawIslandUI(isMouseOver, isFullscreen, animationY, deltaTime);

    ImGui::Render();
    int fbW, fbH;
    glfwGetFramebufferSize(g_mainWindow, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_mainWindow);
}

static void RenderSettingsFrame() {
    if (!g_settingsWindow) return;
    ImGui::SetCurrentContext(g_settingsCtx);
    glfwMakeContextCurrent(g_settingsWindow);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    DrawSettingsWindow();

    ImGui::Render();
    int fbW, fbH;
    glfwGetFramebufferSize(g_settingsWindow, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.13f,0.13f,0.17f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_settingsWindow);
}

int main(int argc, char** argv) {
    FILE* el = fopen("dynamicisland_early.log", "w");
    if (el) { fprintf(el, "main started (Linux)\n"); fclose(el); }

    bool silentStart = false;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--background") == 0 || strcmp(argv[i], "/background") == 0)
            silentStart = true;

    if (!AcquireSingleInstanceLock()) { fprintf(stderr, "Another instance running.\n"); return 0; }
    if (!InitializeApp(silentStart)) { ReleaseSingleInstanceLock(); return 1; }

    LOG_INFO("Entering main loop");
    auto lastTime = std::chrono::steady_clock::now();
    float animationY = 20.0f, targetY = 20.0f;
    int curW = 400, curH = 80;

    while (g_running && !glfwWindowShouldClose(g_mainWindow)) {
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        const float animSpeed = 8.0f;
        animationY += (targetY - animationY) * dt * animSpeed;

        bool isFullscreen = (glfwGetWindowMonitor(g_mainWindow) != nullptr);
        bool isMouseOver = glfwGetWindowAttrib(g_mainWindow, GLFW_HOVERED);

        if (isFullscreen && !isMouseOver) targetY = -curH + 10;
        else targetY = 20.0f;

        int tW = g_islandExpanded ? 600 : 400;
        int tH = g_islandExpanded ? 300 : 80;
        if (tW != curW || tH != curH) { curW = tW; curH = tH; glfwSetWindowSize(g_mainWindow, curW, curH); }

        if (g_islandVisible && !glfwGetWindowAttrib(g_mainWindow, GLFW_VISIBLE))
            glfwShowWindow(g_mainWindow);
        else if (!g_islandVisible && glfwGetWindowAttrib(g_mainWindow, GLFW_VISIBLE))
            glfwHideWindow(g_mainWindow);

        if (g_islandVisible) {
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vid = glfwGetVideoMode(mon);
            glfwSetWindowPos(g_mainWindow, (vid->width - curW)/2, (int)animationY);
        }

        if (g_islandVisible)
            RenderFrame(animationY, dt, isFullscreen);

        if (g_showSettings && !g_settingsWindow)
            g_settingsWindow = CreateSettingsWindow(650, 420);
        else if (!g_showSettings && g_settingsWindow)
            DestroySettingsWindow();

        if (g_settingsWindow) {
            if (glfwWindowShouldClose(g_settingsWindow)) {
                g_showSettings = false; DestroySettingsWindow();
            } else RenderSettingsFrame();
        }
    }

    ShutdownApp();
    return 0;
}
#endif
