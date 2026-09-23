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
from pathlib import Path

_PROGRESS_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p)

# Rust 侧 panic 时让 stderr 带出完整调用栈（panic 被 catch_unwind 转为报告 error 字段，栈信息仅辅助调试）
os.environ.setdefault("RUST_BACKTRACE", "1")


class CoreNotAvailable(RuntimeError):
    """核心 DLL 缺失或加载失败。"""


def _dll_candidates() -> list[Path]:
    out: list[Path] = []
    env = os.environ.get("ENVDOCTOR_CORE_PATH")
    if env:
        out.append(Path(env))
    here = Path(__file__).resolve()
    # app/src/envdoctor/binding.py → parents: [envdoctor, src, app, Revamp]
    if len(here.parents) > 3:
        out.append(
            here.parents[3] / "core" / "target" / "release" / "envdoctor_core.dll"
        )
    return out


class CancelToken:
    """取消令牌：触发后引擎不再派发剩余检查，未完成项记 SKIP。"""

    def __init__(self, core: "Core"):
        self._core = core
        self._ptr = core._lib.envdoctor_cancel_new()

    def trigger(self) -> None:
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


class Core:
    """已加载的 Rust 核心实例（线程安全：仅封装 C 调用）。"""

    def __init__(self, path: str | os.PathLike | None = None):
        candidates = [Path(path)] if path else _dll_candidates()
        lib = None
        last_err: Exception | None = None
        for cand in candidates:
            if cand.is_file():
                try:
                    lib = ctypes.CDLL(str(cand))
                    self.path = cand
                    break
                except OSError as e:
                    last_err = e
        if lib is None:
            raise CoreNotAvailable(
                f"找不到 envdoctor_core.dll（先执行 cargo build --release）。"
                f"候选路径: {[str(c) for c in candidates]}；最后错误: {last_err}"
            )
        self._lib = lib
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

    def version(self) -> str:
        return (self._lib.envdoctor_core_version() or b"?").decode()

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


def get_core() -> Core:
    """进程级单例加载。"""
    global _cached
    if _cached is None:
        _cached = Core()
    return _cached
