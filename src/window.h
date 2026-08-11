#pragma once

// === Platform-specific includes & types ===
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>

extern ID3D11Device*           g_pd3dDevice;
extern ID3D11DeviceContext*    g_pd3dDeviceContext;
extern IDXGISwapChain*         g_pSwapChain;
extern ID3D11RenderTargetView* g_mainRenderTargetView;
extern HWND                    g_mainWindow;
extern const UINT              WM_TRAYICON;

bool  CreateDeviceD3D(HWND hWnd);
void  CreateRenderTarget();
void  CleanupRenderTarget();
void  CleanupDeviceD3D();
HWND  CreateMainWindow();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#else // __linux__
#include <GLFW/glfw3.h>
#include "imgui.h"

extern GLFWwindow*  g_mainWindow;
extern GLFWwindow*  g_settingsWindow;
extern ImGuiContext* g_mainCtx;
extern ImGuiContext* g_settingsCtx;

GLFWwindow*   CreateMainWindow();
GLFWwindow*   CreateSettingsWindow(int w, int h);
void          DestroySettingsWindow();
ImGuiContext* InitImGuiForWindow(GLFWwindow* window);
void          ShutdownImGuiForWindow(ImGuiContext* ctx);
void          MainKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void          SettingsKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void          GlfwErrorCallback(int error, const char* description);
#endif

// === Platform-agnostic globals ===
extern bool g_running;
extern bool g_windowVisible;
extern bool g_islandVisible;
extern bool g_islandExpanded;
extern bool g_showSettings;

// === Platform-agnostic functions ===
void  UpdateWindowGeometry();
void* GetMainWindowHandle();
