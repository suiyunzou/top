# top · Windows 原生版

[返回项目首页](../README.md) · [指标口径](TELEMETRY.md)

使用 C++17、Win32、Direct2D 和 DirectWrite 的桌面监控程序。运行不依赖 Python 或 Qt。

## 运行与构建

准备 Windows x64 和 MinGW-w64 x64 工具链，工具链需包含 `g++.exe` 与 `windres.exe`。从仓库根目录运行，替换示例编译器路径：

```powershell
.\native\build.ps1 -Compiler 'C:\tools\mingw64\bin\g++.exe'
.\native\dist\top.exe
```

构建脚本校验随源码附带的官方 PawnIO 安装程序与签名模块，并运行原生逻辑自检。仅编译时可使用 `-SkipSelfTest`；这不代表自检已通过。

**应用要求管理员权限。** 首次运行缺少 PawnIO 驱动时，会运行内置的官方安装程序；已有可用驱动则复用。取消 UAC 后不启动。安装不会关闭安全启动、禁用签名策略或自动重启。硬件读取失败时显示未知。

## 日常使用

| 操作 | 方法 |
| --- | --- |
| 移动与缩放 | 拖动空白区域或窗口边缘 |
| 显示与隐藏 | 点击托盘图标 |
| 修改设置 | 右键托盘图标，打开模块、主题及更多设置 |
| 切换态势总览 | 点击面板右上角切换图标 |
| 查看来源与单位 | 悬停或点击指标 |
| 配置开机启动 | 设置 → 更多 → 开机启动 |

精简面板显示 CPU、内存、GPU、磁盘和网络，布局随窗口尺寸调整。支持逐逻辑处理器柱形图、进程排序、深浅主题、三套配色、中文/英文及动效开关。

态势总览显示应用与资源之间的关系，支持悬停高亮、点击固定、资源切换和最多 60 帧回看。空格暂停或继续，左右方向键回看，Tab 切换控件，Enter 激活，Esc 关闭。动态图表示观测到的资源活动，不是真实数据包轨迹。

```powershell
.\native\dist\top.exe show
.\native\dist\top.exe hide
.\native\dist\top.exe toggle
.\native\dist\top.exe situation
.\native\dist\top.exe quit
```

## 配置与权限

配置保存在 `%APPDATA%\ftop-native\config.json`。首次运行可从旧 Python 版配置迁移；原配置不变。保存时保留未知字段，但会移除注释。无效配置不会被界面覆盖。可用 `--config <路径>` 指定独立配置。

开机启动只在用户开启开关后注册当前用户登录任务，使用最高可用权限。移动 EXE 后，应关闭再开启此开关，以更新任务路径。

## 支持范围

- 原生进程 CPU 按整机 100% 计算；进程内存为工作集，含共享页。
- CPU 温度与封装功耗目前支持 Intel Comet Lake A5/A6、单封装、单处理器组；内存带宽另有更窄的型号限制。
- 普通面板的 GPU 传感器使用 NVIDIA NVML。态势总览的应用 GPU 使用 Windows GPU Engine 计数器。
- 风扇读取使用支持的联想 WMI 接口；其他硬件接口可能不可用。
- 应用磁盘与网络归因使用 ETW。权限不足、归因失败或数据过期时不猜测读数。
- 整机功耗没有统一实测后端。可选估算带 `≈` 标识，并受硬件模型与输入范围限制。
- 完整屏幕阅读器支持、多处理器组及跨设备兼容性仍有待完善。详见 [指标口径](TELEMETRY.md)。

## 测试与打包

测试位于 `tests/`，应按改动范围选择。桌面与硬件测试可能启动管理员窗口；请使用独立配置。Python 只用于部分外部测试工具。

构建后执行：

```powershell
.\native\package.ps1
```

默认在 `native/scratch/package/` 输出 ZIP，包含程序、文档、许可证和对应源码。发布前请检查输出内容。

## 来源与许可证

设计与算法参考 [Nongfsq](https://github.com/Nongfsq) 的 [ftop](https://github.com/Nongfsq/ftop)，本版为独立移植与扩展。上游 MIT 声明保留于 [licenses/](licenses/)。

JSON 库、编译器运行库、PawnIO 驱动与模块按各自许可使用。PawnIO 的来源、版本、哈希及对应源码见 [第三方组件说明](vendor/pawnio/README.md)。
