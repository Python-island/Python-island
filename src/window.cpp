// ===================================================================
// window.cpp — Platform-specific windowing & rendering
//   Windows: Win32 fullscreen overlay + D3D11
//   Linux:   GLFW multi-window + OpenGL 3
// ===================================================================

#include "window.h"
#include "logging.h"

#ifdef _WIN32
// ========================== WINDOWS ================================
#include <dwmapi.h>
#include <d3d11.h>
#include <ole2.h>
#include <oleidl.h>
#include <shlobj.h>

#ifndef DWMWA_USE_HOSTBACKDROPBRUSH
#define DWMWA_USE_HOSTBACKDROPBRUSH 38
#endif
#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

#include "config.h"
#include <imgui.h>
#include "sysinfo.h"
#if USE_FILE_TRANSFER
#include "transferstation.h"
#endif
#include "trayicon.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
HWND                    g_mainWindow = nullptr;
bool g_running = true, g_windowVisible = true, g_islandVisible = true, g_islandExpanded = false, g_showSettings = false;
const UINT WM_TRAYICON = WM_APP + 1;

void* GetMainWindowHandle() { return g_mainWindow; }

class CDropSource : public IDropSource {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IDropSource) { *ppv = static_cast<IDropSource*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() { ULONG r = InterlockedDecrement(&m_refCount); if (r==0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD ks) { if (esc) return DRAGDROP_S_CANCEL; if (!(ks & MK_LBUTTON)) return DRAGDROP_S_DROP; return S_OK; }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

HRESULT CreateFileDropDataObject(const std::vector<std::wstring>& paths, IDataObject** ppdo) {
    std::vector<LPITEMIDLIST> pidls;
    for (auto& p : paths) { LPITEMIDLIST pidl = ILCreateFromPathW(p.c_str()); if (pidl) pidls.push_back(pidl); else { for (auto x: pidls) ILFree(x); return E_FAIL; } }
    HRESULT hr = SHCreateDataObject(nullptr, (UINT)pidls.size(), const_cast<const ITEMIDLIST**>(pidls.data()), nullptr, IID_IDataObject, (void**)ppdo);
    for (auto x: pidls) ILFree(x); return hr;
}

void UpdateWindowGeometry() {
    if (!g_mainWindow) return;
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_mainWindow, HWND_TOPMOST, 0, 0, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    BOOL ub = TRUE; DwmSetWindowAttribute(g_mainWindow, DWMWA_USE_HOSTBACKDROPBRUSH, &ub, sizeof(ub));
    MARGINS m = {-1}; DwmExtendFrameIntoClientArea(g_mainWindow, &m);
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    D3D_FEATURE_LEVEL fl; const D3D_FEATURE_LEVEL fla[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget(); return true;
}

void CreateRenderTarget() {
    ID3D11Texture2D* bb; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    D3D11_RENDER_TARGET_VIEW_DESC rtd; rtd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rtd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rtd.Texture2D.MipSlice = 0;
    g_pd3dDevice->CreateRenderTargetView(bb, &rtd, &g_mainRenderTargetView); bb->Release();
}

void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && g_showSettings) { g_showSettings = false; return 0; }
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); }
        UpdateWindowGeometry(); return 0;
    case WM_NCHITTEST: {
        POINT pt = {LOWORD(lParam), HIWORD(lParam)}; ScreenToClient(hWnd, &pt);
        RECT rc; GetClientRect(hWnd, &rc);
        RECT dr = {(rc.right-rc.left-100)/2, 10, (rc.right-rc.left-100)/2+100, 30};
        return PtInRect(&dr, pt) ? HTCAPTION : HTTRANSPARENT;
    }
    case WM_TRAYICON: g_trayIcon.HandleMessage(wParam, lParam); return 0;
    case WM_COMMAND:  g_trayIcon.HandleMessage(LOWORD(wParam), MAKELPARAM(WM_COMMAND, 0)); return 0;
    case WM_DISPLAYCHANGE: g_sysinfo.UpdateDisplayInfo(); return 0;
    case WM_HOTKEY: if (wParam == 1) { PostQuitMessage(0); g_running = false; } return 0;
    case WM_DESTROY: PostQuitMessage(0); g_running = false; return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

HWND CreateMainWindow() {
    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"DynamicIsland", nullptr};
    RegisterClassEx(&wc);
    HWND h = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE, wc.lpszClassName, L"DynamicIsland", WS_POPUP,
                            0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, wc.hInstance, nullptr);
    return h;
}

#else // ======================== LINUX ================================
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#if USE_FILE_TRANSFER
#include "transferstation.h"
#endif

#include <cstdio>

static const char* g_GlslVersion = "#version 150";

GLFWwindow*  g_mainWindow    = nullptr;
GLFWwindow*  g_settingsWindow = nullptr;
ImGuiContext* g_mainCtx       = nullptr;
ImGuiContext* g_settingsCtx   = nullptr;
bool g_running = true, g_windowVisible = true, g_islandVisible = true, g_islandExpanded = false, g_showSettings = false;

void* GetMainWindowHandle() { return g_mainWindow; }

void GlfwErrorCallback(int error, const char* desc) { fprintf(stderr, "GLFW Error %d: %s\n", error, desc); }

ImGuiContext* InitImGuiForWindow(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 20; s.FrameRounding = 8; s.GrabRounding = 8; s.PopupRounding = 8; s.ScrollbarRounding = 8; s.TabRounding = 8;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(g_GlslVersion);
    return ctx;
}

void ShutdownImGuiForWindow(ImGuiContext* ctx) {
    if (!ctx) return;
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(ctx);
}

void MainKeyCallback(GLFWwindow*, int key, int, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) g_running = false;
        if (key == GLFW_KEY_ESCAPE && g_showSettings) g_showSettings = false;
    }
}

void SettingsKeyCallback(GLFWwindow*, int key, int, int action, int) {
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_showSettings = false;
}

#if USE_FILE_TRANSFER
static void IslandDropCallback(GLFWwindow*, int count, const char** paths) {
    for (int i = 0; i < count; i++) g_transferstation.AddFile(paths[i]);
}
#endif

GLFWwindow* CreateMainWindow() {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) { LOG_ERROR("GLFW init failed"); return nullptr; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* w = glfwCreateWindow(400, 80, "DynamicIsland", nullptr, nullptr);
    if (!w) { glfwTerminate(); return nullptr; }

    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vid = glfwGetVideoMode(mon);
    glfwSetWindowPos(w, (vid->width - 400)/2, 20);
    glfwMakeContextCurrent(w); glfwSwapInterval(1);
    glfwSetKeyCallback(w, MainKeyCallback);
#if USE_FILE_TRANSFER
    glfwSetDropCallback(w, IslandDropCallback);
#endif
    g_mainCtx = InitImGuiForWindow(w);
    LOG_INFO("Main window created: 400x80");
    return w;
}

GLFWwindow* CreateSettingsWindow(int w, int h) {
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE); glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(w, h, "DynamicIsland Settings", nullptr, nullptr);
    if (!win) return nullptr;
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vid = glfwGetVideoMode(mon);
    glfwSetWindowPos(win, (vid->width-w)/2, (vid->height-h)/2);
    glfwMakeContextCurrent(win); glfwSwapInterval(1);
    glfwSetKeyCallback(win, SettingsKeyCallback);
    g_settingsCtx = InitImGuiForWindow(win);
    return win;
}

void DestroySettingsWindow() {
    if (!g_settingsWindow) return;
    glfwMakeContextCurrent(g_settingsWindow);
    ShutdownImGuiForWindow(g_settingsCtx); g_settingsCtx = nullptr;
    glfwDestroyWindow(g_settingsWindow); g_settingsWindow = nullptr;
}

void UpdateWindowGeometry() { /* no-op on Linux — window manager handles it */ }
#endif
