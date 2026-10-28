# top · Windows Python/Qt 版

[返回项目首页](../README.md) · [Windows 原生版](../native/README.md)

基于 Python、PySide6 和 Windows 系统接口的悬浮监控面板。保留当前状态、逐逻辑处理器柱形图、应用归组、稳定排序与进程详情。

## 启动

准备 Windows 10/11、64 位 Python 3.12，并确保 `python` 在 PATH 中。从仓库根目录执行：

```powershell
.\python\start-ftop.ps1
```

首次启动会创建 `python/.venv-ftop/` 并安装固定版本的依赖，需要联网。运行时不需要编译 C++。

也可以使用命令行：

```powershell
python .\python\ftop_win.py show
python .\python\ftop_win.py toggle
python .\python\ftop_win.py quit
python .\python\ftop_win.py doctor
python .\python\ftop_win.py version
```

## 使用

- 拖动空白处移动，拖动边缘缩放；布局随窗口尺寸调整。
- 点击托盘数字显示或隐藏窗口，右键面板打开设置。
- 点击进程打开详情卡；鼠标离开进程行与卡片后自动收起。
- 切换 CPU/内存排序，悬停指标查看来源及限制。
- `Esc` 先关闭详情卡，再隐藏面板。

支持深浅主题、三套配色、中文/英文、刷新间隔、模块开关、平滑动画、托盘及开机启动。

## 配置

配置位于 `%APPDATA%\ftop\config.json5`。支持外部修改后热加载。界面保存保留未知字段，但会格式化并移除注释。文件格式错误时继续使用最后的有效配置，并暂停覆盖。

使用 `--config <路径>` 可指定独立配置。开机启动只在用户点击开关后修改当前用户注册表项。

## 数据范围

| 指标 | 口径或限制 |
| --- | --- |
| 总 CPU | 整机 0–100% |
| 进程 CPU | 单核 100%，多线程进程可能超过 100% |
| 内存 | 总量减可用量；进程工作集含共享页 |
| 内存压力 | Windows 内存资源通知 |
| CPU 频率 | Windows PDH 估计；不可读时显示 `—` |
| GPU | 首块 NVIDIA GPU 的 NVML 指标；Intel/AMD 后端未实现 |
| CPU 温度、整机功耗、压缩内存 | 当前后端不可用，不以其他指标替代 |

此版本没有原生版的态势总览与磁盘面板。多显示器 DPI、长期运行及不同硬件的兼容性不能由单一环境的测试保证。

## 打包与测试

构建依赖见 `requirements-ftop-build.txt`，打包入口为 `build-ftop.ps1`，测试位于 `tests_ftop/`。构建后的文件夹版运行时需保留旁边的 `_internal/`；单文件版启动时会解压运行库。

本仓库暂未发布应用安装包，不提供自动更新。

## 来源与许可

设计、布局与算法改编自 [Nongfsq/ftop](https://github.com/Nongfsq/ftop)，原作者主页为 [Nongfsq](https://github.com/Nongfsq)。本版为独立移植，不是官方 Windows 版本。

上游 MIT、Qt/PySide6 许可与对应源码说明见 [NOTICE](ftop_windows/NOTICE.txt) 和 [上游许可证](ftop_windows/LICENSE-upstream.txt)。
