import sys
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


@pytest.fixture(autouse=True)
def isolated_logs(tmp_path, monkeypatch):
    from ftop_windows import native

    monkeypatch.setattr(native, "CONFIG_DIR", str(tmp_path))
    monkeypatch.setattr(native, "LOG_PATH", str(tmp_path / "test.log"))
