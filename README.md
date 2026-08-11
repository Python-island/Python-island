# DynamicIsland-imgui

一个基于 Dear ImGui 的跨平台桌面应用，模拟 macOS "灵动岛"（Dynamic Island）效果并集成系统监控与文件中转站。

**Windows**：Win32 + Direct3D 11 全屏透明叠加层  
**Linux**：GLFW + OpenGL 3 原生多窗口架构

## 项目地址

本项目是 Python-island 主项目的 imgui 分支：
[https://github.com/Python-island/Python-island/tree/pyisland-imgui](https://github.com/Python-island/Python-island/tree/pyisland-imgui)

## 功能特性

### 核心功能
- **实时系统监控**：
  - CPU 利用率（总体 + 每核心）+ 频率
  - GPU 利用率、显存、温度（NVIDIA NVML / AMD / Intel）
  - 内存使用情况
  - 电池状态与剩余时间
  - 网络带宽统计（上下行速率，移动平均平滑）
  - 时钟显示（含秒）
- **灵动岛界面**：可折叠/展开的动态信息面板，全屏时自动隐藏
- **系统托盘**：Windows 托盘图标 / Linux 键盘快捷键
- **设置窗口**：独立设置窗口，分类配置（通用、外观、通知、高级）
- **文件中转站**：文件暂存管理，支持拖放导入

### 界面特性
- 半透明圆角背景
- 展开/收起平滑动画
- 暗色/亮色主题切换
- 鼠标穿透（岛外区域不拦截点击）

## 系统要求

### Windows
- Windows 10 / 11（32/64 位）
- Direct3D 11（系统自带）
- MinGW‑w64（g++）或 Visual Studio 2019+
- CMake ≥ 3.20, Ninja

### Linux
- 内核 ≥ 3.2, X11 显示服务
- OpenGL 3.2+, GLFW 3
- CMake ≥ 3.20, Ninja, g++ (C++17)
- 可选：NVIDIA 专有驱动（用于 GPU 监控）、zenity（文件导入对话框）

安装依赖（Debian/Ubuntu）：
```bash
sudo apt install cmake ninja-build g++ libglfw3-dev libgl-dev
```

## 构建方法

项目使用同一套源码 + `#ifdef _WIN32` 条件编译，CMake 根据平台自动选择后端和依赖库。

### Linux

```bash
git clone https://github.com/Python-island/Python-island.git
cd Python-island && git checkout pyisland-imgui
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./DynamicIsland
```

### Windows (MinGW‑w64)

```bash
git clone https://github.com/Python-island/Python-island.git
cd Python-island && git checkout pyisland-imgui
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
DynamicIsland.exe
```

> 如需调试构建，将 `-DCMAKE_BUILD_TYPE=Release` 替换为 `Debug`。

## 使用方法

### 基本操作
- **左键点击灵动岛**：展开/收起
- **右键点击灵动岛**：打开上下文菜单（展开/收起、隐藏/显示、设置、退出）
- **ESC 键**：关闭设置窗口

### 快捷操作
- **Ctrl+Shift+Z**：退出程序

### 上下文菜单
- **Expand / Collapse**：切换面板大小
- **Hide Island / Show Island**：显示/隐藏灵动岛
- **Settings**：打开设置窗口
- **Exit**：退出程序

## 设置窗口

| 分类 | 内容 |
|---|---|
| **通用** | 开机启动、启动最小化、刷新频率 |
| **外观** | 透明度滑块、暗色/亮色主题 |
| **通知** | 通知开关、最大通知数量 |
| **文件中转站** | 存储限制配置 |
| **高级** | 调试控制台、重置默认设置 |
| **关于** | 版本信息与技术栈 |

## 文件中转站

展开灵动岛后，下半部分即为文件中转站。支持：
- **拖放导入**（Linux 需 `zenity` 文件选择器）
- 文件列表：图标、名称、大小、日期
- **双击**打开文件，**右键**打开操作菜单（打开/删除/复制路径）
- 批量操作按钮：导入、打开选中、移除选中、清空

文件中转站默认启用，如需禁用，修改 `CMakeLists.txt` 中：

```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_FILE_TRANSFER=0   # 0=禁用, 1=启用
)
```

## 跨平台架构

```
源码层（src/*.cpp / *.h）
  └─ #ifdef _WIN32   → Windows 实现
  └─ #else           → Linux 实现

CMakeLists.txt
  └─ if(WIN32)       → imgui_impl_win32 + dx11, D3D11/DWM/PDH/COM 库
  └─ else()          → imgui_impl_glfw + opengl3, GLFW/OpenGL
```

| 模块 | Windows | Linux |
|---|---|---|
| 渲染 | Direct3D 11 + 全屏透明叠加层 | OpenGL 3 + GLFW 原生多窗口 |
| 窗口管理 | `CreateWindowEx(WS_EX_TOPMOST\|WS_EX_TRANSPARENT)` | `glfwCreateWindow`（岛 + 设置独立窗口） |
| 鼠标穿透 | `WM_NCHITTEST` 返回 `HTTRANSPARENT` | 窗口自然边界（无需额外处理） |
| CPU | PDH（性能计数器） | `/proc/stat` |
| GPU | NVML + DXGI + WMI | NVML（`libnvidia-ml.so`）+ `/sys/class/drm` |
| 内存 | `GlobalMemoryStatusEx` | `/proc/meminfo` |
| 电池 | `GetSystemPowerStatus` | `/sys/class/power_supply/BAT*` |
| 网络 | `GetIfTable` | `/proc/net/dev` |
| 托盘 | `Shell_NotifyIcon` + GDI+ | 键盘快捷键 |
| 开机启动 | Task Scheduler COM | `~/.config/autostart/*.desktop` |
| 文件操作 | `ShellExecuteA` / Win32 API | `xdg-open` / `std::filesystem` |

## 配置文件

程序使用 `config.json` 保存设置，首次运行自动创建：

```json
{
  "island": {
    "position": "top-center",
    "offset_x": 0,
    "offset_y": 20,
    "idle_width": 120, "idle_height": 40,
    "expanded_width": 380, "expanded_height": 450,
    "animation_speed": 12.0,
    "auto_hide_delay": 5.0,
    "show_seconds": false
  },
  "appearance": {
    "theme": "dark",
    "accent_color": "#0078D4",
    "opacity": 0.95,
    "corner_radius": 20.0,
    "shadow_enabled": true,
    "blur_enabled": true,
    "font_size": 16,
    "font_family": "Noto Sans CJK SC",
    "style": "frosted"
  },
  "system": {
    "update_interval_ms": 1000,
    "cpu_enabled": true,
    "gpu_enabled": true,
    "memory_enabled": true,
    "battery_enabled": true,
    "network_enabled": false
  },
  "behavior": {
    "start_with_windows": true,
    "start_minimized": false,
    "silent_mode": false,
    "game_mode_detection": true,
    "notification_enabled": true,
    "max_notifications": 5
  }
}
```

## 项目结构

```
/
├── CMakeLists.txt              # 跨平台构建配置
├── include/imgui/              # Dear ImGui 库（含所有后端）
├── src/                        # 应用源码
│   ├── main.cpp                # 入口点 & 主循环
│   ├── window.h / window.cpp   # 窗口管理 & 渲染设备
│   ├── ui.h / ui.cpp           # 灵动岛 & 设置 UI
│   ├── sysinfo.h / sysinfo.cpp # 系统指标采集
│   ├── trayicon.h / trayicon.cpp   # 托盘图标
│   ├── scheduler.h / scheduler.cpp # 开机启动
│   ├── transferstation.h / transferstation.cpp  # 文件中转站
│   ├── config.h / config.cpp   # 配置管理 (JSON)
│   ├── logging.h / Logger.cpp  # 日志系统
│   ├── island.h                # 状态机定义
│   └── mingw_compat.h          # MinGW 兼容层 (Windows only)
├── assets/                     # 资源文件
└── README.md
```

## 常见问题

### 程序无法启动
- 确保系统满足最低要求
- Windows：确保 DirectX 11 可用
- Linux：确保 OpenGL 3.2+ 和 GLFW 已安装

### 灵动岛不显示
- 检查是否被其他窗口遮挡
- Windows：检查系统托盘图标
- Linux：使用 Ctrl+Shift+Z 退出后重启

### 鼠标行为
- **Windows**：全屏透明窗口 + `WM_NCHITTEST` 实现点击穿透，仅岛区域可交互
- **Linux**：岛和设置各为独立原生窗口，窗口管理器自然处理输入路由

## 开发说明

- **UI**：Dear ImGui 即时模式，字体渲染，样式统一管理
- **日志**：`log/dynamicisland.log`，支持控制台输出（调试用）
- **监控线程**：独立后台线程采集系统数据，互斥锁保护线程安全
- **跨平台策略**：`#ifdef _WIN32` 条件编译 + CMake 平台分支，同一份源码双平台构建

## 致谢

- **Dear ImGui** — Omar Cornut 的即时模式 GUI 库
- **GLFW** — 跨平台 OpenGL 窗口库（Linux 后端）
- **DirectX** — Microsoft 图形 API（Windows 后端）
- **NVIDIA NVML** — GPU 监控接口

---

*如果您喜欢这个项目，请给它一个星标 ⭐ 支持一下！*
