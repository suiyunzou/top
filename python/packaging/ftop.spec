# Build both a fast directory distribution and a standalone portable executable.
from pathlib import Path
import importlib.metadata as metadata
import sys

root = Path(SPECPATH).parent
assets = root / "ftop_windows" / "assets"
datas = [(str(assets / "ftop.ico"), "ftop_windows/assets")]
for name in ("LICENSE-upstream.txt", "NOTICE.txt"):
    datas.append((str(root / "ftop_windows" / name), "licenses/ftop"))
for dist_name in ("PySide6-Essentials", "shiboken6", "psutil", "json5", "pyinstaller"):
    dist = metadata.distribution(dist_name)
    for file in dist.files or []:
        if ".dist-info/" in str(file).replace("\\", "/") and any(
            token in str(file).lower() for token in ("license", "copying")
        ):
            datas.append((str(dist.locate_file(file)), "licenses/" + dist_name))
datas.append((str(Path(sys.base_prefix) / "LICENSE.txt"), "licenses/Python"))
datas.append((str(root / "packaging" / "licenses"), "licenses/Qt-open-source"))

a = Analysis(
    [str(root / "ftop_win.py")], pathex=[str(root)], binaries=[], datas=datas,
    hiddenimports=[], hookspath=[], hooksconfig={}, runtime_hooks=[],
    excludes=["pytest", "PySide6.QtTest", "tkinter", "unittest"], noarchive=False,
)
pyz = PYZ(a.pure)
options = dict(debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
               console=False, icon=str(assets / "ftop.ico"),
               version=str(root / "packaging" / "version-info.txt"))
directory_exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="ftop", **options)
directory = COLLECT(directory_exe, a.binaries, a.datas, strip=False, upx=False, name="ftop-windows")
portable_exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name="ftop-portable", **options)
