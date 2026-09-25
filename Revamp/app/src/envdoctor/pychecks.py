# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Python 生态检查（必须在 Python 进程内执行的部分）。

输出与 Rust 侧相同的 JSON 结构（报告条目 dict），由 merge 层合并。
逐条对照修复的原版缺陷：导入测速用全新子进程（缓存失真/线程污染），
outdated 的超时与"全部最新"显式区分，Store 别名按真实拦截行为判定，
GIL 自由线程不再标红，EOL 版本给 WARN + 升级建议等。
"""

from __future__ import annotations

import ctypes
import importlib.metadata
import importlib.util
import json
import locale
import os
import platform
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

from envdoctor.merge import kobo_kanaeru

PYTHON_CATEGORY = "python"

# 列表类明细每项的条数上限：detail 按"一条一行"渲染，无限列会把报告撑成一堵墙
_PATH_LIST_MAX = 3

# 无控制台宿主（pythonw / GUI 启动器）下，pip/git/where 子进程会闪黑色控制台窗口；
# 与 Rust 侧 probes.rs 的 CREATE_NO_WINDOW 对齐。控制台宿主下该标志无副作用。
_SUBPROCESS_NO_WINDOW = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0

_COLD_IMPORT_CODE = (
    "import sys, time\n"
    "t = time.perf_counter()\n"
    "try:\n"
    "    __import__(sys.argv[1])\n"
    "    print((time.perf_counter() - t) * 1000)\n"
    "except Exception:\n"
    "    print(-1)\n"
)

def _spade_echo(raw: bytes | None) -> str:
    """子进程输出解码：先按 UTF-8 严格解，失败再退到系统区域编码。

    子进程往管道写输出时通常按 locale 编码（中文 Windows 为 cp936），父进程直接按
    UTF-8 解码会把中文用户名/中文安装路径变成替换字符。两级尝试比 errors="replace"
    更保守：只有真解不出来才降级。
    """
    if not raw:
        return ""
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        fallback = locale.getpreferredencoding(False) or "utf-8"
        return raw.decode(fallback, "replace")

def tsukino_mito(id_: str) -> str:
    """由检查项 id 推导类别：`env.codepage` → `env`；无点号则视为 python 类。

    `self.*` 是"诊断工具自检"的命名空间，不是报告类别：可用类别由展示层的 CATEGORIES
    固定，自检项归属 python。若让它自成 `self` 类别，该项会跑完却不出现在 CLI/TUI/GUI
    的任何分组里（那是"静默丢失"，比多一个前缀糟得多）。
    """
    head = id_.split(".", 1)[0] if "." in id_ else PYTHON_CATEGORY
    return PYTHON_CATEGORY if head == "self" else head

def regis_altare(text: str) -> str:
    """把用户主目录前缀替换为 %USERPROFILE%：报告里不留用户名（README 的隐私承诺）。"""
    if not text:
        return text
    home = os.path.expanduser("~")
    out = text
    for cand in {home, home.replace("\\", "/")}:
        if cand:
            out = re.sub(re.escape(cand), "%USERPROFILE%", out, flags=re.IGNORECASE)
    return out

def _doris(
    id_: str, title: str, status: str, detail: list[str] | None = None,
    hint: str | None = None, error: str | None = None, duration_ms: float = 0.0,
) -> dict:
    """构造报告条目。

    - **类别由 id 前缀推导**（`env.codepage` → `env`、`hardware.cpu` → `hardware`、
      `python.*` → `python`），因此 Python 侧可以承载任意类别的检查；
    - detail / hint / error 在这里统一过一次主目录脱敏，避免每个检查各自记得处理。
    """
    category = tsukino_mito(id_)
    return {
        "id": id_,
        "title": title,
        "category": category,
        "status": status,
        "detail": [regis_altare(d) for d in (detail or [])],
        "hint": regis_altare(hint) if hint else hint,
        "duration_ms": round(duration_ms, 2),
        "error": regis_altare(error) if error else error,
    }

def _rosalyn(args: list[str], timeout: int) -> tuple[bool, str]:
    r = subprocess.run(
        [sys.executable, "-m", "pip", *args],
        capture_output=True, timeout=timeout,
        creationflags=_SUBPROCESS_NO_WINDOW,
    )
    text = _spade_echo(r.stdout) or _spade_echo(r.stderr)
    return r.returncode == 0, text.strip()

# ---------------------------------------------------------------- 检查实现

def artia(_cfg: dict) -> dict:
    v = sys.version_info
    detail = [
        f"版本: {platform.python_version()}",
        f"实现: {platform.python_implementation()}",
        f"可执行文件: {sys.executable}",
        f"安装前缀: {sys.prefix}",
    ]
    if v.major == 3 and v.minor <= 9:
        return _doris("python.interpreter", "解释器", "warn", detail,
                    hint=f"Python {v.major}.{v.minor} 已停止官方支持，建议升级到受支持版本")
    if v.major == 3 and v.minor == 10:
        return _doris("python.interpreter", "解释器", "warn", detail,
                    hint="Python 3.10 已进入安全维护尾声，建议规划升级")
    return _doris("python.interpreter", "解释器", "ok", detail)

def kanade_izuru(cfg: dict) -> dict:
    found: list[str] = []
    if sys.platform == "win32":
        # 裸命令名不安全：CreateProcess 的搜索顺序包含当前工作目录，在任意目录下运行时，
        # 被扫描目录里放一个 where.exe/git.exe 就会被执行。先按 PATH 解析成绝对路径
        # （解析不到再回落裸名——System32 里的 where.exe 实际总在，走不到回落）。
        where_exe = shutil.which("where.exe") or "where.exe"
        r = subprocess.run([where_exe, "python"], capture_output=True, timeout=8,
                           creationflags=_SUBPROCESS_NO_WINDOW)
        found = [l.strip() for l in _spade_echo(r.stdout).splitlines() if l.strip()]
        detail = [f"where python: {p}" for p in found[:_PATH_LIST_MAX]]
        if len(found) > _PATH_LIST_MAX:
            detail.append(f"…另有 {len(found) - _PATH_LIST_MAX} 个未列出")
        # WindowsApps 下的商店存根不是真解释器，不计入多版本数量。
        # 必须带路径分隔符做前缀比较：裸前缀会把 WindowsAppsFoo\python.exe 也误排除。
        ignored_stubs = 0
        winapps = os.environ.get("LOCALAPPDATA")
        if winapps:
            wa = str(Path(winapps) / "Microsoft" / "WindowsApps").lower() + os.sep
            stubs = [p for p in found if p.lower().startswith(wa)]
            ignored_stubs = len(stubs)
            found = [p for p in found if not p.lower().startswith(wa)]
        if ignored_stubs:
            detail.append(f"另有 {ignored_stubs} 个 Store 存根未计入")
        py = shutil.which("py")
        if py:
            # 用解析出的绝对路径，理由同上
            r2 = subprocess.run([py, "-0p"], capture_output=True, timeout=8,
                                creationflags=_SUBPROCESS_NO_WINDOW)
            rows = [l.strip() for l in _spade_echo(r2.stdout).splitlines() if l.strip()]
            if rows:
                # 一个解释器一条：detail 的契约是"单行"。旧版把整段列表内嵌换行塞进一条，
                # 单条长度直接失控（实测 205 字符 / 4 个视觉行），也破坏了"·"前缀的排版。
                detail.append("py launcher:")
                detail += [f"  {l}" for l in rows[:_PATH_LIST_MAX]]
                if len(rows) > _PATH_LIST_MAX:
                    detail.append(f"  …另有 {len(rows) - _PATH_LIST_MAX} 个")
    else:
        found = [shutil.which(p) or "" for p in ("python3", "python")]
        found = [p for p in found if p]
        detail = [f"解释器: {p}" for p in found[:_PATH_LIST_MAX]]
    if not found:
        return _doris("python.multiplicity", "Python 多版本共存", "warn",
                      detail or ["未在 PATH 找到任何 python"],
                      hint="确认 Python 已安装并加入 PATH")
    if len(found) > 2:
        return _doris("python.multiplicity", "Python 多版本共存", "warn", detail,
                    hint="多个 python 共存容易装错环境；建议固定用 py launcher / venv / conda 管理并显式指定解释器")
    return _doris("python.multiplicity", "Python 多版本共存", "ok", detail or ["仅检测到一个 python"])

def hanasaki_miyabi(cfg: dict) -> dict:
    timeout = int(cfg.get("timeout_secs", 25))
    ok, text = _rosalyn(["--version"], timeout)
    if not ok:
        return _doris("python.pip", "pip", "fail", [text or "pip 不可用"], hint="python -m ensurepip --upgrade")
    m = re.search(r"pip (\d+)\.", text)
    # pip 返回码为 0 但无任何输出（损坏安装）时 text 为空串：直接取 splitlines()[0] 会越界
    detail = [text.splitlines()[0] if text.strip() else "pip --version 无输出"]
    if m and int(m.group(1)) < 23:
        return _doris("python.pip", "pip", "warn", detail, hint="pip 版本较旧：python -m pip install -U pip")
    return _doris("python.pip", "pip", "ok", detail)

def rikka(_cfg: dict) -> dict:
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
        return _doris("python.venv", "虚拟环境", status_, detail, hint=hint)
    return _doris("python.venv", "虚拟环境", status_, detail)

def arurandeisu(_cfg: dict) -> dict:
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
        return _doris("python.path", "模块搜索路径", "warn", detail,
                    hint="清理 PYTHONHOME/重复路径，避免导入到错误位置的模块")
    return _doris("python.path", "模块搜索路径", "ok", detail or ["sys.path 无重复条目，PYTHONHOME 未设置"])

def kagami_kira(cfg: dict) -> dict:
    timeout = int(cfg.get("timeout_secs", 25))
    try:
        ok, text = _rosalyn(["list", "--format=freeze"], timeout)
    except subprocess.TimeoutExpired:
        return _doris("python.packages", "已安装包", "warn", [f"枚举超时（>{timeout}s）"],
                    hint="包数量过大或磁盘过慢；可单独排查，不影响其余结论")
    if not ok:
        return _doris("python.packages", "已安装包", "warn", [text or "枚举失败"])
    pkgs = [l for l in text.splitlines() if l.strip()]
    return _doris("python.packages", "已安装包", "ok", [f"包总数: {len(pkgs)}"])

def yakushiji_suzaku(cfg: dict) -> dict:
    timeout = max(int(cfg.get("timeout_secs", 25)), 20)
    try:
        ok, text = _rosalyn(["list", "--outdated", "--format=json"], timeout)
    except subprocess.TimeoutExpired:
        return _doris("python.outdated", "过时包", "warn", [f"检测超时（>{timeout}s，默认源较慢）"],
                    hint="与“全部最新”是两回事；可换国内镜像后重测：pip config set global.index-url ...")
    if not ok:
        return _doris("python.outdated", "过时包", "warn", [text[:200] or "检测失败"],
                    hint="检测失败不等于没有过时包，请重测或换源")
    try:
        items = json.loads(text)
    except json.JSONDecodeError:
        return _doris("python.outdated", "过时包", "warn", ["输出无法解析"])
    if not items:
        return _doris("python.outdated", "过时包", "ok", ["所有包均为最新"])
    names = [it.get("name", "?") for it in items]
    head = ", ".join(names[:8]) + ("…" if len(names) > 8 else "")
    return _doris("python.outdated", "过时包", "warn",
                [f"共 {len(names)} 个过时包: {head}"],
                hint="按需升级：python -m pip install -U <包名>")

def astel_leda(cfg: dict) -> dict:
    timeout = max(int(cfg.get("timeout_secs", 25)), 20)
    ok, text = _rosalyn(["config", "list"], timeout)
    if not ok:
        # 读配置失败不能静默退回"默认 pypi.org"再对它发真实请求——报告会误导
        # 用户以为自己在用官方源。显式降级为 info 并说明原因。
        return _doris("python.mirror", "包镜像源", "info",
                      ["无法读取 pip 配置（pip config list 失败），跳过镜像源探测"],
                      hint="先排查 pip：python -m pip config list")
    index = None
    for line in text.splitlines():
        m = re.search(r"index-url=(\S+)", line)
        if m:
            index = m.group(1).strip("'\"")
    base = (index or "https://pypi.org/simple").rstrip("/")
    # 请求用**真实** base（带凭据才能访问私有源），但报告里一律只出现脱敏形态：
    # 下面的可达/不可达两行同样会回显这个地址，漏一处就等于把 token 写进报告。
    shown = kobo_kanaeru(base)
    detail = [f"当前 index-url: {kobo_kanaeru(index) if index else '默认 (pypi.org)'}"]
    try:
        t0 = time.perf_counter()
        with urllib.request.urlopen(f"{base}/simple/", timeout=8):
            pass
        ms = (time.perf_counter() - t0) * 1000
        # 必须是 .0f：裸 .0 走 %g 风格，超过 1s 会渲染成 1e+03ms
        detail.append(f"GET {shown}/simple/ 可达（{ms:.0f}ms）")
        return _doris("python.mirror", "包镜像源", "ok", detail)
    except Exception as e:
        detail.append(f"{shown} 不可达: {type(e).__name__}")
        return _doris("python.mirror", "包镜像源", "warn", detail,
                    hint="换用可达的镜像源：pip config set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple")

_IMPORT_LIBS = ["pip", "setuptools", "wheel", "requests", "numpy", "pandas"]

def _yukoku_roberu(lib: str):
    def fn(_cfg: dict) -> dict:
        if importlib.util.find_spec(lib) is None:
            return _doris(f"python.import.{lib}", f"导入 · {lib}", "info", ["未安装"])
        r = subprocess.run(
            [sys.executable, "-c", _COLD_IMPORT_CODE, lib],
            capture_output=True, timeout=10,
            creationflags=_SUBPROCESS_NO_WINDOW,
        )
        text = _spade_echo(r.stdout).strip()
        try:
            ms = float(text)
        except ValueError:
            ms = -1.0
        if ms < 0:
            return _doris(f"python.import.{lib}", f"导入 · {lib}", "warn", ["导入失败（全新子进程中）"],
                        hint="库安装可能损坏：pip install -U --force-reinstall " + lib)
        return _doris(f"python.import.{lib}", f"导入 · {lib}", "ok",
                    [f"全新子进程冷导入 {ms:.0f}ms（无缓存污染）"])
    return fn

def kishido_temma(_cfg: dict) -> dict:
    if sys.platform != "win32":
        return _doris("python.store_alias", "Windows Store 别名", "skip", ["仅 Windows"])
    local_app = os.environ.get("LOCALAPPDATA")
    if not local_app:
        return _doris("python.store_alias", "Windows Store 别名", "skip", ["LOCALAPPDATA 未设置"])
    alias = Path(local_app) / "Microsoft" / "WindowsApps" / "python.exe"
    if not alias.exists():
        return _doris("python.store_alias", "Windows Store 别名", "ok", ["未发现 Store 别名文件"])
    resolved = shutil.which("python")
    if resolved and Path(resolved).resolve() == alias.resolve():
        return _doris("python.store_alias", "Windows Store 别名", "warn",
                    ["Store 别名正在拦截 python 命令（输入 python 会打开商店/商店版 Python）"],
                    hint="设置 → 应用 → 高级应用设置 → 应用执行别名，关闭 python.exe 与 python3.exe")
    return _doris("python.store_alias", "Windows Store 别名", "info",
                [f"别名文件存在，但未拦截当前 python（当前: {resolved or '未知'}）"])

def aragami_oga(_cfg: dict) -> dict:
    try:
        enabled = sys._is_gil_enabled()
    except AttributeError:
        return _doris("python.gil", "GIL", "info", ["当前解释器不支持自由线程（GIL 恒启用）"])
    if enabled:
        return _doris("python.gil", "GIL", "info", ["GIL 已启用（默认模式）"])
    return _doris("python.gil", "GIL", "info", ["自由线程模式（free-threading）"])

def kageyama_shien(_cfg: dict) -> dict:
    keys = ["VIRTUAL_ENV", "CONDA_DEFAULT_ENV", "CONDA_PREFIX", "PIP_INDEX_URL",
            "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "PYTHONUTF8", "PYTHONIOENCODING"]
    # 代理地址与私有 index-url 常内嵌 user:pass@/user:token@，报告会被导出或粘贴到 issue，
    # 一律走与 Rust 侧同规则的脱敏，避免凭据明文落盘。
    detail = [f"{k} = {kobo_kanaeru(os.environ[k])}" for k in keys if os.environ.get(k)]
    return _doris("python.env_vars", "相关环境变量", "info", detail or ["未设置相关环境变量"])

# ---------------------------------------------------------------- 宿主环境类检查（env / hardware / network）
#
# 这些检查的类别不是 python：id 前缀即类别（见 _doris 的推导），
# 因此「用 Python 实现」与「归到哪个类别」互不绑定。

def axel_syrios(_cfg: dict) -> dict:
    """env.codepage：控制台代码页、系统 ANSI 代码页与 Python 输出编码是否自洽。"""
    if sys.platform != "win32":
        return _doris("env.codepage", "控制台编码", "skip", ["仅 Windows 需要检查代码页"])
    cp = acp = 0
    try:
        k32 = ctypes.windll.kernel32
        cp = int(k32.GetConsoleOutputCP())
        acp = int(k32.GetACP())
    except Exception:  # noqa: BLE001 —— 取不到按"无控制台"处理，不是问题
        pass
    out_enc = (getattr(sys.stdout, "encoding", "") or "").lower() or "未知"
    preferred = (locale.getpreferredencoding(False) or "").lower() or "未知"
    utf8_mode = os.environ.get("PYTHONUTF8") == "1"
    io_enc = (os.environ.get("PYTHONIOENCODING") or "").lower()
    detail = [
        f"控制台代码页: {cp or '无控制台'}",
        f"系统 ANSI 代码页: {acp or '未知'}",
        f"Python 输出编码: {out_enc}",
        f"locale 首选编码: {preferred}",
    ]
    if utf8_mode or "utf-8" in io_enc:
        detail.append("UTF-8 模式已启用")
    if utf8_mode or out_enc.startswith("utf-8"):
        return _doris("env.codepage", "控制台编码", "ok", detail)
    return _doris(
        "env.codepage", "控制台编码", "warn", detail,
        hint="输出编码不是 UTF-8：管道/重定向下中文与图标可能抛 UnicodeEncodeError；"
             "建议 set PYTHONUTF8=1（或 python -X utf8），控制台可先 chcp 65001",
    )

def gavis_bettel(_cfg: dict) -> dict:
    """python.permissions：site-packages 是否真的可写（写入探针），以及管理员状态。

    不用 `site.getsitepackages()[0]`：venv 下它返回 venv 根而不是 site-packages（实测）；
    `os.access(W_OK)` 在 Windows 上只看只读属性、不反映 ACL，因此以真实写入为准。
    """
    try:
        import sysconfig
        purelib = sysconfig.get_paths().get("purelib") or ""
    except Exception:  # noqa: BLE001
        purelib = ""
    if not purelib:
        return _doris("python.permissions", "安装目录权限", "skip", ["无法确定 site-packages 路径"])
    target = Path(purelib)
    detail = [f"site-packages: {target}"]
    if not target.is_dir():
        return _doris("python.permissions", "安装目录权限", "skip", detail + ["目录不存在"])
    probe = target / f".envdoctor_write_{os.getpid()}"
    try:
        probe.write_text("x", encoding="utf-8")
        detail.append("写入探针: 通过")
        status, hint = "ok", None
    except OSError as e:
        detail.append(f"写入探针: 失败（{type(e).__name__}）")
        status = "warn"
        hint = "当前解释器装不进新包：建议用项目内 venv，或 pip install --user"
    finally:
        try:
            probe.unlink()
        except OSError:
            pass
    if sys.platform == "win32":
        try:
            admin = bool(ctypes.windll.shell32.IsUserAnAdmin())
            detail.append(f"管理员权限: {'是' if admin else '否'}")
        except Exception:  # noqa: BLE001
            pass
    return _doris("python.permissions", "安装目录权限", status, detail, hint=hint)

_HOSTS_HOT = ("github.com", "raw.githubusercontent.com", "objects.githubusercontent.com",
              "pypi.org", "files.pythonhosted.org")

def banzoin_hakka(text: str, hot_domains: tuple[str, ...] = _HOSTS_HOT) -> dict:
    """纯函数：统计 hosts 自定义记录。只返回条数与命中的公开域名，不回显内容。"""
    lines = [l.strip() for l in text.splitlines()]
    custom = [l for l in lines if l and not l.startswith("#")]
    hot = sorted({d for d in hot_domains if any(d in l for l in custom)})
    return {"total": len(lines), "custom": len(custom), "hot": hot}

def josuiji_shinri(_cfg: dict) -> dict:
    """network.hosts：hosts 是否存在、有多少条自定义解析记录。

    隐私：只报条数与"是否命中常见加速域名"，**不回显任何映射内容**（内网映射常含敏感信息）。
    """
    root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    path = root / "System32" / "drivers" / "etc" / "hosts"
    if not path.is_file():
        return _doris("network.hosts", "hosts 解析", "skip", [f"未找到 {path}"])
    try:
        text = _spade_echo(path.read_bytes())
    except OSError as e:
        return _doris("network.hosts", "hosts 解析", "skip", [f"读取 hosts 失败（{type(e).__name__}）"])
    r = banzoin_hakka(text)
    detail = [f"自定义解析记录: {r['custom']} 条（内容不回显）"]
    if r["hot"]:
        detail.append("命中常见加速域名: " + ", ".join(r["hot"]))
        detail.append("代理/加速工具常改写 hosts；若访问异常，先核对这些记录是否仍然有效")
    return _doris("network.hosts", "hosts 解析", "info", detail)

def jurard_t_rexford(_cfg: dict) -> dict:
    """python.ssl：CA 来源与一次真实 TLS 握手。

    注意：Windows 上 `get_default_verify_paths()` 的 cafile/capath 通常为空（走系统证书库），
    那是正常状态，不能报成问题。
    """
    detail = [f"OpenSSL: {ssl.OPENSSL_VERSION}"]
    try:
        paths = ssl.get_default_verify_paths()
        if paths.cafile or paths.capath:
            detail.append(f"CA 文件/目录: {paths.cafile or paths.capath}")
        else:
            detail.append("CA: 使用系统证书库（Windows 默认行为，非异常）")
    except Exception as e:  # noqa: BLE001
        detail.append(f"读取 CA 配置失败: {type(e).__name__}")
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen("https://pypi.org", timeout=8):
            pass
    except urllib.error.URLError as e:
        reason = getattr(e, "reason", e)
        detail.append(f"TLS 握手 pypi.org: 失败（{type(reason).__name__}）")
        if isinstance(reason, ssl.SSLCertVerificationError):
            hint = "证书校验失败：多为中间人代理/企业根证书场景，装其根证书或临时改用可信镜像"
        else:
            hint = "TLS 握手失败：确认网络与代理设置，或先换国内镜像验证"
        return _doris("python.ssl", "证书与 TLS", "warn", detail, hint=hint)
    except Exception as e:  # noqa: BLE001
        detail.append(f"TLS 握手 pypi.org: 未完成（{type(e).__name__}）")
        return _doris("python.ssl", "证书与 TLS", "skip", detail)
    ms = (time.perf_counter() - t0) * 1000
    detail.append(f"TLS 握手 pypi.org: 成功（{ms:.0f}ms）")
    if ms > 3000:
        return _doris("python.ssl", "证书与 TLS", "warn", detail,
                      hint="握手异常缓慢，可能是代理/加速器链路问题，pip 安装会明显变慢")
    return _doris("python.ssl", "证书与 TLS", "ok", detail)

class KokoroTsurumaki(ctypes.Structure):
    """SYSTEM_INFO（GetNativeSystemInfo）。"""

    _fields_ = [
        ("wProcessorArchitecture", ctypes.c_ushort), ("wReserved", ctypes.c_ushort),
        ("dwPageSize", ctypes.c_ulong), ("lpMinimumApplicationAddress", ctypes.c_void_p),
        ("lpMaximumApplicationAddress", ctypes.c_void_p), ("dwActiveProcessorMask", ctypes.c_void_p),
        ("dwNumberOfProcessors", ctypes.c_ulong), ("dwProcessorType", ctypes.c_ulong),
        ("dwAllocationGranularity", ctypes.c_ulong), ("wProcessorLevel", ctypes.c_ushort),
        ("wProcessorRevision", ctypes.c_ushort),
    ]

class RinkoShirokane(ctypes.Structure):
    """SYSTEM_LOGICAL_PROCESSOR_INFORMATION（GetLogicalProcessorInformation）。

    布局按 **64 位进程**对齐（size_t=8 → union 载荷偏移 16）；32 位 Python 下
    结构体会比系统预期的小，枚举结果不可信——但 octavio() 对失败有兜底（返回 0，
    检查降级为只报逻辑核数），只是不精确，不会崩。
    """

    _fields_ = [
        ("ProcessorMask", ctypes.c_size_t),
        ("Relationship", ctypes.c_int),
        ("_pad", ctypes.c_int),
        ("_payload", ctypes.c_ulonglong * 2),
    ]

def octavio() -> int:
    """数物理核（RelationProcessorCore == 0）；失败返回 0，由调用方降级。"""
    try:
        k32 = ctypes.windll.kernel32
        size = ctypes.c_ulong(0)
        k32.GetLogicalProcessorInformation(None, ctypes.byref(size))
        if not size.value:
            return 0
        count = size.value // ctypes.sizeof(RinkoShirokane)
        buf = (RinkoShirokane * count)()
        if not k32.GetLogicalProcessorInformation(buf, ctypes.byref(size)):
            return 0
        return sum(1 for i in range(count) if buf[i].Relationship == 0)
    except Exception:  # noqa: BLE001
        return 0

def goldbullet(_cfg: dict) -> dict:
    """hardware.cpu：型号、厂商、标称频率与核心数（注册表 + Win32，不启动子进程）。"""
    if sys.platform != "win32":
        return _doris("hardware.cpu", "CPU", "skip", ["仅 Windows 实现"])
    detail: list[str] = []
    got_name = False
    try:
        import winreg
        vals: dict[str, object] = {}
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
                            0, winreg.KEY_READ) as k:
            try:
                for i in range(winreg.QueryInfoKey(k)[1]):
                    vname, vdata, _ = winreg.EnumValue(k, i)
                    vals[vname] = vdata
            except OSError:
                pass
        name = vals.get("ProcessorNameString")
        vendor = vals.get("VendorIdentifier")
        mhz = vals.get("~MHz")
        if name:
            detail.append(f"型号: {name}")
            got_name = True
        if vendor:
            detail.append(f"厂商: {vendor}")
        if mhz:
            detail.append(f"标称频率: {mhz}MHz（注册表 ~MHz，非当前频率）")
    except Exception as e:  # noqa: BLE001
        detail.append(f"注册表读取失败: {type(e).__name__}")
    logical = 0
    try:
        si = KokoroTsurumaki()
        ctypes.windll.kernel32.GetNativeSystemInfo(ctypes.byref(si))
        logical = int(si.dwNumberOfProcessors)
    except Exception:  # noqa: BLE001
        pass
    physical = octavio()
    if physical and logical:
        detail.append(f"核心: {physical} 物理 / {logical} 逻辑")
    elif logical:
        detail.append(f"逻辑处理器: {logical}")
    if not got_name and not detail:
        return _doris("hardware.cpu", "CPU", "info", ["未获取到 CPU 信息"])
    return _doris("hardware.cpu", "CPU", "ok" if got_name else "info", detail)

# ---------------------------------------------------------------- 环境与生态明细（D2）

# 遍历上限：原件 analyze_filesystem / pip 缓存统计都是无上限整树遍历（包多的环境可达数十秒）
_WALK_MAX_FILES = 20000
_WALK_MAX_SECONDS = 3.0

# (模块名, 发行名) 对照表：模块名常与发行名不同（cv2→opencv-python、sklearn→scikit-learn、PIL→pillow），
# 按模块名取版本会取不到。
_LIB_TABLE: tuple[tuple[str, str], ...] = (
    ("numpy", "numpy"), ("pandas", "pandas"), ("requests", "requests"),
    ("cv2", "opencv-python"), ("sklearn", "scikit-learn"), ("PIL", "pillow"),
    ("flask", "flask"), ("django", "django"), ("fastapi", "fastapi"),
    ("pymysql", "pymysql"), ("psycopg2", "psycopg2-binary"), ("redis", "redis"),
    ("pymongo", "pymongo"), ("torch", "torch"), ("matplotlib", "matplotlib"), ("scipy", "scipy"),
)
_PKG_TABLE: tuple[tuple[str, str], ...] = (
    ("PyInstaller", "pyinstaller"), ("nuitka", "nuitka"), ("cx_Freeze", "cx-freeze"),
)

def crimzon_ruze(root: Path, max_files: int = _WALK_MAX_FILES,
                 max_seconds: float = _WALK_MAX_SECONDS) -> dict:
    """带上限的目录统计：文件数与总字节。达到任一上限即截断并置 truncated。"""
    files = 0
    total = 0
    truncated = False
    deadline = time.perf_counter() + max_seconds
    stack = [root]
    while stack:
        d = stack.pop()
        try:
            with os.scandir(d) as it:
                for e in it:
                    files += 1
                    if files > max_files or time.perf_counter() > deadline:
                        truncated = True
                        break
                    try:
                        if e.is_dir(follow_symlinks=False):
                            stack.append(Path(e.path))
                        else:
                            total += e.stat(follow_symlinks=False).st_size
                    except OSError:
                        continue
        except OSError:
            continue
        if truncated:
            break
    return {"files": files, "bytes": total, "truncated": truncated}

def ushimi_ichigo(pairs) -> list[str]:
    """按 (模块名, 发行名) 对照表列出已安装项及其版本。**绝不 import**（避免导入副作用）。"""
    found = []
    for mod, dist in pairs:
        if importlib.util.find_spec(mod) is None:
            continue
        try:
            ver = importlib.metadata.version(dist)
        except Exception:  # noqa: BLE001 —— 发行名对不上时只报未知，不猜
            ver = "版本未知"
        found.append(f"{mod} {ver}")
    return found

def moira(cfg: dict) -> dict:
    """python.startup：解释器冷启动耗时，把"环境慢"拆成启动慢 vs 导入慢。"""
    budget = int(cfg.get("timeout_secs", 25)) if isinstance(cfg, dict) else 25
    times: list[float] = []
    for _ in range(2):
        t0 = time.perf_counter()
        try:
            subprocess.run([sys.executable, "-c", "pass"], capture_output=True,
                           timeout=max(3, min(budget, 20)),
                           creationflags=_SUBPROCESS_NO_WINDOW)
        except subprocess.TimeoutExpired:
            return _doris("python.startup", "解释器启动", "warn", ["解释器启动超时"],
                          hint="启动一个空脚本都超时，通常意味着启动钩子/杀软扫描把解释器卡住了")
        times.append((time.perf_counter() - t0) * 1000)
    detail = [f"冷启动 {times[0]:.0f}ms / 预热 {times[-1]:.0f}ms"]
    if times[-1] > 1500:
        return _doris("python.startup", "解释器启动", "warn", detail,
                      hint="启动异常慢：常见于 site-packages 过大、杀软实时扫描、或启动钩子过多；"
                           "可对比 python.startup 与 python.import.* 判断瓶颈在启动还是在导入")
    return _doris("python.startup", "解释器启动", "ok", detail)

def elu(_cfg: dict) -> dict:
    """python.pip_env：pip 配置文件位置与 index-url、缓存体积、site-packages 位置。"""
    detail: list[str] = []
    if sys.platform == "win32":
        # Windows 下 pip 实际读取的是 %APPDATA%\pip\pip.ini（用户级）与
        # %PROGRAMDATA%\pip\pip.ini（系统级）；~/pip/pip.ini 不是 pip 的读取位置，
        # 列进去会暗示"pip 在用它"而误导。
        cands = [Path(os.environ.get("APPDATA", str(Path(os.path.expanduser("~"))))) / "pip" / "pip.ini",
                 Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "pip" / "pip.ini"]
    else:
        home = Path(os.path.expanduser("~"))
        cands = [home / ".pip" / "pip.conf", home / ".config" / "pip" / "pip.conf",
                 Path("/etc/pip.conf")]
    hits = [c for c in cands if c.is_file()]
    if hits:
        detail.append("配置文件: " + ", ".join(str(p) for p in hits))
        try:
            # pip.ini 常为 GBK/ANSI：走协商解码，避免中文注释乱码；只回显 index-url 且脱敏
            for line in _spade_echo(hits[0].read_bytes()).splitlines():
                if "index-url" in line.lower():
                    detail.append(kobo_kanaeru(line.strip()))
                    break
        except OSError:
            pass
    else:
        detail.append("未找到 pip 配置文件（使用默认源）")
    try:
        r = subprocess.run([sys.executable, "-m", "pip", "cache", "dir"],
                           capture_output=True, timeout=10,
                           creationflags=_SUBPROCESS_NO_WINDOW)
        lines = [l for l in _spade_echo(r.stdout).splitlines() if l.strip()]
        if r.returncode == 0 and lines:
            cache = Path(lines[0].strip())
            if cache.is_dir():
                stat = crimzon_ruze(cache)
                tail = "（已达统计上限，为下界）" if stat["truncated"] else ""
                detail.append(f"缓存: {stat['files']} 个条目 / {stat['bytes'] / 2 ** 20:.1f}MB{tail}")
            else:
                detail.append("缓存目录不存在")
    except (OSError, subprocess.TimeoutExpired) as e:
        detail.append(f"缓存统计失败: {type(e).__name__}")
    try:
        import sysconfig
        purelib = sysconfig.get_paths().get("purelib") or ""
        if purelib:
            detail.append(f"site-packages: {purelib}")
    except Exception:  # noqa: BLE001
        pass
    return _doris("python.pip_env", "pip 环境", "info", detail)

def yuki_chihiro(_cfg: dict) -> dict:
    """python.libs：常用库是否安装与版本（只列已安装项，避免报告变成一墙"未安装"）。"""
    found = ushimi_ichigo(_LIB_TABLE)
    if not found:
        return _doris("python.libs", "常用库", "info", ["常用库均未安装（干净环境）"])
    lines = [", ".join(found[i:i + 4]) for i in range(0, len(found), 4)]
    return _doris("python.libs", "常用库", "info", [f"已安装 {len(found)} 个:"] + [f"  {l}" for l in lines])

def suzuya_aki(_cfg: dict) -> dict:
    """python.packaging：打包工具是否可用（同样不 import）。"""
    found = ushimi_ichigo(_PKG_TABLE)
    if not found:
        return _doris("python.packaging", "打包工具", "info", ["PyInstaller/Nuitka/cx_Freeze 均未安装"])
    return _doris("python.packaging", "打包工具", "info", [", ".join(found)])

def ienaga_mugi(_cfg: dict) -> dict:
    """python.cache_size：.pyc 数量与体积、元数据目录数（scandir + 上限遍历）。"""
    try:
        import sysconfig
        purelib = Path(sysconfig.get_paths().get("purelib") or "")
    except Exception:  # noqa: BLE001
        purelib = Path()
    if not purelib.is_dir():
        return _doris("python.cache_size", "字节码缓存", "skip", ["未找到 site-packages"])
    pyc = pyc_bytes = dist_info = files = 0
    truncated = False
    deadline = time.perf_counter() + _WALK_MAX_SECONDS
    stack = [purelib]
    while stack:
        d = stack.pop()
        try:
            with os.scandir(d) as it:
                for e in it:
                    files += 1
                    if files > _WALK_MAX_FILES or time.perf_counter() > deadline:
                        truncated = True
                        break
                    try:
                        if e.is_dir(follow_symlinks=False):
                            if e.name.endswith((".dist-info", ".egg-info")):
                                dist_info += 1
                            stack.append(Path(e.path))       # 不跳过 __pycache__：.pyc 就在里面
                        elif e.name.endswith(".pyc"):
                            pyc += 1
                            pyc_bytes += e.stat(follow_symlinks=False).st_size
                    except OSError:
                        continue
        except OSError:
            continue
        if truncated:
            break
    tail = "（已达上限，为下界）" if truncated else ""
    detail = [f".pyc {pyc} 个 / {pyc_bytes / 2 ** 20:.1f}MB；元数据目录 {dist_info} 个；"
              f"扫描 {files} 个条目{tail}"]
    return _doris("python.cache_size", "字节码缓存", "info", detail)

def morinaka_kazaki(text: str) -> list[str]:
    """纯函数：从 `git config --get-regexp` 输出里**只取键名**。

    值属于用户身份信息（姓名/邮箱），一律丢弃、绝不进报告。
    只接受形如 `section.key` 的首字段——输出形态万一异常时，宁可少报也不能把"值"当键回显。
    """
    keys = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        first = line.split(None, 1)[0]
        if re.fullmatch(r"[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+", first):
            keys.append(first)
    return sorted(set(keys))

def kenmochi_toya(_cfg: dict) -> dict:
    """toolchains.git_identity：git 身份是否已配置（只报"是否"，不回显值）。"""
    # 裸 "git" 会走 CreateProcess 搜索顺序（含当前工作目录）：在被扫描目录放一个
    # git.exe 就会被执行。先按 PATH 解析成绝对路径（解析不到再回落裸名，让 OSError
    # 走既有的 skip 分支）。
    git_exe = shutil.which("git") or "git"
    try:
        r = subprocess.run([git_exe, "config", "--get-regexp",
                            r"^(user\.(name|email)|core\.autocrlf)$"],
                           capture_output=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as e:
        return _doris("toolchains.git_identity", "Git 身份", "skip",
                      [f"无法执行 git（{type(e).__name__}）"])
    keys = morinaka_kazaki(_spade_echo(r.stdout))
    detail = [f"已配置: {', '.join(keys)}" if keys else "user.name / user.email 均未配置"]
    if "user.name" not in keys or "user.email" not in keys:
        return _doris("toolchains.git_identity", "Git 身份", "warn", detail,
                      hint="提交会失败或用错身份：git config --global user.name / user.email 各设一次")
    return _doris("toolchains.git_identity", "Git 身份", "ok", detail)

def fushimi_gaku(_cfg: dict) -> dict:
    """toolchains.ssh_keys：~/.ssh 是否存在、公钥数量、known_hosts 是否存在。

    只报数量与存在性：公钥文件名/注释常含邮箱或主机名。
    """
    ssh = Path(os.path.expanduser("~")) / ".ssh"
    if not ssh.is_dir():
        return _doris("toolchains.ssh_keys", "SSH 密钥", "info", ["未找到 ~/.ssh（尚未配置 SSH）"])
    try:
        pubs = [p for p in ssh.glob("*.pub") if p.is_file()]
    except OSError:
        pubs = []
    has_known = (ssh / "known_hosts").is_file()
    detail = [f"公钥 {len(pubs)} 个", f"known_hosts: {'存在' if has_known else '不存在'}"]
    if not pubs:
        detail.append("无公钥：若需免密访问 Git 远端，先用 ssh-keygen 生成")
    return _doris("toolchains.ssh_keys", "SSH 密钥", "info", detail)

# ---------------------------------------------------------------- 完整性与生态陷阱（D3）
#
# 这一批的共同点：结论必须"可行动"，且**取数与判定分离**——判定写成纯函数或用可注入参数，
# I/O 走模块级可 mock 的调用，因此单测不依赖宿主环境（本机装没装 Java 都不影响结论）。

_PTH_MAX_BYTES = 64 * 1024
_SHADOW_MAX_DIRS = 8
_TEMP_PATH_MAX = 150

# 与标准库/常用库同名的"影子模块"是"莫名 ImportError"的头号根因。标准库名单取自
# sys.stdlib_module_names，这里只补第三方常用顶层模块（它们同样会被 CWD 里的同名文件抢走）。
_SHADOW_LIBS = frozenset({
    "numpy", "pandas", "requests", "flask", "django", "fastapi", "cv2", "sklearn",
    "PIL", "torch", "pytest", "yaml", "dotenv", "setuptools", "pip", "wheel",
    "psycopg2", "pymysql", "redis", "pymongo", "matplotlib", "scipy", "httpx",
})

def uzuki_kou(text: str, exists=None) -> dict:
    """纯函数：解析 pyvenv.cfg，并核对基解释器是否仍然存在。

    `exists` 可注入，判定因此不依赖真实文件系统。返回的 `alive` 是仍然存在的候选基解释器
    —— 空列表就是"僵尸 venv"（创建它的基 Python 已被删除或升级到别的目录）。
    """
    exists = exists or os.path.exists
    cfg: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        cfg[key.strip().lower()] = value.strip()
    home = cfg.get("home", "")
    explicit = cfg.get("base-executable", "") or cfg.get("executable", "")
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    if home:
        # venv 的 home 是"装解释器的目录"：Windows 下里面是 python.exe，POSIX 下是 python3
        candidates += [str(Path(home) / n)
                       for n in ("python.exe", "python3.exe", "python3", "python")]
    return {
        "version": cfg.get("version", "") or cfg.get("version_info", ""),
        "home": home,
        "candidates": candidates,
        "alive": [c for c in candidates if exists(c)],
    }

def yashiro_kizuku(_cfg: dict) -> dict:
    """python.venv_integrity：venv 的基解释器是否还在（基 Python 被删/升级后的僵尸环境）。"""
    id_, title = "python.venv_integrity", "虚拟环境完整性"
    in_venv = sys.prefix != getattr(sys, "base_prefix", sys.prefix)
    cfg_path = Path(sys.prefix) / "pyvenv.cfg"
    if not cfg_path.is_file():
        reason = "当前不是 venv" if not in_venv else "未找到 pyvenv.cfg（conda 等非 venv 形态）"
        return _doris(id_, title, "skip", [reason])
    try:
        text = _spade_echo(cfg_path.read_bytes())
    except OSError as e:
        return _doris(id_, title, "skip", [f"读取 pyvenv.cfg 失败（{type(e).__name__}）"])
    info = uzuki_kou(text)
    detail = [f"环境: {sys.prefix}"]
    if info["version"]:
        detail.append(f"创建时基版本: {info['version']}")
    if not info["candidates"]:
        return _doris(id_, title, "info",
                      detail + ["pyvenv.cfg 未记录 home/base-executable，无从核对基解释器"])
    if info["alive"]:
        return _doris(id_, title, "ok", detail + [f"基解释器仍在: {info['alive'][0]}"])
    return _doris(
        id_, title, "warn",
        detail + [f"基解释器已缺失（{len(info['candidates'])} 个候选路径均不存在）"],
        hint="僵尸 venv：创建它的基 Python 已被删除或升级到别的目录，继续用它装包会报路径错乱的错误；"
             "建议重建环境（删掉现有 venv 后重新 python -m venv .venv）",
    )

def kuroi_shiba(extra=()) -> frozenset[str]:
    """标准库 + 常用库里"放在 sys.path 前面就会真的抢走导入"的顶层模块名。

    排除两类：内置模块（`sys.builtin_module_names`，C 层实现，路径上的同名文件无效）与
    冻结模块（`os`/`site`/`abc` 等，FrozenImporter 排在 PathFinder 之前）。
    不排除的话，项目里一个完全无害的 `os.py` 就会被报成问题——这类检查宁可少报也不能误报。
    """
    names = set(getattr(sys, "stdlib_module_names", frozenset())) | set(_SHADOW_LIBS) | set(extra)
    names -= set(sys.builtin_module_names)
    try:
        from importlib.machinery import FrozenImporter
    except ImportError:  # pragma: no cover —— 极简解释器上可能不可用，此时退化为不排除
        return frozenset(n for n in names if n.isidentifier())
    out: set[str] = set()
    for name in names:
        if not name.isidentifier():
            continue
        try:
            if FrozenImporter.find_spec(name, None) is not None:
                continue
        except Exception:  # noqa: BLE001 —— 探测失败只影响"多报/少报"，不该中断检查
            pass
        out.add(name)
    return frozenset(out)

def nakao_azuma(entries, names) -> list[str]:
    """纯函数：从目录条目名里挑出与标准库/常用库同名的模块。

    后缀为 `.py` 时去掉后缀再比对（`json.py` 命中 `json`）；包目录按目录名比对。
    """
    hits = []
    for entry in entries:
        if not entry:
            continue
        stem = entry[:-3] if entry.endswith(".py") else entry
        if stem in names:
            hits.append(entry)
    return sorted(set(hits))

def umiyashano_kami(_cfg: dict) -> dict:
    """python.shadowing：当前工作目录与 PYTHONPATH 里是否有"影子模块"。

    只看顶层名：`sys.path` 上排在前面的同名 .py / 包会把标准库或已装库整个换掉，
    现象通常是"昨天还能跑、今天 ImportError"或"属性凭空消失"。
    """
    id_, title = "python.shadowing", "模块遮蔽"
    names = kuroi_shiba()
    dirs: list[Path] = []
    try:
        dirs.append(Path.cwd())
    except OSError:
        pass
    for item in os.environ.get("PYTHONPATH", "").split(os.pathsep):
        item = item.strip().strip('"')
        if item:
            dirs.append(Path(item))
    uniq: list[Path] = []
    seen: set[str] = set()
    for d in dirs:
        key = str(d).rstrip("\\/").lower()
        if key in seen or not d.is_dir():
            continue
        seen.add(key)
        uniq.append(d)
    uniq = uniq[:_SHADOW_MAX_DIRS]
    hits: list[str] = []
    scanned = 0
    truncated = False
    deadline = time.perf_counter() + _WALK_MAX_SECONDS
    for d in uniq:
        plausible: list[str] = []
        try:
            with os.scandir(d) as it:
                for e in it:
                    scanned += 1
                    if scanned > _WALK_MAX_FILES or time.perf_counter() > deadline:
                        truncated = True
                        break
                    stem = e.name[:-3] if e.name.endswith(".py") else e.name
                    if stem not in names:
                        continue
                    if not e.name.endswith(".py"):
                        # 目录只有是包（含 __init__.py）才会遮蔽导入；普通同名目录无害
                        try:
                            if not (Path(e.path) / "__init__.py").is_file():
                                continue
                        except OSError:
                            continue
                    plausible.append(e.name)
        except OSError:
            continue
        hits += [f"{h}（{d}）" for h in nakao_azuma(plausible, names)]
    tail = "（已达扫描上限）" if truncated else ""
    detail = [f"已扫描 {len(uniq)} 个目录（当前工作目录 + PYTHONPATH），{scanned} 个条目{tail}"]
    if hits:
        detail.append(f"与标准库/常用库同名的模块 {len(hits)} 个:")
        detail += [f"  {h}" for h in hits[:6]]
        if len(hits) > 6:
            detail.append(f"  …另有 {len(hits) - 6} 个未列出")
        return _doris(
            id_, title, "warn", detail,
            hint="sys.path 上排在前面的同名模块会顶掉标准库或已装库，表现为莫名的 ImportError 或"
                 "“属性不见了”；给项目文件改名（或把代码收进包目录）即可",
        )
    return _doris(id_, title, "ok", detail + ["未发现影子模块"])

def hassaku_yuzu(text: str) -> dict:
    """纯函数：统计 `.pth` 里的路径条目与可执行语句条数。

    `.pth` 每行的正常语义是"追加一个路径"，但以 `import ` / `exec` 开头的行会被 site.py
    直接执行（历史遗留的可执行钩子）。只回条数与关键字，**不回显整行**——那些行往往含内网路径。
    """
    body = [l.strip() for l in text.splitlines()]
    body = [l for l in body if l and not l.startswith("#")]
    executable = [l for l in body if re.match(r"(import|exec)[\s(]", l)]
    return {"paths": len(body), "executable": len(executable)}

def izumo_kasumi(_cfg: dict) -> dict:
    """python.pth_files：site-packages 顶层 `.pth` 的数量与可执行钩子。"""
    id_, title = "python.pth_files", ".pth 路径注入"
    try:
        import sysconfig
        purelib = Path(sysconfig.get_paths().get("purelib") or "")
    except Exception:  # noqa: BLE001
        purelib = Path()
    if not purelib.is_dir():
        return _doris(id_, title, "skip", ["未找到 site-packages"])
    pths: list[Path] = []
    try:
        with os.scandir(purelib) as it:
            for e in it:
                if e.name.endswith(".pth") and e.is_file(follow_symlinks=False):
                    pths.append(Path(e.path))
    except OSError as e:
        return _doris(id_, title, "skip", [f"枚举 site-packages 失败（{type(e).__name__}）"])
    if not pths:
        return _doris(id_, title, "info", [f"site-packages: {purelib}", "顶层没有 .pth 文件"])
    paths = exec_files = 0
    for p in sorted(pths):
        try:
            raw = p.read_bytes()[:_PTH_MAX_BYTES]
        except OSError:
            continue
        stat = hassaku_yuzu(_spade_echo(raw))
        paths += stat["paths"]
        if stat["executable"]:
            exec_files += 1
    detail = [f"site-packages: {purelib}",
              f".pth 文件 {len(pths)} 个 / 路径条目 {paths} 条"]
    if exec_files:
        detail.append(f"含可执行语句的 .pth: {exec_files} 个（语句内容不回显）")
        return _doris(
            id_, title, "warn", detail,
            hint=".pth 里的 import/exec 行会在每次启动解释器时执行（site.py 行为）：会拖慢启动，"
                 "也可能在导入期埋下副作用；来源不明时应打开对应 .pth 确认内容",
        )
    detail.append("全部为纯路径注入")
    return _doris(id_, title, "info", detail)

def azuchi_momo(text: str, returncode: int = 0) -> dict:
    """纯函数：把 `pip check` 的输出压成"首条冲突 + 总条数"。

    冲突多时输出会很长（每个冲突一行），报告里只放首行，避免 detail 被撑爆。
    """
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    body = [l for l in lines if not l.lower().startswith(("warning:", "notice:", "deprecationwarning"))]
    return {
        "conflicts": len(body) if returncode else 0,
        "head": body[0] if body else "",
    }

def harusaki_air(cfg: dict) -> dict:
    """python.pip_check：已装包之间的依赖冲突（`pip check`）。

    自带预算：包多的环境要跑十几秒，但也不能无限等——超时记 skip（不是"没有冲突"）。
    """
    id_, title = "python.pip_check", "依赖冲突"
    budget = int(cfg.get("timeout_secs", 30) or 30)
    timeout = min(max(budget, 30), 60)
    try:
        r = subprocess.run([sys.executable, "-m", "pip", "check"],
                           capture_output=True, timeout=timeout,
                           creationflags=_SUBPROCESS_NO_WINDOW)
    except subprocess.TimeoutExpired:
        return _doris(id_, title, "skip", [f"pip check 超时（>{timeout}s）"],
                      hint="环境很大或磁盘慢时会超时；可单独跑 python -m pip check 复核")
    except OSError as e:
        return _doris(id_, title, "skip", [f"无法执行 pip check（{type(e).__name__}）"])
    if r.returncode == 0:
        return _doris(id_, title, "ok", ["未发现依赖冲突"])
    info = azuchi_momo(_spade_echo(r.stdout) or _spade_echo(r.stderr), r.returncode)
    if not info["head"]:
        return _doris(id_, title, "skip", ["pip check 返回非零但没有可解析的输出"])
    return _doris(
        id_, title, "warn",
        [f"冲突 {info['conflicts']} 条", f"首条: {info['head']}"],
        hint="按提示逐个修：pip install -U <包名>，或按 requirements.txt 重装一遍；"
             "冲突不会挡住解释器启动，但会让某些库在运行时才报错",
    )

# (说明, 注册表路径, 值名/None 表示"看这个键在不在")

def takamiya_rion(temp: str, tmp: str, exists: bool, writable: bool,
                  limit: int = _TEMP_PATH_MAX) -> tuple[str, list[str], str | None]:
    """纯函数：TEMP/TMP 的取值与探针结果 → 结论（不碰系统，便于离线测试）。"""
    chosen = temp or tmp
    detail = [f"TEMP: {temp or '未设置'}", f"TMP: {tmp or '未设置'}"]
    if not chosen:
        return ("fail", detail,
                "TEMP/TMP 均未设置：构建工具与解包程序找不到临时目录会直接报错；设置后需重开终端")
    flags = []
    if not chosen.isascii():
        flags.append("含非 ASCII 字符")
    if len(chosen) > limit:
        flags.append(f"过长（{len(chosen)} > {limit} 字符）")
    if not exists:
        return ("fail", detail + ["目录不存在"],
                "TEMP/TMP 指向的目录不存在：构建与解包会失败；把它指回可写目录")
    if not writable:
        return ("fail", detail + ["写入探针失败"],
                "临时目录不可写：构建与解包会失败；检查目录 ACL，或确认磁盘没满")
    if flags:
        return ("warn", detail + ["；".join(flags)],
                "临时路径里的非 ASCII / 超长会踩到一批老工具（旧 MSVC/nmake、部分包的编译脚本）"
                "按 ANSI 代码页处理路径的缺陷；建议把 TEMP 指到纯 ASCII 的短路径（如 C:\\Temp）")
    return "ok", detail, None

def asuka_hina(_cfg: dict) -> dict:
    """env.temp_path：TEMP/TMP 是否可用、是否踩到非 ASCII / 超长路径。"""
    id_, title = "env.temp_path", "临时目录路径"
    if sys.platform != "win32":
        return _doris(id_, title, "skip", ["仅 Windows（其他平台的临时目录约定不同）"])
    temp = os.environ.get("TEMP", "")
    tmp = os.environ.get("TMP", "")
    chosen = temp or tmp
    exists = bool(chosen) and Path(chosen).is_dir()
    writable = False
    if exists:
        probe = Path(chosen) / f".envdoctor_temppath_{os.getpid()}"
        try:
            probe.write_bytes(b"x")
            writable = True
        except OSError:
            writable = False
        finally:
            try:
                probe.unlink()
            except OSError:
                pass
    status_, detail, hint = takamiya_rion(temp, tmp, exists, writable)
    return _doris(id_, title, status_, detail, hint=hint)

def joe_rikiichi(java_home: str, on_path: str, home_java: str,
                 resolve=None) -> tuple[str, list[str], str | None]:
    """纯函数：比对 JAVA_HOME 下的 java 与 PATH 上的 java 是否同一个文件。

    `resolve` 可注入（默认 `Path.resolve`），所以判定不依赖真实文件系统。
    """
    resolve = resolve or (lambda p: str(Path(p).resolve()))
    if not java_home:
        return "info", ["JAVA_HOME 未设置（不装 Java 时属正常）"], None
    if not home_java:
        return ("info", [f"JAVA_HOME = {java_home}",
                         "该目录下没有 bin/java（可能是 JRE 布局，或路径写错）"], None)
    if not on_path:
        return ("info", [f"JAVA_HOME = {java_home}",
                         "PATH 上没有 java（只设了 JAVA_HOME，命令行调不到）"], None)
    here, there = resolve(home_java), resolve(on_path)
    if here == there:
        return "ok", [f"JAVA_HOME = {java_home}", f"与 PATH 上的 java 是同一个: {here}"], None
    return ("warn", [f"JAVA_HOME = {java_home}", f"JAVA_HOME 下: {here}", f"PATH 上: {there}"],
            "两处不是同一个 java：构建工具（Maven/Gradle/IDE）按 JAVA_HOME 走、命令行按 PATH 走，"
            "会出现“编译用 17、运行用 8”这类难查的版本错配；把 PATH 上的 java 指到 JAVA_HOME\\bin 即可")

def machita_chima(_cfg: dict) -> dict:
    """toolchains.java_home：JAVA_HOME 与 PATH 上的 java 是否同一个。"""
    id_, title = "toolchains.java_home", "JAVA_HOME 一致性"
    java_home = os.environ.get("JAVA_HOME", "").strip().strip('"')
    on_path = shutil.which("java") or ""
    home_java = ""
    if java_home:
        for name in ("java.exe", "java"):
            cand = Path(java_home) / "bin" / name
            if cand.is_file():
                home_java = str(cand)
                break
    status_, detail, hint = joe_rikiichi(java_home, on_path, home_java)
    return _doris(id_, title, status_, detail, hint=hint)

# 只取关键键：绝不用 `^(http|https|core)\.` 这种宽匹配——`http.<url>.extraheader` 里
# 装的是 Authorization 令牌，一旦被取出来就有落进报告的风险。
_GIT_CONFIG_KEYS = (
    r"http\.proxy", r"https\.proxy", r"http\.sslbackend",
    r"http\..*\.schannelcheckrevoke", r"core\.longpaths", r"core\.autocrlf",
)

def sakura_ritsuki(text: str) -> dict:
    """纯函数：解析 `git config --get-regexp` 输出里关心的几项，代理值走 `kobo_kanaeru` 脱敏。

    键与值都进不了报告的只有两类：子段含主机名的（只报条数）与代理 URL（凭据打码）。
    """
    proxies: list[str] = []
    revoked = 0
    scalars: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        key = parts[0].lower()
        value = parts[1].strip() if len(parts) > 1 else ""
        if key in ("http.proxy", "https.proxy"):
            proxies.append(f"{key} = {kobo_kanaeru(value)}")
        elif key.endswith(".schannelcheckrevoke"):
            # 子段是主机名/URL，可能含内网域名：只计数，不回显
            if value.lower() == "false":
                revoked += 1
        elif key in ("http.sslbackend", "core.longpaths", "core.autocrlf"):
            scalars[key] = value
    return {"proxies": proxies, "revoked": revoked, "scalars": scalars}

def belmond_banderas(_cfg: dict) -> dict:
    """toolchains.git_config：git 的代理/证书/换行关键配置。

    单次调用取全部键（分次调用会明显变慢）；**关键项缺失不算问题**——默认配置本来就没有这些键。
    """
    id_, title = "toolchains.git_config", "Git 关键配置"
    pattern = "^(" + "|".join(_GIT_CONFIG_KEYS) + ")$"
    git_exe = shutil.which("git") or "git"  # 先解析绝对路径，理由见 kenmochi_toya
    try:
        r = subprocess.run([git_exe, "config", "--get-regexp", pattern],
                           capture_output=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as e:
        return _doris(id_, title, "skip", [f"无法执行 git（{type(e).__name__}）"])
    info = sakura_ritsuki(_spade_echo(r.stdout))
    detail: list[str] = list(info["proxies"]) or ["未配置 http.proxy / https.proxy"]
    if info["revoked"]:
        detail.append(f"已关闭证书吊销检查的条目: {info['revoked']} 个（子段含主机名，不回显）")
        detail.append("吊销检查常被中间人代理/加速器要求关闭，属该场景下的预期配置")
    for key in ("http.sslbackend", "core.longpaths", "core.autocrlf"):
        if key in info["scalars"]:
            detail.append(f"{key} = {info['scalars'][key]}")
    return _doris(id_, title, "info", detail)

_EXPECTED_REPORT_VERSION = 1
# 用一个不存在的类别跑一次空报告：只读契约字段，不触发任何真实检查（毫秒级）
_ABI_PROBE_CATEGORY = "__envdoctor_abi_probe__"

def yaguruma_rine(core_version: str, has_list_checks: bool,
                  report_version) -> tuple[str, list[str], str | None]:
    """纯函数：核心自检的判定（版本可解析、清单出口存在、报告契约版本符合预期）。"""
    detail = [
        f"核心版本: {core_version or '未知'}",
        f"检查项清单出口: {'有' if has_list_checks else '无'}",
        f"报告契约版本: {report_version if report_version is not None else '未取到'}"
        f"（预期 {_EXPECTED_REPORT_VERSION}）",
    ]
    problems = []
    if not re.fullmatch(r"\d+\.\d+\.\d+", core_version or ""):
        problems.append("核心版本号无法解析")
    if not has_list_checks:
        problems.append("核心没有 envdoctor_list_checks 出口（版本过旧，或不是本项目的核心）")
    if report_version is not None and report_version != _EXPECTED_REPORT_VERSION:
        problems.append(f"报告契约版本不是 {_EXPECTED_REPORT_VERSION}")
    if problems:
        return ("warn", detail + ["问题: " + "；".join(problems)],
                "契约不一致时展示层可能读不到字段或误读状态；请确认 Python 包与核心 DLL 来自同一次构建"
                "（cd core && cargo build --release）")
    return "ok", detail, None

def yumeoi_kakeru(_cfg: dict) -> dict:
    """self.abi：Python 包与 Rust 核心的 ABI/报告契约是否对得上。

    核心缺失时记 skip 而不是 fail：CLI 在启动阶段已经给出"请先构建核心"的明确报错，
    这里再报一次只会让报告多一个假问题。
    """
    id_, title = "self.abi", "核心 ABI 自检"
    try:
        from envdoctor.binding import irys
        core = irys()
    except Exception as e:  # noqa: BLE001 —— 加载失败一律降级，不让检查本身变成故障源
        return _doris(id_, title, "skip", [f"核心不可用（{type(e).__name__}）"])
    try:
        version = core.takanashi_kiara()
    except Exception as e:  # noqa: BLE001
        return _doris(id_, title, "skip", [f"读取核心版本失败（{type(e).__name__}）"])
    report_version = None
    try:
        probe = core.gawr_gura({"categories": [_ABI_PROBE_CATEGORY], "timeout_secs": 5})
        report_version = probe.get("report_version")
    except Exception:  # noqa: BLE001
        report_version = None
    status_, detail, hint = yaguruma_rine(version, bool(getattr(core, "has_list_checks", False)),
                                         report_version)
    return _doris(id_, title, status_, detail, hint=hint)

# ---------------------------------------------------------------- 本地项目巡检（projects）
#
# 与环境类检查的分界：上面那些诊断的是**机器**（解释器/工具链/网络/注册表），
# 这一组诊断的是**工作区**——因此扫描范围必须由调用方显式给出（--scan-root），
# 不给就记 skip。全盘扫描实测不可行：一个根 25s 预算、40 万条目上限，17s 就截断且没走完。
#
# 三项检查共享同一份快照：`git status` 是逐仓库的子进程调用（实测每仓库 30~130ms），
# 各查一遍就是三倍代价。判定全部是纯函数，取数全部走可 mock 的模块级调用。

_PROJECTS_BUDGET_SECONDS = 8.0
_PROJECTS_MAX_REPOS = 200
_PROJECTS_WALK_MAX_ENTRIES = 200_000
_PROJECTS_WALK_MAX_DEPTH = 3
_PROJECTS_GIT_TIMEOUT = 6
_PROJECTS_MANIFEST_MAX_BYTES = 256 * 1024
_PROJECTS_SAMPLE = 5
# 残留 index.lock 超过这个时长才算"卡住"（阈值与"锁超过 3 分钟先杀残留进程"的处置约定一致）
_PROJECTS_LOCK_STALE_SECONDS = 180

# 不值得进入的目录：产物、依赖缓存、虚拟环境。它们既不含仓库，量又极大。
_PROJECTS_SKIP_DIRS = frozenset({
    "node_modules", "__pycache__", ".venv", "venv", "target", "dist", "build",
    ".mimosa", ".idea", ".vscode", "site-packages", ".mypy_cache", ".pytest_cache",
    "$RECYCLE.BIN", "System Volume Information",
})
# 只看仓库根的清单文件：不限层级时实测 build.gradle 会数出 137 个（走进了 vendored/生成树）
_PROJECT_MANIFESTS = ("requirements.txt", "pyproject.toml", "package.json", "go.mod", "Cargo.toml")
_PROJECT_OPERATIONS = {
    "MERGE_HEAD": "merge", "rebase-merge": "rebase", "rebase-apply": "rebase",
    "CHERRY_PICK_HEAD": "cherry-pick", "REVERT_HEAD": "revert", "BISECT_LOG": "bisect",
}

_PROJECTS_CACHE: dict = {}

def tsukimi_shizuku(roots, budget: float | None = None) -> dict:
    """限深度发现 git 仓库。

    `.git` 是目录（普通仓库）**或**文件（worktree / 子模块里它写着 `gitdir: …`）都算——只判目录
    会整批漏掉 worktree。三道闸缺一不可：深度上限、条目上限、时间预算。
    编号规则：按调用方给定的根顺序，根内按路径字典序 —— 因此编号可复现。
    """
    budget = _PROJECTS_BUDGET_SECONDS if budget is None else float(budget)
    t0 = time.perf_counter()
    found: list[Path] = []
    seen: set[str] = set()
    scanned = 0
    truncated = False
    for root in roots:
        base = Path(root)
        if not base.is_dir():
            continue
        per_root: list[Path] = []
        stack: list[tuple[Path, int]] = [(base, 0)]
        while stack:
            d, depth = stack.pop()
            try:
                with os.scandir(d) as it:
                    for e in it:
                        scanned += 1
                        if scanned > _PROJECTS_WALK_MAX_ENTRIES or time.perf_counter() - t0 > budget:
                            truncated = True
                            break
                        try:
                            is_git = e.name == ".git" and (
                                e.is_dir(follow_symlinks=False) or e.is_file(follow_symlinks=False))
                        except OSError:
                            continue
                        if is_git:
                            repo = Path(e.path).parent
                            key = str(repo).lower()
                            if key not in seen:
                                seen.add(key)
                                per_root.append(repo)
                            continue
                        try:
                            if depth >= _PROJECTS_WALK_MAX_DEPTH or e.name in _PROJECTS_SKIP_DIRS:
                                continue
                            if not e.is_dir(follow_symlinks=False):
                                continue
                        except OSError:
                            continue
                        stack.append((Path(e.path), depth + 1))
            except OSError:
                continue
            if truncated:
                break
        per_root.sort(key=lambda p: str(p).lower())
        found.extend(per_root)
        if truncated:
            break
    return {"repos": found, "scanned": scanned, "truncated": truncated,
            "seconds": time.perf_counter() - t0}

def achikita_chinami(repo, timeout: int = _PROJECTS_GIT_TIMEOUT) -> dict:
    """采集单个仓库的只读事实。**只读且不改仓库状态**：不写文件、不碰 index、不起 shell。

    HEAD 分支 / 远端 / 浅克隆 / 中途操作 / 残留锁直接读 `.git` 下的小文件，比再起几个 git
    子进程便宜得多（实测每个 git 子进程约 30~130ms）；只有"有多少改动"必须靠 `git status`。
    子进程用参数表形式（program 是字面量 `git`，路径只作为 argv 数据），不经过 shell。
    """
    repo = Path(repo)
    out: dict = {"path": repo, "branch": "", "detached": False, "remotes": 0, "shallow": False,
                 "manifests": [], "dirty": None, "operation": "", "lock_age": None, "error": ""}
    git_dir = repo / ".git"
    if git_dir.is_file():
        try:
            line = _spade_echo(git_dir.read_bytes()[:4096]).strip()
            if line.lower().startswith("gitdir:"):
                target = Path(line.split(":", 1)[1].strip())
                git_dir = target if target.is_absolute() else (repo / target)
        except OSError:
            pass
    try:
        head = _spade_echo((git_dir / "HEAD").read_bytes()[:4096]).strip()
    except OSError:
        head = ""
    if head.startswith("ref:"):
        out["branch"] = head.rsplit("/", 1)[-1].strip()
    elif len(head) >= 40:
        out["detached"] = True
    out["shallow"] = (git_dir / "shallow").is_file()
    out["operation"] = next((name for marker, name in _PROJECT_OPERATIONS.items()
                             if (git_dir / marker).exists()), "")
    try:
        out["lock_age"] = time.time() - (git_dir / "index.lock").stat().st_mtime
    except OSError:
        pass
    try:
        cfg_text = _spade_echo((git_dir / "config").read_bytes()[:_PROJECTS_MANIFEST_MAX_BYTES])
        out["remotes"] = len(re.findall(r'^\[remote\s+"', cfg_text, re.M))
    except OSError:
        pass
    try:
        with os.scandir(repo) as it:
            out["manifests"] = sorted(e.name for e in it
                                      if e.is_file(follow_symlinks=False)
                                      and e.name in _PROJECT_MANIFESTS)
    except OSError:
        pass
    try:
        # --no-optional-locks：git status 默认会顺手刷新 index（stat 缓存），那是写操作。
        # 加上它才是真正的只读诊断——不做任何需要加锁的可选动作。
        git_exe = shutil.which("git") or "git"  # 先解析绝对路径，理由见 kenmochi_toya
        r = subprocess.run([git_exe, "--no-optional-locks", "-C", str(repo),
                            "status", "--porcelain", "-z"],
                           capture_output=True, timeout=timeout,
                           creationflags=_SUBPROCESS_NO_WINDOW)
    except subprocess.TimeoutExpired:
        out["error"] = f"status 超时（>{timeout}s）"
    except OSError as e:
        out["error"] = f"无法执行 git（{type(e).__name__}）"
    else:
        if r.returncode == 0:
            # porcelain -z 里 rename/copy 条目占**两个** NUL 字段（新路径 + 原路径）：
            # 按字段数数会把一次重命名计成 2 条改动，dirty 偏大。
            fields = _spade_echo(r.stdout).split("\0")
            dirty = 0
            i = 0
            while i < len(fields):
                f = fields[i]
                if not f:
                    i += 1
                    continue
                dirty += 1
                if len(f) >= 2 and ("R" in f[:2] or "C" in f[:2]):
                    i += 1  # 跳过紧跟的原路径字段
                i += 1
            out["dirty"] = dirty
        else:
            lines = _spade_echo(r.stderr).strip().splitlines()
            out["error"] = (lines or ["git status 返回非零"])[0][:120]
    return out

def naruto_kogane(cfg: dict) -> dict:
    """项目巡检快照（一次取数、三项共享），按（扫描根, 预算）记忆化。

    `yatogami_fuma` 每轮开跑前会清掉缓存，因此界面重复运行拿到的是新数据，不会用到上一轮的。
    """
    roots = [str(r).strip() for r in (cfg.get("scan_roots") or []) if str(r).strip()]
    if not roots:
        return {"roots": [], "repos": [], "candidates": 0, "truncated": False, "seconds": 0.0}
    budget = float(cfg.get("projects_budget_secs") or _PROJECTS_BUDGET_SECONDS)
    key = (tuple(roots), round(budget, 3))
    if _PROJECTS_CACHE.get("key") == key:
        return _PROJECTS_CACHE["value"]
    t0 = time.perf_counter()
    disc = tsukimi_shizuku(roots, budget)
    deadline = t0 + budget
    repos: list[dict] = []
    truncated = disc["truncated"]
    for path in disc["repos"][:_PROJECTS_MAX_REPOS]:
        left = deadline - time.perf_counter()
        if left <= 0:
            truncated = True
            break
        # 单次调用的超时也要被剩余预算压住：否则一个超慢的仓库能再加 6s
        repos.append(achikita_chinami(path, timeout=max(1.0, min(_PROJECTS_GIT_TIMEOUT, left))))
    value = {"roots": roots, "repos": repos, "candidates": len(disc["repos"]),
             "truncated": truncated, "seconds": time.perf_counter() - t0}
    _PROJECTS_CACHE["key"] = key
    _PROJECTS_CACHE["value"] = value
    return value

def naruse_naru(repos, truncated: bool = False) -> tuple[str, list[str], str | None]:
    """纯函数：把各仓库事实映射为（状态、明细、建议）。

    只有**会挡路或会丢东西**的状态才报 warn：残留 index.lock（所有 git 命令会被拒）、
    未完成的 merge/rebase（历史停在中途）、detached HEAD（提交会游离）。未提交改动、
    无远端、浅克隆都是正常工作状态，只记 info —— 见"只在可行动时报 warn/fail"。
    """
    if not repos:
        return "info", ["未在给定扫描根下发现 git 仓库（或扫描已达预算上限）"], None
    n = len(repos)
    dirty = [i for i, r in enumerate(repos, 1) if r.get("dirty")]
    detached = [i for i, r in enumerate(repos, 1) if r.get("detached")]
    no_remote = [i for i, r in enumerate(repos, 1) if not r.get("remotes")]
    shallow = [i for i, r in enumerate(repos, 1) if r.get("shallow")]
    broken = [i for i, r in enumerate(repos, 1) if r.get("operation")]
    locked = [i for i, r in enumerate(repos, 1)
              if (r.get("lock_age") or 0) > _PROJECTS_LOCK_STALE_SECONDS]
    errored = [i for i, r in enumerate(repos, 1) if r.get("error")]

    def tag(ids):
        head = " ".join(f"#{i}" for i in ids[:_PROJECTS_SAMPLE])
        return head + (f" …（共 {len(ids)}）" if len(ids) > _PROJECTS_SAMPLE else "")

    detail = [f"仓库 {n} 个：未提交改动 {len(dirty)}、无远端 {len(no_remote)}、浅克隆 {len(shallow)}"
              + (f"、查询失败 {len(errored)}" if errored else "")]
    for label, ids in (("未完成的 git 操作", broken), ("残留 index.lock", locked),
                       ("detached HEAD", detached), ("未提交改动", dirty),
                       ("无远端", no_remote), ("浅克隆", shallow),
                       ("查询失败", errored)):
        if ids:
            detail.append(f"  {label}: {tag(ids)}")
    if truncated:
        detail.append(f"  已达预算上限，结果只覆盖前 {n} 个仓库（为下界）")
    if broken or locked or detached:
        return "warn", detail[:_PROJECTS_SAMPLE + 2], (
            "编号按扫描根顺序 + 根内路径字典序。未完成的操作与残留锁会让后续 git 命令失败或丢提交："
            "先进该仓库跑 git status 看提示；锁超过 3 分钟，确认没有 git 进程残留后再删 index.lock"
        )
    return "info", detail[:_PROJECTS_SAMPLE + 2], None

def kudo_chitose(name: str, text: str) -> list[tuple[str, str, str]]:
    """纯函数：按清单文件名解析依赖，返回 `(包名, 版本约束, 精确版本或空串)`。

    只解析**声明的**依赖，不解析传递依赖（那要联网甚至装包，违反"只读诊断不装包"）。
    解析不出来的行直接跳过——宁可不报，也不把解析残渣当依赖名回显。
    """
    deps: list[tuple[str, str, str]] = []

    def add(spec_text: str, ecosystem: str) -> None:
        m = re.match(r"^\s*([A-Za-z0-9._@/-]+)\s*(.*)$", spec_text.strip())
        if not m:
            return
        pkg, spec = m.group(1), m.group(2).strip()
        if spec.startswith("["):        # PEP 508 的 extras：uvicorn[standard]==0.30.0
            spec = spec.split("]", 1)[-1].strip() if "]" in spec else ""
        spec = spec.rstrip(",")
        if not pkg or pkg.startswith(("-", "http", "git+")):
            return
        pin = ""
        if ecosystem == "python" and re.fullmatch(r"==[0-9][\w.]*", spec):
            pin = spec[2:]
        elif ecosystem == "npm" and re.fullmatch(r"\d+\.\d+\.\d+", spec):
            pin = spec
        elif ecosystem == "go" and re.fullmatch(r"v\d+\.\d+\.\d+", spec):
            pin = spec
        deps.append((pkg, spec, pin))

    if name == "requirements.txt":
        for line in text.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line or line.startswith(("-", "http", "git+")) or "://" in line:
                continue
            add(line.split(";", 1)[0], "python")
    elif name == "package.json":
        try:
            data = json.loads(text)
        except (json.JSONDecodeError, ValueError):
            return []
        if not isinstance(data, dict):
            return []
        for key in ("dependencies", "devDependencies"):
            block = data.get(key)
            if isinstance(block, dict):
                for pkg, ver in block.items():
                    add(f"{pkg} {ver}" if isinstance(ver, str) else str(pkg), "npm")
    elif name == "go.mod":
        in_block = False
        for line in text.splitlines():
            line = line.split("//", 1)[0].strip()
            if line.startswith("require ("):
                in_block = True
                continue
            if in_block and line == ")":
                in_block = False
                continue
            if line.startswith("require "):
                add(line[len("require "):], "go")
            elif in_block:
                add(line, "go")
    elif name in ("pyproject.toml", "Cargo.toml"):
        try:
            import tomllib
        except ImportError:      # Python 3.10 没有 tomllib：跳过而不是猜
            return []
        try:
            data = tomllib.loads(text)
        except (ValueError, TypeError):
            return []
        if name == "pyproject.toml":
            project = data.get("project") or {}
            for item in list(project.get("dependencies") or []):
                if isinstance(item, str):
                    add(item, "python")
            for group in (project.get("optional-dependencies") or {}).values():
                for item in list(group or []):
                    if isinstance(item, str):
                        add(item, "python")
        else:
            for section in ("dependencies", "dev-dependencies", "build-dependencies"):
                for pkg, spec in (data.get(section) or {}).items():
                    add(f"{pkg} {spec if isinstance(spec, str) else ''}", "cargo")
    return deps

def warabeda_meiji(entries) -> dict:
    """纯函数：跨项目的依赖共存分析。

    `entries` 是可迭代的 `(仓库编号, 包名, 精确版本或空串)`。只对**精确钉死**的版本判互斥：
    范围约束之间是否相容需要真正的求解器（Cargo 的裸版本号本身也是 caret 语义），
    这里不做——宁可不报，也不报错的。`shared` 给出跨项目共用最多的包，那才是环境视角下要紧的。
    """
    pins: dict[str, dict[int, str]] = {}
    counts: dict[str, set[int]] = {}
    total = 0
    for idx, pkg, pin in entries:
        total += 1
        counts.setdefault(pkg, set()).add(idx)
        if pin:
            pins.setdefault(pkg, {}).setdefault(idx, pin)
    conflicts = [(pkg, sorted(by_repo.items())) for pkg, by_repo in pins.items()
                 if len(set(by_repo.values())) > 1]
    conflicts.sort(key=lambda kv: (-len(kv[1]), kv[0]))
    shared = sorted(((p, len(v)) for p, v in counts.items() if len(v) > 1),
                    key=lambda kv: (-kv[1], kv[0]))
    return {"conflicts": conflicts, "shared": shared, "total": total,
            "pinned": sum(len(v) for v in pins.values())}

def _projects_snapshot_or_skip(id_: str, title: str, cfg: dict):
    """三项共用：拿到快照；未配置扫描根时返回 skip 条目（未配置不是问题）。"""
    snap = naruto_kogane(cfg)
    if not snap["roots"]:
        return None, _doris(id_, title, "skip",
                            ["未指定扫描根（--scan-root 可重复；默认不扫描本地项目）"])
    return snap, None

def yuzuki_roa(cfg: dict) -> dict:
    """projects.inventory：给定扫描根下有多少 git 仓库、多少配了远端。"""
    id_, title = "projects.inventory", "本地项目清点"
    snap, skip = _projects_snapshot_or_skip(id_, title, cfg)
    if skip:
        return skip
    repos = snap["repos"]
    with_remote = sum(1 for r in repos if r.get("remotes"))
    detail = [f"扫描根 {len(snap['roots'])} 个 / 发现仓库 {len(repos)} 个",
              f"有远端 {with_remote} 个 / 无远端 {len(repos) - with_remote} 个"]
    if snap["candidates"] > len(repos):
        detail.append(f"另有 {snap['candidates'] - len(repos)} 个仓库未纳入（可达上限/预算）")
    detail.append(f"扫描耗时 {snap['seconds']:.1f}s"
                  + ("（已达上限，为下界）" if snap["truncated"] else ""))
    return _doris(id_, title, "info", detail)

def gundo_mirei(cfg: dict) -> dict:
    """projects.health：仓库是否卡在中途状态（残留锁 / 未完成操作 / detached HEAD）。"""
    id_, title = "projects.health", "项目仓库健康度"
    snap, skip = _projects_snapshot_or_skip(id_, title, cfg)
    if skip:
        return skip
    status_, detail, hint = naruse_naru(snap["repos"], snap["truncated"])
    return _doris(id_, title, status_, detail, hint=hint)

def onomachi_haruka(cfg: dict) -> dict:
    """projects.deps：仓库根清单里的依赖，以及跨项目的精确版本互斥。

    报告里**不出现项目名与路径**：仓库一律用编号（编号规则见 projects.health 的建议）。
    依赖名回显（那是可行动信息），远端 URL 从不进入报告。
    """
    id_, title = "projects.deps", "跨项目依赖"
    snap, skip = _projects_snapshot_or_skip(id_, title, cfg)
    if skip:
        return skip
    entries: list[tuple[int, str, str]] = []
    manifests = 0
    unreadable = 0
    for i, r in enumerate(snap["repos"], 1):
        for mname in r.get("manifests") or []:
            try:
                raw = (Path(r["path"]) / mname).read_bytes()[:_PROJECTS_MANIFEST_MAX_BYTES]
            except OSError:
                unreadable += 1
                continue
            deps = kudo_chitose(mname, _spade_echo(raw))
            if deps:
                manifests += 1
            entries += [(i, pkg, pin) for pkg, _spec, pin in deps]
    if not manifests:
        return _doris(id_, title, "info",
                      [f"仓库 {len(snap['repos'])} 个，均无受支持的根清单文件"
                       "（requirements.txt / pyproject.toml / package.json / go.mod / Cargo.toml）"])
    info = warabeda_meiji(entries)
    detail = [f"已解析清单 {manifests} 份 / 依赖条目 {info['total']} 条"
              + (f"（{unreadable} 份读取失败）" if unreadable else "")]
    if info["shared"]:
        detail.append("跨项目共用最多: "
                      + "、".join(f"{p}（{n} 个项目）" for p, n in info["shared"][:6]))
    if info["conflicts"]:
        detail.append(f"精确版本互斥 {len(info['conflicts'])} 项（同一时间只可能装一个版本）:")
        for pkg, items in info["conflicts"][:_PROJECTS_SAMPLE]:
            detail.append("  " + pkg + ": " + "、".join(f"#{i}={v}" for i, v in items))
        return _doris(
            id_, title, "warn", detail[:_PROJECTS_SAMPLE + 2],
            hint="互斥的包在不同项目里钉了不同版本：切换项目时容易装错。"
                 "要么统一版本，要么让每个项目用独立 venv / node_modules 隔离",
        )
    detail.append(f"未发现精确版本互斥（精确钉版本 {info['pinned']} 条）")
    return _doris(id_, title, "info", detail)

# ---------------------------------------------------------------- 注册与运行

_PY_CHECKS = [
    ("python.interpreter", "解释器", artia),
    ("python.multiplicity", "Python 多版本共存", kanade_izuru),
    ("python.pip", "pip", hanasaki_miyabi),
    ("python.venv", "虚拟环境", rikka),
    ("python.path", "模块搜索路径", arurandeisu),
    ("python.packages", "已安装包", kagami_kira),
    ("python.outdated", "过时包", yakushiji_suzaku),
    ("python.mirror", "包镜像源", astel_leda),
    ("python.store_alias", "Windows Store 别名", kishido_temma),
    ("python.gil", "GIL", aragami_oga),
    ("python.env_vars", "相关环境变量", kageyama_shien),
    ("python.permissions", "安装目录权限", gavis_bettel),
    ("python.ssl", "证书与 TLS", jurard_t_rexford),
    ("python.startup", "解释器启动", moira),
    ("python.pip_env", "pip 环境", elu),
    ("python.libs", "常用库", yuki_chihiro),
    ("python.packaging", "打包工具", suzuya_aki),
    ("python.cache_size", "字节码缓存", ienaga_mugi),
    ("env.codepage", "控制台编码", axel_syrios),
    ("hardware.cpu", "CPU", goldbullet),
    ("network.hosts", "hosts 解析", josuiji_shinri),
    ("toolchains.git_identity", "Git 身份", kenmochi_toya),
    ("toolchains.ssh_keys", "SSH 密钥", fushimi_gaku),
    ("python.venv_integrity", "虚拟环境完整性", yashiro_kizuku),
    ("python.shadowing", "模块遮蔽", umiyashano_kami),
    ("python.pth_files", ".pth 路径注入", izumo_kasumi),
    ("python.pip_check", "依赖冲突", harusaki_air),
    ("env.temp_path", "临时目录路径", asuka_hina),
    ("toolchains.java_home", "JAVA_HOME 一致性", machita_chima),
    ("toolchains.git_config", "Git 关键配置", belmond_banderas),
    ("self.abi", "核心 ABI 自检", yumeoi_kakeru),
    ("projects.inventory", "本地项目清点", yuzuki_roa),
    ("projects.health", "项目仓库健康度", gundo_mirei),
    ("projects.deps", "跨项目依赖", onomachi_haruka),
]

def tsukishita_kaoru() -> list[dict]:
    """供 GUI/CLI 列出全部 Python 检查（含动态导入项）。类别由 id 前缀推导。"""
    defs = [{"id": i, "title": t, "category": tsukino_mito(i)} for i, t, _ in _PY_CHECKS]
    for lib in _IMPORT_LIBS:
        if importlib.util.find_spec(lib) is not None:
            defs.append({"id": f"python.import.{lib}", "title": f"导入 · {lib}",
                         "category": PYTHON_CATEGORY})
    return defs

def yatogami_fuma(
    cfg: dict,
    progress=None,
    done_offset: int = 0,
    total: int = 0,
    categories: list[str] | None = None,
) -> list[dict]:
    """顺序执行 Python 侧检查（Rust 侧已并发跑完系统类检查）。

    类别过滤按**每项自己的类别**（id 前缀）判定：Python 侧不再只产出 python 类，
    `env.*` / `hardware.cpu` 等也在这里实现，所以不能用"categories 不含 python 就整体跳过"。
    """
    # 项目巡检快照按（扫描根, 预算）记忆化，是为了让三项共享同一次扫描；
    # 每轮开跑前清掉，界面里连点两次"运行"才不会拿到上一轮的仓库状态。
    _PROJECTS_CACHE.clear()
    results: list[dict] = []
    jobs = [(i, t, f) for i, t, f in _PY_CHECKS]
    for lib in _IMPORT_LIBS:
        if importlib.util.find_spec(lib) is not None:
            jobs.append((f"python.import.{lib}", f"导入 · {lib}", _yukoku_roberu(lib)))
    if categories is not None:
        wanted = set(categories)
        jobs = [j for j in jobs if tsukino_mito(j[0]) in wanted]
        if not jobs:
            return []
    for n, (id_, title, fn) in enumerate(jobs, 1):
        t0 = time.perf_counter()
        try:
            r = fn(cfg)
        except subprocess.TimeoutExpired:
            budget = int(cfg.get("timeout_secs", 25))
            r = _doris(id_, title, "timeout", [f"检测超时（>{budget}s）"])
        except Exception as e:  # noqa: BLE001 —— 检查函数不允许拖垮整轮诊断
            r = _doris(id_, title, "fail", [f"{type(e).__name__}: {e}"], error=str(e))
        r["duration_ms"] = round((time.perf_counter() - t0) * 1000, 2)
        results.append(r)
        if progress is not None:
            progress(done_offset + n, total, id_)
    return results
