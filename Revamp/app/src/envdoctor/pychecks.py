# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Python 生态检查（必须在 Python 进程内执行的部分）。

输出与 Rust 侧相同的 JSON 结构（Outcome dict），由 merge 层合并。
逐条对照修复的原版缺陷：导入测速用全新子进程（缓存失真/线程污染），
outdated 的超时与"全部最新"显式区分，Store 别名按真实拦截行为判定，
GIL 自由线程不再标红，EOL 版本给 WARN + 升级建议等。
"""

from __future__ import annotations

import importlib.util
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

PYTHON_CATEGORY = "python"

_COLD_IMPORT_CODE = (
    "import sys, time\n"
    "t = time.perf_counter()\n"
    "try:\n"
    "    __import__(sys.argv[1])\n"
    "    print((time.perf_counter() - t) * 1000)\n"
    "except Exception:\n"
    "    print(-1)\n"
)


def _out(
    id_: str, title: str, status: str, detail: list[str] | None = None,
    hint: str | None = None, error: str | None = None, duration_ms: float = 0.0,
) -> dict:
    return {
        "id": id_,
        "title": title,
        "category": PYTHON_CATEGORY,
        "status": status,
        "detail": detail or [],
        "hint": hint,
        "duration_ms": round(duration_ms, 2),
        "error": error,
    }


def _pip(args: list[str], timeout: int) -> tuple[bool, str]:
    r = subprocess.run(
        [sys.executable, "-m", "pip", *args],
        capture_output=True, timeout=timeout,
    )
    text = (r.stdout or b"").decode("utf-8", "replace") or (r.stderr or b"").decode("utf-8", "replace")
    return r.returncode == 0, text.strip()


# ---------------------------------------------------------------- 检查实现

def check_interpreter(_cfg: dict) -> dict:
    v = sys.version_info
    detail = [
        f"版本: {platform.python_version()}",
        f"实现: {platform.python_implementation()}",
        f"可执行文件: {sys.executable}",
        f"安装前缀: {sys.prefix}",
    ]
    if v.major == 3 and v.minor <= 9:
        return _out("python.interpreter", "解释器", "warn", detail,
                    hint=f"Python {v.major}.{v.minor} 已停止官方支持，建议升级到受支持版本")
    if v.major == 3 and v.minor == 10:
        return _out("python.interpreter", "解释器", "warn", detail,
                    hint="Python 3.10 已进入安全维护尾声，建议规划升级")
    return _out("python.interpreter", "解释器", "ok", detail)


def check_multiplicity(cfg: dict) -> dict:
    found: list[str] = []
    if sys.platform == "win32":
        r = subprocess.run(["where.exe", "python"], capture_output=True, timeout=8)
        found = [l.strip() for l in r.stdout.decode("utf-8", "replace").splitlines() if l.strip()]
        detail = [f"where python: {p}" for p in found]
        # WindowsApps 下的商店存根不是真解释器，不计入多版本数量
        ignored_stubs = 0
        winapps = os.environ.get("LOCALAPPDATA")
        if winapps:
            wa = str(Path(winapps) / "Microsoft" / "WindowsApps").lower()
            stubs = [p for p in found if p.lower().startswith(wa)]
            ignored_stubs = len(stubs)
            found = [p for p in found if not p.lower().startswith(wa)]
        if ignored_stubs:
            detail.append(f"另有 {ignored_stubs} 个 Store 存根未计入")
        py = shutil.which("py")
        if py:
            r2 = subprocess.run(["py", "-0p"], capture_output=True, timeout=8)
            listing = r2.stdout.decode("utf-8", "replace").strip()
            if listing:
                detail.append("py launcher:\n    " + listing.replace("\n", "\n    "))
    else:
        found = [shutil.which(p) or "" for p in ("python3", "python")]
        found = [p for p in found if p]
        detail = [f"解释器: {p}" for p in found]
    if len(found) > 2:
        return _out("python.multiplicity", "Python 多版本共存", "warn", detail,
                    hint="多个 python 共存容易装错环境；建议固定用 py launcher / venv / conda 管理并显式指定解释器")
    return _out("python.multiplicity", "Python 多版本共存", "ok", detail or ["仅检测到一个 python"])


def check_pip(cfg: dict) -> dict:
    timeout = int(cfg.get("timeout_secs", 25))
    ok, text = _pip(["--version"], timeout)
    if not ok:
        return _out("python.pip", "pip", "fail", [text or "pip 不可用"], hint="python -m ensurepip --upgrade")
    m = re.search(r"pip (\d+)\.", text)
    detail = [text.splitlines()[0]]
    if m and int(m.group(1)) < 23:
        return _out("python.pip", "pip", "warn", detail, hint="pip 版本较旧：python -m pip install -U pip")
    return _out("python.pip", "pip", "ok", detail)


def check_venv(_cfg: dict) -> dict:
    in_venv = sys.prefix != getattr(sys, "base_prefix", sys.prefix)
    detail = []
    if in_venv:
        detail.append("当前位于虚拟环境中")
        if (Path(sys.prefix) / "conda-meta").exists():
            detail.append("类型: conda")
        elif (Path(sys.prefix) / "pyvenv.cfg").exists():
            detail.append("类型: venv")
        status_ = "ok"
    else:
        detail.append("当前使用全局解释器")
        hint = "依赖写入全局环境容易互相污染，建议项目内使用 python -m venv .venv"
        status_ = "warn"
        return _out("python.venv", "虚拟环境", status_, detail, hint=hint)
    return _out("python.venv", "虚拟环境", status_, detail)


def check_path(_cfg: dict) -> dict:
    detail = []
    problems = False
    if os.environ.get("PYTHONHOME"):
        detail.append(f"PYTHONHOME 已设置: {os.environ['PYTHONHOME']}")
        hint = "PYTHONHOME 常导致混用两个安装的库文件，除非明确需要否则应删除"
        problems = True
    seen: set[str] = set()
    dupes: list[str] = []
    for p in sys.path:
        key = p.lower()
        if key in seen and p:
            dupes.append(p)
        seen.add(key)
    if dupes:
        detail.append(f"sys.path 存在重复条目: {dupes}")
        problems = True
    pp = os.environ.get("PYTHONPATH")
    if pp:
        detail.append(f"PYTHONPATH = {pp}")
    if problems:
        return _out("python.path", "模块搜索路径", "warn", detail,
                    hint="清理 PYTHONHOME/重复路径，避免导入到错误位置的模块")
    return _out("python.path", "模块搜索路径", "ok", detail or ["sys.path 无重复条目，PYTHONHOME 未设置"])


def check_packages(cfg: dict) -> dict:
    timeout = int(cfg.get("timeout_secs", 25))
    try:
        ok, text = _pip(["list", "--format=freeze"], timeout)
    except subprocess.TimeoutExpired:
        return _out("python.packages", "已安装包", "warn", [f"枚举超时（>{timeout}s）"],
                    hint="包数量过大或磁盘过慢；可单独排查，不影响其余结论")
    if not ok:
        return _out("python.packages", "已安装包", "warn", [text or "枚举失败"])
    pkgs = [l for l in text.splitlines() if l.strip()]
    return _out("python.packages", "已安装包", "ok", [f"包总数: {len(pkgs)}"])


def check_outdated(cfg: dict) -> dict:
    timeout = max(int(cfg.get("timeout_secs", 25)), 20)
    try:
        ok, text = _pip(["list", "--outdated", "--format=json"], timeout)
    except subprocess.TimeoutExpired:
        return _out("python.outdated", "过时包", "warn", [f"检测超时（>{timeout}s，默认源较慢）"],
                    hint="与“全部最新”是两回事；可换国内镜像后重测：pip config set global.index-url ...")
    if not ok:
        return _out("python.outdated", "过时包", "warn", [text[:200] or "检测失败"],
                    hint="检测失败不等于没有过时包，请重测或换源")
    try:
        items = json.loads(text)
    except json.JSONDecodeError:
        return _out("python.outdated", "过时包", "warn", ["输出无法解析"])
    if not items:
        return _out("python.outdated", "过时包", "ok", ["所有包均为最新"])
    names = [it.get("name", "?") for it in items]
    head = ", ".join(names[:8]) + ("…" if len(names) > 8 else "")
    return _out("python.outdated", "过时包", "warn",
                [f"共 {len(names)} 个过时包: {head}"],
                hint="按需升级：python -m pip install -U <包名>")


def check_mirror(cfg: dict) -> dict:
    timeout = max(int(cfg.get("timeout_secs", 25)), 20)
    _, text = _pip(["config", "list"], timeout)
    index = None
    for line in text.splitlines():
        m = re.search(r"index-url=(\S+)", line)
        if m:
            index = m.group(1).strip("'\"")
    base = (index or "https://pypi.org/simple").rstrip("/")
    detail = [f"当前 index-url: {index or '默认 (pypi.org)'}"]
    try:
        t0 = time.perf_counter()
        urllib.request.urlopen(f"{base}/simple/", timeout=8)
        ms = (time.perf_counter() - t0) * 1000
        detail.append(f"GET {base}/simple/ 可达（{ms:.0}ms）")
        return _out("python.mirror", "包镜像源", "ok", detail)
    except Exception as e:
        detail.append(f"{base} 不可达: {type(e).__name__}")
        return _out("python.mirror", "包镜像源", "warn", detail,
                    hint="换用可达的镜像源：pip config set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple")


_IMPORT_LIBS = ["pip", "setuptools", "wheel", "requests", "numpy", "pandas"]


def _import_check(lib: str):
    def fn(_cfg: dict) -> dict:
        if importlib.util.find_spec(lib) is None:
            return _out(f"python.import.{lib}", f"导入 · {lib}", "info", ["未安装"])
        r = subprocess.run(
            [sys.executable, "-c", _COLD_IMPORT_CODE, lib],
            capture_output=True, timeout=10,
        )
        text = (r.stdout or b"").decode("utf-8", "replace").strip()
        ms = float(text) if text and text != "-1" else -1.0
        if ms < 0:
            return _out(f"python.import.{lib}", f"导入 · {lib}", "warn", ["导入失败（全新子进程中）"],
                        hint="库安装可能损坏：pip install -U --force-reinstall " + lib)
        return _out(f"python.import.{lib}", f"导入 · {lib}", "ok",
                    [f"全新子进程冷导入 {ms:.0f}ms（无缓存污染）"])
    return fn


def check_store_alias(_cfg: dict) -> dict:
    if sys.platform != "win32":
        return _out("python.store_alias", "Windows Store 别名", "skip", ["仅 Windows"])
    local_app = os.environ.get("LOCALAPPDATA")
    if not local_app:
        return _out("python.store_alias", "Windows Store 别名", "skip", ["LOCALAPPDATA 未设置"])
    alias = Path(local_app) / "Microsoft" / "WindowsApps" / "python.exe"
    if not alias.exists():
        return _out("python.store_alias", "Windows Store 别名", "ok", ["未发现 Store 别名文件"])
    resolved = shutil.which("python")
    if resolved and Path(resolved).resolve() == alias.resolve():
        return _out("python.store_alias", "Windows Store 别名", "warn",
                    ["Store 别名正在拦截 python 命令（输入 python 会打开商店/商店版 Python）"],
                    hint="设置 → 应用 → 高级应用设置 → 应用执行别名，关闭 python.exe 与 python3.exe")
    return _out("python.store_alias", "Windows Store 别名", "info",
                [f"别名文件存在，但未拦截当前 python（当前: {resolved or '未知'}）"])


def check_gil(_cfg: dict) -> dict:
    try:
        enabled = sys._is_gil_enabled()
    except AttributeError:
        return _out("python.gil", "GIL", "info", ["当前解释器不支持自由线程（GIL 恒启用）"])
    if enabled:
        return _out("python.gil", "GIL", "info", ["GIL 已启用（默认模式）"])
    return _out("python.gil", "GIL", "info", ["自由线程模式（free-threading）"])


def check_env_vars(_cfg: dict) -> dict:
    keys = ["VIRTUAL_ENV", "CONDA_DEFAULT_ENV", "CONDA_PREFIX", "PIP_INDEX_URL",
            "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "PYTHONUTF8", "PYTHONIOENCODING"]
    detail = [f"{k} = {os.environ[k]}" for k in keys if os.environ.get(k)]
    return _out("python.env_vars", "相关环境变量", "info", detail or ["未设置相关环境变量"])


# ---------------------------------------------------------------- 注册与运行

_PY_CHECKS = [
    ("python.interpreter", "解释器", check_interpreter),
    ("python.multiplicity", "Python 多版本共存", check_multiplicity),
    ("python.pip", "pip", check_pip),
    ("python.venv", "虚拟环境", check_venv),
    ("python.path", "模块搜索路径", check_path),
    ("python.packages", "已安装包", check_packages),
    ("python.outdated", "过时包", check_outdated),
    ("python.mirror", "包镜像源", check_mirror),
    ("python.store_alias", "Windows Store 别名", check_store_alias),
    ("python.gil", "GIL", check_gil),
    ("python.env_vars", "相关环境变量", check_env_vars),
]


def py_check_defs() -> list[dict]:
    """供 GUI/CLI 列出全部 Python 检查（含动态导入项）。"""
    defs = [{"id": i, "title": t, "category": PYTHON_CATEGORY} for i, t, _ in _PY_CHECKS]
    for lib in _IMPORT_LIBS:
        if importlib.util.find_spec(lib) is not None:
            defs.append({"id": f"python.import.{lib}", "title": f"导入 · {lib}",
                         "category": PYTHON_CATEGORY})
    return defs


def run_python_checks(
    cfg: dict,
    progress=None,
    done_offset: int = 0,
    total: int = 0,
    categories: list[str] | None = None,
) -> list[dict]:
    """顺序执行 Python 侧检查（Rust 侧已并发跑完系统类检查）。

    categories 不含 python 时直接返回空列表，避免白跑后被丢弃。
    """
    if categories is not None and PYTHON_CATEGORY not in categories:
        return []
    results: list[dict] = []
    jobs = [(i, t, f) for i, t, f in _PY_CHECKS]
    for lib in _IMPORT_LIBS:
        if importlib.util.find_spec(lib) is not None:
            jobs.append((f"python.import.{lib}", f"导入 · {lib}", _import_check(lib)))
    for n, (id_, title, fn) in enumerate(jobs, 1):
        t0 = time.perf_counter()
        try:
            r = fn(cfg)
        except subprocess.TimeoutExpired:
            budget = int(cfg.get("timeout_secs", 25))
            r = _out(id_, title, "timeout", [f"检测超时（>{budget}s）"])
        except Exception as e:  # noqa: BLE001 —— 检查函数不允许拖垮整轮诊断
            r = _out(id_, title, "fail", [f"{type(e).__name__}: {e}"], error=str(e))
        r["duration_ms"] = round((time.perf_counter() - t0) * 1000, 2)
        results.append(r)
        if progress is not None:
            progress(done_offset + n, total, id_)
    return results
