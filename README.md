<div align="center">

<img src="docs/assets/top-icon.png" alt="top 图标" width="80" height="80">

# top

**Windows 桌面监控 · Android 桌面卡片**

查看资源占用，找到正在消耗资源的应用。

[快速开始](#快速开始) · [功能介绍](#功能介绍) · [使用说明](#使用说明) · [来源与致谢](#来源与致谢)

</div>

---

top 提供 Windows 原生版、Windows Python/Qt 版和 Android 小米版。各版本采用对应平台的窗口与交互方式；数据范围随系统权限和硬件支持而变化。读不到的指标显示 `—`，估算功耗标注 `≈`。

> **首次使用：** 此仓库提供源码，尚未提供可直接下载的应用安装包。希望先运行 Windows 面板，可使用 Python/Qt 版；需要原生程序和态势总览，可构建 C++ 版。

## 选择版本

| 版本 | 适合的使用方式 | 环境与入口 |
| --- | --- | --- |
| **Windows 原生版** | 悬浮面板、托盘指标、应用资源态势总览 | Windows x64；构建后运行 `native/dist/top.exe`；需要管理员权限 |
| **Windows Python/Qt 版** | 从源码启动，查看当前资源状态 | Windows x64 + Python；运行 `python/start-ftop.ps1` |
| **Android 小米版** | 桌面卡片、应用与进程详情、应用流量 | Android 12+；构建并安装 `xiaomi/dist/ftop-xiaomi.apk` |

macOS 用户请访问原作者的 [Nongfsq/ftop](https://github.com/Nongfsq/ftop)。本仓库为独立移植与扩展，与上游分别维护。

## 功能介绍

| 功能 | Windows 原生版 | Python/Qt 版 | Android 小米版 |
| --- | --- | --- | --- |
| 日常监控 | CPU、内存、GPU、磁盘、网络 | CPU、内存、GPU、网络 | 内存、电池、网络；CPU/GPU 受设备限制 |
| 应用与进程 | 应用归组、排序、进程详情 | 应用归组、排序、进程详情 | 搜索、筛选、进程详情；完整范围需 Shizuku |
| 桌面入口 | 可缩放悬浮面板、托盘指标 | 可缩放悬浮面板、托盘数字 | 监控卡片、进程卡片 |
| 资源态势 | 应用与资源关系、短时回看、暂停与固定 | — | — |
| 硬件信息 | 支持范围内的温度、功耗、内存带宽、风扇 | NVIDIA GPU 相关指标 | 设备、存储及可读取的核心信息 |
| 外观 | 深浅主题、中文/英文、动效开关 | 深浅主题、中文/英文、动效开关 | 跟随系统、深色、浅色 |

**Windows 原生版：** 窗口随尺寸调整布局。精简面板适合常驻查看；态势总览用于观察应用与 CPU、GPU、内存、磁盘、网络之间的资源关系。悬停查看详情，点击固定应用，暂停后可回看最多 60 帧采样。

**Python/Qt 版：** 保留当前状态面板、逐逻辑处理器柱形图、稳定进程排序与独立详情卡。首次启动脚本会创建虚拟环境并安装依赖。

**Android 小米版：** 提供桌面卡片和应用内详情。连接 Shizuku 后，可读取更多进程和应用流量，并在系统权限允许时执行经确认的应用停止操作。卡片更新受 Android 后台策略限制。

## 快速开始

先下载仓库源码，或执行：

```powershell
git clone https://github.com/suiyunzou/top.git
cd top
```

以下命令均从仓库根目录执行。

### Windows：先运行 Python/Qt 版

准备 Windows 10/11 和 64 位 Python 3.12，确保 `python` 可在终端使用。

```powershell
.\python\start-ftop.ps1
```

首次启动需要联网安装依赖。安装完成后显示悬浮面板；点击托盘数字可显示或隐藏窗口。详细用法见 [Python/Qt 版说明](python/README-ftop-windows.md)。

<details>
<summary><strong>Windows：构建 C++ 原生版</strong></summary>

准备支持 C++17 的 MinGW-w64 x64 工具链，其中应包含 `g++.exe` 和 `windres.exe`。将下面的示例路径改成实际安装位置。

```powershell
.\native\build.ps1 -Compiler 'C:\tools\mingw64\bin\g++.exe'
.\native\dist\top.exe
```

构建脚本会校验随源码附带的 PawnIO 安装程序及模块，并运行原生自检。自检与应用启动可能请求 UAC 授权。

原生程序不依赖 Python 或 Qt。首次运行如缺少 PawnIO 驱动，会运行内置官方安装程序。详见 [原生版说明](native/README.md) 和 [指标口径](native/TELEMETRY.md)。

</details>

<details>
<summary><strong>Android：构建并安装桌面卡片</strong></summary>

准备 JDK 17、Android SDK Platform 35、Build Tools 36.0.0。将示例路径改成实际安装位置。

```powershell
.\xiaomi\build.ps1 -Sdk 'C:\tools\AndroidSDK' -Jdk 'C:\tools\jdk17'
```

将生成的 `xiaomi/dist/ftop-xiaomi.apk` 复制到手机安装。打开应用，在设置中选择“添加监控卡片”或“添加进程卡片”，再确认系统提示。

首次构建会下载并校验固定版本的 Shizuku 依赖，并生成本地开发签名。请保管本地签名密钥；不同签名的 APK 不能直接覆盖安装。高级进程功能需要单独安装、启动并授权 Shizuku。详见 [Android 版说明](xiaomi/README.md)。

</details>

## 使用说明

| 想做什么 | 操作 |
| --- | --- |
| 移动或缩放 Windows 面板 | 拖动空白区域，或拖动窗口边缘 |
| 显示或隐藏 Windows 面板 | 点击托盘图标 |
| 打开原生版设置 | 右键点击托盘图标 |
| 切换原生版态势总览 | 点击面板右上角切换图标，或运行 `top.exe situation` |
| 查看不可用指标的说明 | 悬停或点击指标；具体支持范围见各版本文档 |
| 更新 Android 桌面卡片 | 点击卡片“刷新”；点击主体进入实时详情 |

### 权限与数据

- **Windows 原生版需要管理员权限。** 权限用于硬件读取和部分系统采样；取消 UAC 后不会启动。应用不会关闭系统驱动签名校验。
- **硬件支持有范围。** 原生 CPU 温度与封装功耗目前限定于支持的 Intel Comet Lake 型号；普通面板的 GPU 传感器使用 NVIDIA NVML。其他硬件的部分读数可能不可用。
- **Android 基础功能无需 Root。** Shizuku 授权可扩展读取范围，但不能绕过所有系统限制。手机重启后通常需要重新启动 Shizuku。
- **数值含义因版本而异。** 原生版进程 CPU 按整机 100% 计算；Python/Qt 与 Android 进程 CPU 按单核 100% 计算。进程内存含共享页，不能直接相加作为整机内存占用。
- **配置留在本机。** Windows 使用 `%APPDATA%/ftop` 与 `%APPDATA%/ftop-native`；Android 使用应用私有存储。提交反馈前，请遮盖进程名、路径、设备标识和日志中的私人信息。

## 常见问题

<details>
<summary>为什么温度、功耗或进程读数显示横线？</summary>

可能是硬件尚未适配、权限不足、驱动不可用，或正在等待差分采样的第一组有效数据。横线表示未知，不等于零。功耗前的 `≈` 表示估算值，不是外部功率计测量结果。

</details>

<details>
<summary>为什么手机桌面卡片没有持续每秒刷新？</summary>

Android 会限制后台组件更新。桌面卡片支持手动刷新；需要连续观察时，请打开应用内的对应页面。系统省电策略可能进一步延后卡片更新。

</details>

<details>
<summary>在哪里下载 EXE 或 APK？</summary>

本次发布提供源码，尚未发布应用安装包。请使用上面的启动或构建步骤。不要将上游 macOS 安装包用于 Windows 或 Android。

</details>

## 项目结构

| 目录 | 内容 |
| --- | --- |
| [`native/`](native/) | C++/Win32 源码、构建脚本、测试与第三方许可 |
| [`python/`](python/) | Python/Qt 源码、启动脚本、打包工具与测试 |
| [`xiaomi/`](xiaomi/) | Android Java 源码、桌面卡片资源、构建脚本与测试 |

遇到问题可提交 [Issue](https://github.com/suiyunzou/top/issues)，注明使用版本、系统版本、复现步骤和预期结果。请勿附带密钥、私人配置或未经脱敏的桌面截图。

## 来源与致谢

本项目的设计、布局选择、动效与稳定排序思路参考并改编自 **[Nongfsq](https://github.com/Nongfsq)** 的 **[ftop](https://github.com/Nongfsq/ftop)**。上游面向 Apple Silicon Mac；本仓库提供独立的 Windows 与 Android 实现，不是原作者的官方发行版。

保留上游 MIT 许可与版权声明，见 [上游许可证](native/licenses/LICENSE-upstream.txt)。第三方组件各自遵循对应许可，不将整个仓库内的所有组件统一声明为 MIT：

| 组件 | 用途与许可资料 |
| --- | --- |
| Nongfsq/ftop | 设计与算法参考；[Windows 移植说明](python/ftop_windows/NOTICE.txt) |
| nlohmann/json | C++ JSON 解析；[MIT 许可](native/licenses/LICENSE-json.txt) |
| PawnIO 与签名模块 | 支持范围内的硬件读取；[来源、版本与许可证](native/vendor/pawnio/README.md) |
| Qt for Python / PySide6 | Python 版界面；[依赖声明](python/ftop_windows/NOTICE.txt) |
| Shizuku API | Android 授权访问；[第三方声明](xiaomi/THIRD-PARTY-NOTICES.txt) |

感谢原作者及相关开源项目提供的实现与文档。
