# top · Android 小米版

[返回项目首页](../README.md)

使用 Java、Android AppWidget、RemoteViews 和 Canvas 的桌面监控卡片与详情应用。最低 Android 12，目标 Android 15。源码中应用名称仍为 ftop。

## 安装与使用

1. 按下方步骤构建，安装 `dist/ftop-xiaomi.apk`。
2. 打开应用，在设置中选择“添加监控卡片”或“添加进程卡片”，确认系统提示。
3. 点击卡片“刷新”获取新数据；点击主体进入实时详情。
4. 需要扩展进程与应用流量信息时，启动 Shizuku，再在应用中连接并确认系统授权。

小米桌面添加没有反应时，请检查应用“其他权限”中的“桌面快捷方式”。支持跟随系统、深色与浅色主题。

## 功能与边界

| 功能 | 说明 |
| --- | --- |
| 监控卡片 | 内存、电池、网络与可读取的 CPU 指标 |
| 进程卡片 | 按应用归组，显示可读进程数、CPU 与内存 |
| 应用浏览 | 搜索、排序、应用/系统/归属不明筛选 |
| 进程详情 | PID、UID、内存口径、关联服务与按需读取的近期日志 |
| 应用流量 | Shizuku 授权后读取实时 UID 计数；共享 UID 合并显示 |
| 设备信息 | 存储、电池、可读取的核心频率与 GPU 信息 |
| 应用操作 | 在权限允许时，确认后停止应用或清理后台 |

普通模式可能只能读取自身进程。Shizuku 的 shell 身份不等同 Root，仍受 SELinux 和系统接口限制。无法读取时显示 `—`，不会用最近使用列表替代真实进程。

进程 CPU 按单核 100% 计算。普通模式内存可能使用 PSS；Shizuku 模式使用 RSS。RSS 含共享页，不可直接相加当作整机占用。

应用流量是观测时段的计数差值，不是抓包。共享 UID、VPN 及系统归因会影响合计；不读取通信内容或目标地址。没有实时计数接口时明确显示不可用。

桌面卡片更新受系统省电策略限制，不承诺后台每秒刷新。手动刷新需要差分采样时间；连续查看请进入应用内页面。手机重启后通常需重新启动 Shizuku。

**停止应用会影响整个应用的后台任务和通知，可能丢失未保存内容。** 操作前会确认权限与目标身份；受保护应用、过期目标或归属不明时不执行。Root 设备上的操作路径未完成实机验收。

## 构建

准备 JDK 17、Android SDK Platform 35 与 Build Tools 36.0.0。从仓库根目录执行，并替换工具路径：

```powershell
.\xiaomi\build.ps1 -Sdk 'C:\tools\AndroidSDK' -Jdk 'C:\tools\jdk17'
```

不依赖 Gradle 或 Maven 命令。首次构建从 Maven Central 下载 Shizuku API/provider/shared/aidl 13.1.5 并校验固定 SHA-256。

生成 APK 位于 `xiaomi/dist/`。构建脚本在 `xiaomi/.local/` 创建本地开发签名。密钥不得提交到仓库；请保管密钥，以便覆盖安装后续本地版本。不同签名之间不能直接覆盖安装。

## 测试

测试脚本需要 Python、ADB、Java 和明确指定的测试设备，可能安装测试 APK 或执行测试进程操作。以下示例仅针对已经准备好的模拟器：

```powershell
.\xiaomi\test.ps1 -Serial emulator-5554
.\xiaomi\test-network.ps1 -Serial emulator-5554
```

功能、权限与布局测试位于 `tests/`。测试资料输出到本地 `scratch/`，发布前须检查并脱敏。

## 来源与许可

界面设计参考 [Nongfsq](https://github.com/Nongfsq) 的 [ftop](https://github.com/Nongfsq/ftop)。本版为独立 Android 实现，不是上游官方发行版。

Shizuku API 使用 MIT 许可，见 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)。Shizuku 使用方式见 [官方说明](https://shizuku.rikka.app/zh-hans/guide/setup/)。
