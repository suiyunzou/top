"""Collect reproducible source, user instructions and checksums beside the builds."""

from pathlib import Path
import hashlib
import shutil
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
dist = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "dist-ftop"
folder = dist / "ftop-windows"
assert (folder / "ftop.exe").is_file() and (dist / "ftop-portable.exe").is_file()
readme = """ftop Windows 0.3.0-win.1 — Windows x64

文件夹版：解压整个 ZIP，双击 ftop-windows\\ftop.exe。
请保留旁边的 _internal 文件夹。此版本启动更快。
单文件版：双击 ftop-portable.exe。启动时会解压运行库到临时目录。
两个版本都免安装，不需要另装 Python，也不需要管理员权限。

设置保存在 %APPDATA%\\ftop\\config.json5，沿用 Python 版的设置。
已有程序运行时，再次启动只会显示已有窗口。
若要切换到 EXE 运行，请先在原程序设置中点“退出”，再打开 EXE。
右键面板或托盘数字打开设置；点托盘数字显示／隐藏面板。
开机启动会使用当前 EXE 路径，请先将程序放到固定位置再开启。

ftop-source.zip 包含本移植版源码、依赖版本和构建脚本。
解压源码后用 Python 3.12 x64 执行 build-ftop.ps1 可重新生成两个版本。
第三方许可保存在文件夹版 _internal\\licenses 中。
原项目：https://github.com/Nongfsq/ftop
Qt 打包参考：https://doc.qt.io/qtforpython-6/deployment/deployment-pyinstaller.html

验证环境：Windows 10 19045 x64，i7-10750H，GTX 1650。
未在另一台无 Python 的干净机器上测试；已用最小 PATH 验证包内运行库加载。
"""
(dist / "使用说明.txt").write_text(readme, encoding="utf-8-sig")
source_zip = dist / "ftop-source.zip"
with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED) as archive:
    for name in (
        "ftop_win.py",
        "requirements-ftop.txt",
        "requirements-ftop-build.txt",
        "build-ftop.ps1",
        "start-ftop.ps1",
        "README-ftop-windows.md",
        "FTOP-PARITY.md",
    ):
        archive.write(root / name, name)
    for subdir in ("ftop_windows", "packaging", "tests_ftop"):
        for file in (root / subdir).rglob("*"):
            if file.is_file() and "__pycache__" not in file.parts:
                archive.write(file, file.relative_to(root))
shutil.copy2(source_zip, folder / source_zip.name)
shutil.copy2(dist / "使用说明.txt", folder / "使用说明.txt")
shutil.make_archive(str(dist / "ftop-windows-x64"), "zip", dist, "ftop-windows")
lines = []
for name in ("ftop-portable.exe", "ftop-windows-x64.zip", "ftop-source.zip"):
    file = dist / name
    digest = hashlib.sha256(file.read_bytes()).hexdigest()
    lines.append(f"{digest}  {name}")
    print(f"{name}: {file.stat().st_size / 1048576:.1f} MiB")
(dist / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
