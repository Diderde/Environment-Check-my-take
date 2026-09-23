# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""合并 Rust 报告与 Python 检查结果为统一报告，并提供报告中立化规则。"""

from __future__ import annotations

import time

__all__ = ["merge_reports", "redact_url", "verdict"]


def redact_url(value: str) -> str:
    """抹掉 URL 里的凭据部分（`scheme://user:pass@host` → `scheme://***@host`）。

    与 Rust 侧 `network.rs::mask_credentials` 保持同一规则：按**最后一个** `@` 切分
    （口令里带 `@` 的情况很常见，按第一个切会把口令尾巴泄进 host），非 URL 形态原样返回。
    报告会被导出成本地文件或粘贴进 issue，代理地址与私有 index-url 里的
    `user:token@` 不能明文落进报告。
    """
    scheme, sep, rest = value.partition("://")
    if not sep:
        return value
    _creds, at, host = rest.rpartition("@")
    if not at:
        return value
    return f"{scheme}://***@{host}"


def merge_reports(rust_report: dict, python_results: list[dict]) -> dict:
    results = list(rust_report.get("results", [])) + list(python_results)
    results.sort(key=lambda r: (r.get("category", ""), r.get("id", "")))

    counts: dict[str, int] = {}
    problems: list[str] = []
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
        if r["status"] in ("warn", "fail"):
            problems.append(f"[{r['id']}] {r['title']}")

    return {
        "report_version": 1,
        "source": "merged(rust+python)",
        "platform": rust_report.get("platform", ""),
        "generated_at_unix": int(time.time()),
        "duration_ms": round(rust_report.get("duration_ms") or 0.0, 2),
        "error": rust_report.get("error"),
        "results": results,
        "summary": {"counts": counts, "problems": problems},
    }


def verdict(report: dict) -> tuple[str, list[dict]]:
    """生成诊断结论（旧版"诊断分析"死分支的真正实现）。"""
    problems = [r for r in report["results"] if r["status"] in ("warn", "fail")]
    if report.get("error"):
        return "error", problems
    if not problems:
        return "good", []
    return "issues", problems
