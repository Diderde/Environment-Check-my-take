# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""合并 Rust 报告与 Python 检查结果为统一报告。"""

from __future__ import annotations

import time


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
