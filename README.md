# Pyisland - 现代灵动岛控制中心

Pyisland是一个基于PySide6开发的现代化灵动岛控制中心，提供直观的系统亮度控制、系统状态监控以及系统托盘功能。

## 功能特点

- **灵动岛设计**：采用现代胶囊形状设计，初始状态显示时间，点击展开显示控制选项
- **亮度调节**：通过滑动条调节系统亮度，支持防抖机制，避免频繁调节
- **系统状态监控**：显示当前WiFi连接、蓝牙设备和电池状态
- **系统托盘**：支持最小化到系统托盘，后台运行
- **设置窗口**：通过托盘菜单打开设置窗口
- **URL检测**：自动检测剪贴板中的URL并提供打开选项
- **鼠标拖动**：支持鼠标拖动调整灵动岛位置
- **自动收缩**：展开后失去焦点自动收缩
- **实时时间显示**：初始状态显示当前系统时间

## 项目结构

```
PyislandWeb/
├── app/
│   ├── __init__.py           # 包初始化
│   ├── island.py             # 主窗口，整合所有模块
│   ├── utils.py              # 工具函数（向后兼容）
│   ├── core/                 # 核心功能模块
│   │   ├── __init__.py
│   │   ├── worker.py         # 后台工作线程
│   │   └── config.py         # 配置常量
│   ├── ui/                   # UI组件模块
│   │   ├── __init__.py
│   │   ├── controls.py       # 控制面板组件
│   │   ├── status_bar.py     # 状态栏组件
│   │   ├── url_dialog.py     # URL检测对话框
│   │   └── settings.py       # 设置窗口
│   ├── services/             # 服务模块
│   │   ├── __init__.py
│   │   ├── clipboard.py      # 剪贴板服务
│   │   ├── system_status.py  # 系统状态服务
│   │   ├── brightness.py     # 亮度控制服务
│   │   └── tray.py           # 托盘服务
│   └── animations/           # 动画效果模块
│       ├── __init__.py
│       └── effects.py        # 展开/收起动画
├── resources/
│   ├── icons/                # 图标资源
│   │   ├── controls/         # 控制相关图标
│   │   │   ├── light.png     # 亮度图标
│   │   │   ├── volume.png    # 音量图标
│   │   │   └── tray.png      # 托盘图标
│   │   └── system/           # 系统状态图标
│   │       ├── battery.png    # 电池图标
│   │       ├── bluetooth.png  # 蓝牙图标
│   │       └── internet.png   # 网络图标
│   └── styles/
│       └── style.qss         # QSS样式文件
├── main.py                   # 应用入口
└── README.md                 # 项目说明文档
```

## 安装和运行

### 安装依赖

```bash
# 安装PySide6
pip install PySide6

# 安装其他依赖（如果需要）
pip install comtypes  # 用于音量控制的COM接口
pip install screen_brightness_control  # 用于亮度控制
```

### 运行应用

```bash
python main.py
```

## 使用说明

### 系统托盘

- **单击托盘图标**：打开设置窗口
- **双击托盘图标**：打开设置窗口
- **右键托盘图标**：显示菜单
  - 设置：打开设置窗口
  - 退出：退出应用

### 灵动岛

- **点击灵动岛**：展开/收起控制面板
- **拖动灵动岛**：调整位置
- **亮度滑块**：调节系统亮度

## 配置和修改指南

### 1. 界面样式修改

修改 `resources/styles/style.qss` 文件可以调整界面样式：

- **灵动岛主体**：修改 `#IslandContainer` 样式
- **时间显示**：修改 `#TimeLabel` 样式
- **滑动条**：修改 `QSlider#CapsuleSlider` 相关样式
- **状态栏**：修改 `#StatusLabel` 样式

### 2. 功能调整

#### 调整防抖时间

在 `app/core/config.py` 文件中修改：

```python
DEBOUNCE_DELAY = 180  # 防抖时间，单位毫秒
```

#### 调整状态栏更新频率

在 `app/core/config.py` 文件中修改：

```python
STATUS_UPDATE_INTERVAL = 5000  # 更新频率，单位毫秒
```

#### 添加新功能

1. 在 `app/services/` 目录下创建新的服务类
2. 在 `app/ui/` 目录下创建新的UI组件
3. 在 `app/core/config.py` 中添加配置常量
4. 在 `app/island.py` 中整合新模块
5. 在 `resources/styles/style.qss` 中添加相应的样式

### 3. 图标替换

将自定义图标替换到 `resources/icons/` 目录下，确保文件名与代码中的引用一致：
- 亮度图标：`controls/light.png`
- 音量图标：`controls/volume.png`
- 托盘图标：`controls/tray.png`

## 模块说明

### 核心模块 (core/)

| 模块 | 说明 |
|------|------|
| `worker.py` | 后台工作线程，执行耗时操作 |
| `config.py` | 配置常量，包括尺寸、路径、时间间隔等 |

### UI模块 (ui/)

| 模块 | 说明 |
|------|------|
| `controls.py` | 控制面板组件，亮度/音量滑块 |
| `status_bar.py` | 状态栏组件，WiFi/蓝牙/电池状态 |
| `url_dialog.py` | URL检测对话框，处理剪贴板URL |
| `settings.py` | 设置窗口，应用配置界面 |

### 服务模块 (services/)

| 模块 | 说明 |
|------|------|
| `clipboard.py` | 剪贴板服务，URL检测和打开 |
| `system_status.py` | 系统状态服务，WiFi/蓝牙/电池信息 |
| `brightness.py` | 亮度控制服务，获取和设置系统亮度 |
| `tray.py` | 托盘服务，系统托盘图标和菜单 |

### 动画模块 (animations/)

| 模块 | 说明 |
|------|------|
| `effects.py` | 动画效果，展开/收起动画、圆角遮罩 |

## 技术栈

- **Python 3.7+**
- **PySide6**：Qt的Python绑定，用于GUI开发
- **QSS (Qt Style Sheets)**：用于界面样式定制
- **Windows API**：通过PowerShell和ctypes调用系统功能

## 注意事项

1. **系统兼容性**：目前仅支持Windows系统，因为使用了Windows特定的API调用
2. **权限要求**：可能需要管理员权限才能修改系统亮度
3. **图标路径**：确保 `resources/icons/` 目录存在且包含所需图标
4. **PowerShell**：需要系统安装PowerShell以获取系统状态信息

## 故障排除

### 常见错误

1. **`ModuleNotFoundError: No module named 'PySide6'`**
   - 原因：未安装PySide6
   - 解决方案：`pip install PySide6`

2. **`ModuleNotFoundError: No module named 'app'`**
   - 原因：直接运行了子模块文件
   - 解决方案：通过 `python main.py` 运行应用

3. **托盘图标不显示**
   - 原因：图标文件路径错误
   - 解决方案：确保 `resources/icons/controls/tray.png` 存在

### 调试技巧

- 查看控制台输出获取详细错误信息
- 检查PowerShell命令是否能在系统中正常执行
- 确保图标文件路径正确

## 贡献指南

欢迎提交问题和改进建议！如果您想为项目贡献代码，请：

1. Fork项目仓库
2. 创建功能分支
3. 提交更改
4. 推送到分支
5. 打开Pull Request

## 许可证

本项目采用MIT许可证。

---

希望这个项目能为您提供一个现代化、直观的系统控制界面！如有任何问题，欢迎提出。
