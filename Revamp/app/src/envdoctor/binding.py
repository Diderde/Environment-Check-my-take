# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Rust 核心（envdoctor_core.dll）的 ctypes 绑定。

DLL 查找顺序：
1. 环境变量 ENVDOCTOR_CORE_PATH；
2. 仓库内相对路径 Revamp/core/target/release/envdoctor_core.dll（开发模式）。

GIL/线程约定：
- `CDLL` 调用期间 ctypes 自动释放 GIL——Rust 引擎并发跑检查时 GUI 主线程不阻塞；
- 进度回调由 ctypes 在引擎工作线程上调用，ctypes 回调进入 Python 前自动获取 GIL；
  GUI/TUI 侧拿到回调后需再经 Qt 信号 / call_from_thread 切回界面线程。
"""

from __future__ import annotations

import ctypes
import json
import os
import threading
from pathlib import Path

_PROGRESS_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p)

# Rust 侧 panic 时让 stderr 带出完整调用栈（panic 被 catch_unwind 转为报告 error 字段，栈信息仅辅助调试）
os.environ.setdefault("RUST_BACKTRACE", "1")


class CoreNotAvailable(RuntimeError):
    """核心 DLL 缺失或加载失败。"""


def _dll_candidates() -> list[Path]:
    """核心动态库候选路径，按优先级排列。

    1. 环境变量 `ENVDOCTOR_CORE_PATH`；
    2. 包内 `envdoctor/_native/`（wheel 安装形态：把库随包分发）；
    3. 仓库内 `Revamp/core/target/release/`（editable/开发形态）。

    同时覆盖 `.dll` / `.so` / `.dylib`：旧版只找 `.dll`，非 Windows 上永远加载不到核心。
    """
    names = ("envdoctor_core.dll", "libenvdoctor_core.so", "libenvdoctor_core.dylib")
    out: list[Path] = []
    env = os.environ.get("ENVDOCTOR_CORE_PATH")
    if env:
        out.append(Path(env))
    pkg = Path(__file__).resolve().parent
    for name in names:
        out.append(pkg / "_native" / name)
    # app/src/envdoctor/binding.py → 包的上级依次为 src / app / Revamp
    if len(pkg.parents) > 2:
        release = pkg.parents[2] / "core" / "target" / "release"
        for name in names:
            out.append(release / name)
    return out


class CancelToken:
    """取消令牌：触发后引擎不再派发剩余检查，未完成项记 SKIP。

    生命周期契约（比"配对"更强，务必遵守）：**令牌必须活到 `Core.run` 返回之后**才能
    `close()` —— 引擎在整个运行期间持有它的引用，运行中释放即 use-after-free。
    正确姿势：`trigger()` 随时可调（可在其它线程），`close()` 放到 run 返回之后。
    """

    def __init__(self, core: "Core"):
        self._core = core
        self._ptr = core._lib.envdoctor_cancel_new()

    def trigger(self) -> None:
        # close() 之后 _ptr 为 None：Rust 侧对空指针是 no-op，重复触发安全
        self._core._lib.envdoctor_cancel_trigger(self._ptr)

    def close(self) -> None:
        if getattr(self, "_ptr", None):
            self._core._lib.envdoctor_cancel_free(self._ptr)
            self._ptr = None

    def __del__(self):  # noqa: D105
        try:
            self.close()
        except Exception:
            pass


def _bind(lib: ctypes.CDLL) -> None:
    """声明核心导出函数的签名（缺符号会抛 AttributeError，由调用方转成 CoreNotAvailable）。"""
    lib.envdoctor_core_version.restype = ctypes.c_char_p
    lib.envdoctor_run.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]
    lib.envdoctor_run.restype = ctypes.c_void_p
    lib.envdoctor_string_free.argtypes = [ctypes.c_void_p]
    lib.envdoctor_string_free.restype = None
    lib.envdoctor_cancel_new.restype = ctypes.c_void_p
    lib.envdoctor_cancel_trigger.argtypes = [ctypes.c_void_p]
    lib.envdoctor_cancel_trigger.restype = None
    lib.envdoctor_cancel_free.argtypes = [ctypes.c_void_p]
    lib.envdoctor_cancel_free.restype = None
    # 可选出口：老版本核心没有它，缺失时降级为"只列 Python 检查"
    try:
        lib.envdoctor_list_checks.restype = ctypes.c_void_p
    except AttributeError:
        pass


class Core:
    """已加载的 Rust 核心实例（线程安全：仅封装 C 调用）。"""

    def __init__(self, path: str | os.PathLike | None = None):
        candidates = [Path(path)] if path else _dll_candidates()
        lib = None
        problems: list[str] = []
        for cand in candidates:
            if not cand.is_file():
                problems.append(f"{cand}: 文件不存在")
                continue
            try:
                lib = ctypes.CDLL(str(cand))
            except OSError as e:
                problems.append(f"{cand}: 加载失败（{e}）")
                continue
            try:
                _bind(lib)
            except AttributeError as e:
                # DLL 能加载但不是我们的核心（或版本过旧缺符号）：以前会漏成裸 traceback
                problems.append(f"{cand}: 缺少导出符号（{e}）")
                lib = None
                continue
            self.path = cand
            self.has_list_checks = hasattr(lib, "envdoctor_list_checks")
            break
        if lib is None:
            raise CoreNotAvailable(
                "找不到可用的 envdoctor_core 动态库"
                "（先执行 cargo build --release，或用 ENVDOCTOR_CORE_PATH 指定）。"
                f"候选路径: {[str(c) for c in candidates]}；探测结果: {problems}"
            )
        self._lib = lib

    def version(self) -> str:
        return (self._lib.envdoctor_core_version() or b"?").decode()

    def list_checks(self) -> list[dict]:
        """列出核心侧全部检查项元数据；核心过旧（无该出口）时返回空列表。"""
        if not getattr(self, "has_list_checks", False):
            return []
        ptr = self._lib.envdoctor_list_checks()
        if not ptr:
            raise RuntimeError("envdoctor_list_checks 返回空指针")
        try:
            return json.loads(ctypes.string_at(ptr))
        finally:
            self._lib.envdoctor_string_free(ptr)

    def new_cancel_token(self) -> CancelToken:
        return CancelToken(self)

    def run(
        self,
        config: dict | None = None,
        progress=None,
        cancel: CancelToken | None = None,
    ) -> dict:
        """运行诊断，返回报告 dict。progress(done, total, current_id) 在引擎线程上回调。"""
        cfg = json.dumps(config or {}).encode("utf-8")
        cb = None
        if progress is not None:
            cb = _PROGRESS_CB(
                lambda done, total, current: progress(done, total, current.decode("utf-8", "replace"))
            )
        progress_arg = cb if cb is not None else ctypes.cast(None, _PROGRESS_CB)
        ptr = self._lib.envdoctor_run(cfg, progress_arg, cancel._ptr if cancel else None)
        if not ptr:
            raise RuntimeError("envdoctor_run 返回空指针")
        try:
            raw = ctypes.string_at(ptr)
            return json.loads(raw)
        finally:
            self._lib.envdoctor_string_free(ptr)


_cached: Core | None = None
_cache_lock = threading.Lock()


def get_core() -> Core:
    """进程级单例加载（加锁：并发首调不会重复加载同一个 DLL）。"""
    global _cached
    with _cache_lock:
        if _cached is None:
            _cached = Core()
        return _cached
