"""ftop Windows entry point. Keeps existing launchers and CLI commands working."""

from pathlib import Path
import importlib.util
import os
import sys


def main():
    if any(
        importlib.util.find_spec(name) is None
        for name in ("PySide6", "psutil", "json5")
    ):
        executable = (
            "pythonw.exe"
            if Path(sys.executable).name.lower() == "pythonw.exe"
            else "python.exe"
        )
        local = Path(__file__).resolve().parent / ".venv-ftop" / "Scripts" / executable
        if local.exists() and local.resolve() != Path(sys.executable).resolve():
            os.execv(
                str(local), [str(local), str(Path(__file__).resolve()), *sys.argv[1:]]
            )
        raise SystemExit(
            "Dependencies missing. Run start-ftop.ps1 or install requirements-ftop.txt."
        )
    from ftop_windows.app import main as run

    return run()


if __name__ == "__main__":
    raise SystemExit(main())
