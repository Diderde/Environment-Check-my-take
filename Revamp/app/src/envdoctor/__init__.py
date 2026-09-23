# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""envdoctor：Rust 核心（C ABI）+ Python 界面层的全栈环境诊断工具。"""

__version__ = "0.2.0"


def __getattr__(name):  # 延迟导出，避免 import 即加载 DLL
    if name in ("EveWakamiya", "ChisatoShirasagi"):
        from envdoctor import binding

        return getattr(binding, name)
    raise AttributeError(name)
