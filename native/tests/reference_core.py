"""Platform-independent configuration, ranking, formatting, and layout geometry.

Layout choice and steady ranking follow FtopCore at aa40ed6 (MIT).
Geometry uses Windows' Segoe UI metrics, in device-independent pixels.
"""

from dataclasses import dataclass, field
from pathlib import Path
import copy
import json
import math
import os
import json5

DEFAULTS = dict(
    modules=["cpu", "memory", "network", "processes"],
    hidden=[],
    palette="sea",
    language="auto",
    interval=1.0,
    motion=True,
    maxProcesses=24,
    processSort="cpu",
    floating=False,
    menuBar=True,
    appearance="auto",
    opacity=0.93,
    width=300,
    height=420,
    updates="off",
)


def finite(value, default, lo, hi):
    try:
        value = float(value)
        return max(lo, min(hi, value)) if math.isfinite(value) else default
    except (TypeError, ValueError):
        return default


def normalize(raw):
    c = copy.deepcopy(DEFAULTS)
    if isinstance(raw, dict):
        c.update(raw)
    if "floating" not in (raw or {}) and isinstance(raw, dict) and "is_topmost" in raw:
        c["floating"] = raw["is_topmost"] is True
    for key in ("motion", "floating", "menuBar"):
        if not isinstance(c[key], bool):
            c[key] = DEFAULTS[key]
    for key, choices in dict(
        palette=("sea", "graphite", "warm"),
        language=("auto", "zh", "en"),
        processSort=("cpu", "memory"),
        appearance=("auto", "dark", "light"),
    ).items():
        if c[key] not in choices:
            c[key] = DEFAULTS[key]
    for key, lo, hi in [
        ("interval", 0.5, 10),
        ("maxProcesses", 3, 24),
        ("opacity", 0.6, 1),
        ("width", 40, 4000),
        ("height", 16, 2400),
    ]:
        c[key] = finite(c[key], DEFAULTS[key], lo, hi)
    c["maxProcesses"] = int(c["maxProcesses"])
    for key, allowed in [
        ("modules", ("cpu", "memory", "network", "processes")),
        ("hidden", ("gpu", "power")),
    ]:
        value = c[key]
        if not isinstance(value, list):
            value = DEFAULTS[key]
        c[key] = list(
            dict.fromkeys(x for x in value if isinstance(x, str) and x in allowed)
        )
    if "cpu" not in c["modules"]:
        c["modules"].insert(0, "cpu")
    for axis in ("x", "y"):
        if axis in c:
            c[axis] = int(finite(c[axis], 40, -32000, 32000))
    return c


def shown(c):
    return c["modules"] + [m for m in ("gpu", "power") if m not in c["hidden"]]


class ConfigStore:
    def __init__(self, path=None):
        self.path = Path(
            path
            or os.environ.get("FTOP_CONFIG", "")
            or Path(os.environ.get("APPDATA", str(Path.home())))
            / "ftop"
            / "config.json5"
        )
        self.data = normalize({})
        self.stamp = None
        self.error = None
        if not self.path.exists():
            legacy = self.path.with_name("config.json")
            if legacy.exists():
                try:
                    self.data = normalize(
                        json.loads(legacy.read_text(encoding="utf-8-sig"))
                    )
                    # The old Tk panel had a fixed dark appearance.
                    self.data["appearance"] = "dark"
                    self.data["positionUnits"] = "physical"
                except (ValueError, OSError):
                    pass
            self.save()
        self.reload()

    def reload(self):
        try:
            stamp = (self.path.stat().st_mtime_ns, self.path.stat().st_size)
            if stamp == self.stamp:
                return False
            raw = json5.loads(self.path.read_text(encoding="utf-8-sig"))
            if not isinstance(raw, dict):
                raise ValueError(
                    "配置根节点必须是对象 / configuration must be an object"
                )
            new = normalize(raw)
            changed = new != self.data
            self.data, self.stamp, self.error = new, stamp, None
            return changed
        except (ValueError, OSError) as exc:
            self.error = str(exc)
            return False  # A partial editor save never resets the live settings.

    def save(self, changes=None):
        # Merge an editor's latest valid changes before writing UI changes.
        self.reload()
        if self.error and self.path.exists():
            return False  # Preserve malformed user content for repair.
        self.data = normalize({**self.data, **(changes or {})})
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(f".tmp-{os.getpid()}")
        tmp.write_text(
            "// ftop Windows. 保存后即时生效。CPU 进程占用以单核 100% 计。\n"
            + json.dumps(self.data, ensure_ascii=False, indent=2)
            + "\n",
            encoding="utf-8",
        )
        os.replace(tmp, self.path)
        self.stamp = (self.path.stat().st_mtime_ns, self.path.stat().st_size)
        self.error = None
        return True


class SteadyRanking:
    def __init__(self, relative=0.12, margin=1.0, fall=4.0):
        self.relative, self.margin, self.fall = relative, margin, fall
        self.levels, self.order = {}, []

    def step(self, entries, elapsed, hold=False):
        decay = math.exp(-max(0, elapsed) / self.fall) if self.fall > 0 else 0
        values = {}
        for key, value in entries:
            values.setdefault(key, value)
        self.levels = {
            k: max(v, v + (self.levels.get(k, v) - v) * decay)
            for k, v in values.items()
        }
        result = [k for k in self.order if k in values]
        known = set(result)
        result += sorted(
            (k for k in values if k not in known), key=lambda k: -values[k]
        )
        if not hold or not known:
            for i in range(1, len(result)):
                level = self.levels[result[i]]
                target = next(
                    (
                        j
                        for j in range(i)
                        if level
                        > self.levels[result[j]] * (1 + self.relative) + self.margin
                    ),
                    None,
                )
                if target is not None:
                    result.insert(target, result.pop(i))
        self.order = result
        return result[:]


def rate(value):
    if value is None:
        return "—", ""
    value = max(0.0, value)
    if value >= 999_500_000:
        return f"{value / 1e9:.1f}", "GB/s"
    if value >= 999_500:
        return f"{value / 1e6:.1f}", "MB/s"
    return str(int(value / 1000 + 0.5)), "KB/s"


def memory(value):
    if value is None:
        return "—", ""
    mb = value / 1048576
    return (f"{mb / 1024:.1f}", "G") if mb >= 1000 else (str(int(mb + 0.5)), "M")


def number(v, places=0):
    return "—" if v is None else f"{v:.{places}f}"


def group_processes(records):
    """Fold only identical executable paths and recognisable descendants.

    Never merge two unrelated same-name executables. A helper belongs to an app
    only if its ancestor and executable directory match (not arbitrary children).
    """
    by_pid = {r["pid"]: r for r in records}
    groups = {}
    for r in records:
        owner, cursor, visited = r, r, set()
        path = (r.get("exe") or "").casefold()
        directory = os.path.dirname(path)
        while cursor.get("ppid") in by_pid and cursor["ppid"] not in visited:
            visited.add(cursor["ppid"])
            parent = by_pid[cursor["ppid"]]
            parent_path = (parent.get("exe") or "").casefold()
            # Chromium helpers, WebView, and app-local utility executables.
            helper = any(
                token in os.path.basename(path)
                for token in ("helper", "webview", "qtwebengine", "crashpad")
            )
            same_app = (
                parent_path == path
                or helper
                and directory
                and (
                    os.path.dirname(parent_path) == directory
                    or directory.startswith(os.path.dirname(parent_path) + os.sep)
                )
            )
            if directory and parent_path and same_app:
                owner = parent
            else:
                break
            cursor = parent
        key = (
            owner.get("exe") or ""
        ).casefold() or f"pid:{owner['pid']}:{owner.get('created', 0)}"
        g = groups.setdefault(
            key,
            dict(
                key=key,
                name=owner.get("app_name") or owner["name"],
                exe=owner.get("exe"),
                cpu=None,
                mem=None,
                members=[],
                count=0,
            ),
        )
        for metric in ("cpu", "mem"):
            if r.get(metric) is not None:
                g[metric] = (g[metric] or 0) + r[metric]
        g["members"].append(r)
        g["count"] += 1
    return groups


@dataclass(frozen=True)
class Candidate:
    tier: str
    columns: int = 1
    count: int = 0
    strip: str = ""
    wide: bool = False


@dataclass
class Block:
    kind: str
    x: float
    y: float
    w: float
    h: float


@dataclass
class Layout:
    candidate: Candidate
    width: float
    height: float
    blocks: list = field(default_factory=list)
    scale: float = 1.0


class LayoutEngine:
    """Ports the upstream candidate ladder, 90% coverage tie-break and stretch limits."""

    def __init__(self, core_count, config):
        self.n = max(1, core_count)
        self.config = config
        self.modules = shown(config)

    def candidates(self):
        counts = [
            n for n in (24, 16, 12, 9, 7, 5, 3) if n <= self.config["maxProcesses"]
        ]
        if "processes" not in self.modules:
            counts = []

        def add(tier, count):
            out = []
            if count >= 7 and tier != "compact":
                out.append(Candidate(tier, 2, count, wide=True))
            for cols in (1, 2, 3):
                if cols < 3 or count == (5 if 5 in counts else 3) and count:
                    out.append(Candidate(tier, cols, count))
            return out

        out = []
        for tier in ("detailed", "full"):
            for count in counts:
                out += add(tier, count)
        if counts:
            out += add("compact", 3)
        for tier in ("detailed", "full", "compact"):
            out += add(tier, 0)
        return (
            out
            + [Candidate("corner")]
            + [Candidate("strip", strip=s) for s in ("rich", "plain", "vertical")]
            + [Candidate("micro")]
        )

    def measure(self, c, ex=0.0, ey=0.0):
        mods = self.modules
        if c.tier == "micro":
            count = 2 + ("gpu" in mods)
            return Layout(
                c, count * 26 + 8, 30, [Block("micro", 7, 5, count * 26 - 6, 20)]
            )
        if c.tier == "strip":
            if c.strip == "vertical":
                width = max(78.0, min(120.0, self.n * 4.5))
                kinds = ["cpu", "temperature"] + (["gpu"] if "gpu" in mods else [])
                kinds += (["memory"] if "memory" in mods else []) + (
                    ["down", "up"] if "network" in mods else []
                )
                blocks = [Block("mini", 8, 10, width, 48 + ey)]
                y = 66 + ey
                for k in kinds:
                    blocks.append(Block("fig:" + k, 8, y, width, 20))
                    y += 28
                return Layout(c, width + 16, y + 2, blocks)
            kinds = ["cpu"]
            if c.strip == "rich":
                kinds += ["temperature"] + (["gpu"] if "gpu" in mods else [])
            kinds += ["memory"] if "memory" in mods else []
            kinds += ["down", "up"] if "network" in mods else []
            if c.strip == "rich" and "processes" in mods:
                kinds.append("top")
            mini = min(110, self.n * 4.5)
            blocks = [Block("mini", 10, 8, mini, 14)]
            x = 22 + mini
            for kind in kinds:
                width = 160 if kind == "top" else 78
                blocks.append(Block("fig:" + kind, x, 5, width, 20))
                x += width + 10
            return Layout(c, x, 30, blocks)
        if c.tier == "corner":
            w = max(248, self.n * 9) + ex
            h = 22 + 20 + 8 + 34 + ey
            blocks = [Block("cornercpu", 13, 11, w, 62 + ey)]
            if "memory" in mods:
                blocks.append(Block("meter", 13, h, w, 5))
                h += 13
            if "network" in mods or "power" in mods:
                blocks.append(Block("cornerbottom", 13, h, w, 20))
                h += 28
            return Layout(c, w + 26, h + 3, blocks)
        visible = [
            m
            for m in mods
            if m in ("cpu", "memory", "network", "processes")
            and (m != "processes" or c.count)
        ]
        pairs = c.tier != "compact" and any(m in mods for m in ("gpu", "power"))
        if pairs:
            pos = visible.index("processes") if "processes" in visible else len(visible)
            visible.insert(pos, "pairs")
        if c.wide and "processes" in visible:
            groups = [[m for m in visible if m != "processes"], ["processes"]]
        elif c.columns == 2 and len(visible) >= 2:
            groups = [visible[:1], visible[1:]]
        elif c.columns == 3 and len(visible) >= 3:
            groups = [visible[:1], visible[1:-1], visible[-1:]]
        else:
            groups = [visible]
        roomy = len(groups) > 1

        def size(m):
            if m == "cpu":
                return (
                    max(
                        self.n * (29 if c.tier == "detailed" else 9) - 4,
                        246 if c.tier == "compact" and "gpu" in mods else 170,
                    ),
                    28
                    + (84 if c.tier == "detailed" or c.count > 3 else 44)
                    + (30 if c.tier == "detailed" else 0),
                )
            if m == "memory":
                return 238, 56 if c.tier != "compact" else 34
            if m == "network":
                return 238, 20
            if m == "pairs":
                return 238, 28 * sum(m in mods for m in ("gpu", "power")) - 8
            width = 218 if roomy else 194
            return (width * 2 + 22 if c.wide else width), (
                c.count * 28 - 8 + (28 if c.wide else 0)
            )

        def gap(group, i):
            return 8 if group[i] == "network" and group[i + 1] == "pairs" else 15

        widths, heights, sizes = [], [], []
        for group in groups:
            sizes.append([size(m)[1] for m in group])
            if len(groups) == 1 and "cpu" in group:
                sizes[-1][group.index("cpu")] += ey
            widths.append(
                max(size(m)[0] for m in group)
                + ex
                / (len(groups) + c.wide)
                * (2 if c.wide and group == ["processes"] else 1)
            )
            heights.append(
                sum(sizes[-1]) + sum(gap(group, i) for i in range(len(group) - 1))
            )
        tallest = max(heights)
        blocks, x = [], 15
        for gi, group in enumerate(groups):
            if "cpu" in group:
                sizes[gi][group.index("cpu")] += tallest - heights[gi]
            y = 13
            for i, m in enumerate(group):
                bh = sizes[gi][i]
                if len(groups) == 3 and gi == 1 and i == len(group) - 1:
                    y = max(y, 13 + tallest - bh)
                blocks.append(Block(m, x, y, widths[gi], bh))
                y += bh + (gap(group, i) if i < len(group) - 1 else 0)
            x += widths[gi] + 22
        return Layout(c, x - 7, tallest + 26, blocks)

    def stretch(self, c):
        if c.tier == "micro":
            return 0, 0
        if c.tier == "strip":
            return 0, 150 if c.strip == "vertical" else 0
        if c.tier == "corner":
            return 90, 50
        if c.wide:
            return 210, 0
        if c.columns == 1:
            return 110, 150
        return c.columns * 70, 0

    def choose(self, width, height):
        fitting = []
        for c in self.candidates():
            natural = self.measure(c)
            fit = min(width / natural.width, height / natural.height)
            floor = 0.45 if c.tier == "micro" else 0.76 if c.tier == "strip" else 1
            if fit + 1e-8 < floor:
                continue
            scale = min(1.3, fit)
            sx, sy = self.stretch(c)
            layout = self.measure(
                c,
                min(sx, max(0, width / scale - natural.width)),
                min(sy, max(0, height / scale - natural.height)),
            )
            layout.scale = scale
            fitting.append((layout, layout.width * layout.height * scale * scale))
        if not fitting:
            layout = self.measure(Candidate("micro"))
            layout.scale = 0.45
            return layout
        best = max(area for _, area in fitting)
        return next(layout for layout, area in fitting if area >= best * 0.9)


class Follow:
    """Additive, staggered third-order follow curve from FtopUI/CoreBars.swift."""

    def __init__(self):
        self.targets, self.moves = {}, {}

    def set(self, key, value, now, interval=1.0, delay=0.0, animate=True):
        old = self.targets.get(key, value)
        self.targets[key] = value
        if value is None or old is None or not animate:
            self.moves.pop(key, None)
        elif abs(old - value) > 0.001:
            self.moves.setdefault(key, []).append(
                (old - value, now + delay, min(1.5, max(0.25, interval)))
            )

    def value(self, key, now):
        target = self.targets.get(key)
        if target is None:
            return None
        active, extra = [], 0.0
        for delta, start, interval in self.moves.get(key, []):
            x = max(0.0, now - start) * 4.5 / interval
            if x < 12:
                active.append((delta, start, interval))
                extra += delta * (1 + x + x * x / 2) * math.exp(-x)
        self.moves[key] = active
        return target + extra

    def active(self):
        return any(self.moves.values())
