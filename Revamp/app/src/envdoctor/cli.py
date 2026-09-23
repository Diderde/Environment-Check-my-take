# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Typer CLI：默认折叠为分类摘要，--expand/-E/--expand-all 展开明细。"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import typer

from envdoctor.binding import Core, CoreNotAvailable, get_core
from envdoctor.merge import merge_reports, verdict
from envdoctor import pychecks

app = typer.Typer(add_completion=False, no_args_is_help=True,
                  help="全栈开发环境诊断（Rust 核心 + Python 界面层）")

CATEGORIES = ["hardware", "toolchains", "network", "containers", "databases", "python"]

_STATUS_ICON = {"ok": "✅", "warn": "⚠️", "fail": "❌", "skip": "⏭️", "info": "ℹ️", "timeout": "⏱️"}
_COLOR = {"ok": "32", "warn": "33", "fail": "31", "skip": "90", "info": "36", "timeout": "33"}


class _Palette:
    def __init__(self, enabled: bool):
        self.enabled = enabled
        if enabled and sys.platform == "win32":
            os.system("")  # 激活 Windows 控制台的 ANSI 支持

    def paint(self, text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.enabled else text

    def status(self, status: str, icon: bool = False) -> str:
        text = _STATUS_ICON.get(status, "·") if icon else status
        return self.paint(text, _COLOR.get(status, "0"))


def _load_core(core_path: Path | None) -> Core:
    try:
        return Core(path=core_path) if core_path else get_core()
    except CoreNotAvailable as e:
        typer.secho(str(e), fg=typer.colors.RED, err=True)
        typer.echo("请先构建 Rust 核心：cargo build --release（在 Revamp/core 下）")
        raise typer.Exit(code=2) from e


def _print_report(report: dict, pal: _Palette, expanded: set[str], expand_all: bool) -> None:
    by_cat: dict[str, list[dict]] = {}
    for r in report["results"]:
        by_cat.setdefault(r["category"], []).append(r)

    for cat in CATEGORIES:
        rows = by_cat.get(cat)
        if not rows:
            continue
        counts: dict[str, int] = {}
        for r in rows:
            counts[r["status"]] = counts.get(r["status"], 0) + 1
        head = " ".join(
            f"{pal.status(s, icon=True)}{n}" for s, n in counts.items()
        )
        typer.echo(pal.paint(f"▸ {cat}", "1") + f"  {head}")
        if expand_all or cat in expanded:
            for r in rows:
                line = (
                    f"  {pal.status(r['status'], icon=True)} "
                    f"[{pal.paint(r['id'], '90')}] {r['title']}"
                    f"  ({r['duration_ms']:.0f}ms)"
                )
                typer.echo(line)
                for d in r.get("detail", []):
                    typer.echo(f"      · {d}")
                if r.get("hint"):
                    typer.echo(pal.paint(f"      ↳ 建议: {r['hint']}", "33"))

    verdict_state, problems = verdict(report)
    typer.echo("")
    typer.echo(pal.paint("════ 诊断结论 ════", "1"))
    counts = report["summary"]["counts"]
    typer.echo("统计: " + "  ".join(f"{pal.status(s, icon=True)}{n}" for s, n in counts.items()))
    if verdict_state == "good":
        typer.echo(pal.paint("✅ 未发现需要处理的问题", "32"))
    elif verdict_state == "error":
        typer.echo(pal.paint(f"❌ 诊断引擎异常: {report.get('error')}", "31"))
    else:
        typer.echo(pal.paint(f"发现 {len(problems)} 个需要关注的问题:", "1"))
        for n, r in enumerate(problems, 1):
            typer.echo(f"  {n}. {pal.status(r['status'], icon=True)} [{r['id']}] {r['title']}")
            if r.get("hint"):
                typer.echo(pal.paint(f"     ↳ {r['hint']}", "33"))


@app.callback(invoke_without_command=True)
def _default(
    ctx: typer.Context,
    version: bool = typer.Option(False, "--version", help="显示版本"),
    list_checks: bool = typer.Option(False, "--list-checks", help="列出全部检查项"),
):
    if version:
        core = _load_core(None)
        typer.echo(f"envdoctor {__import__('envdoctor').__version__} / core {core.version()}")
        raise typer.Exit()
    if list_checks:
        for d in pychecks.py_check_defs():
            typer.echo(f"{d['category']:12} {d['id']:30} {d['title']}")
        raise typer.Exit()
    if ctx.invoked_subcommand is None:
        ctx.invoke(run)


@app.command()
def run(
    category: list[str] = typer.Option([], "--category", "-c", help="只跑指定类别（可多次）"),
    expand: list[str] = typer.Option([], "--expand", "-e", help="展开指定类别明细（默认折叠）"),
    expand_all: bool = typer.Option(False, "--expand-all", "-E", help="展开全部明细"),
    require: list[str] = typer.Option([], "--require", help="必备工具：缺失记 FAIL（可多次）"),
    net_full: bool = typer.Option(False, "--net-full", help="启用隐私外发检查（公网 IP）"),
    timeout: int = typer.Option(25, "--timeout", help="单项检测预算（秒）"),
    json_out: Path = typer.Option(None, "--json", help="导出 JSON 报告到该路径"),
    txt_out: Path = typer.Option(None, "--txt", help="导出 Markdown 报告到该路径"),
    no_color: bool = typer.Option(False, "--no-color"),
    core: Path = typer.Option(None, "--core", envvar="ENVDOCTOR_CORE_PATH", help="核心 DLL 路径"),
):
    """运行完整诊断。默认折叠为分类摘要；未指定的类别按检测结果出现顺序追加。"""
    pal = _Palette(enabled=not no_color and sys.stdout.isatty())
    core_obj = _load_core(core)

    cfg = {
        "categories": category or None,
        "required": require or None,
        "net_full": net_full,
        "timeout_secs": timeout,
    }
    if category:
        allowed = set(category)
    else:
        allowed = None

    is_tty = sys.stdout.isatty()

    def on_progress(done: int, total_n: int, current: str) -> None:
        # 单行覆盖式进度（GBK 控制台安全，纯 ASCII + 中文）
        sys.stdout.write(f"\r[进度] {done}/{total_n}  {current}   ")
        sys.stdout.flush()

    typer.echo(pal.paint("正在运行诊断（系统类检查由 Rust 核心并发执行）…", "90"))
    token = core_obj.new_cancel_token()
    try:
        rust_report = core_obj.run(
            cfg, progress=on_progress if is_tty else None, cancel=token
        )
        want_py = not category or "python" in category
        py_total = len(pychecks.py_check_defs()) if want_py else 0
        py_cfg = {"timeout_secs": timeout}
        rust_n = len(rust_report.get("results", []))
        py_results = pychecks.run_python_checks(
            py_cfg,
            categories=category or None,
            progress=on_progress if is_tty and py_total else None,
            done_offset=rust_n,
            total=rust_n + py_total,
        )
    finally:
        token.close()
    if is_tty:
        sys.stdout.write("\r" + " " * 70 + "\r")
        sys.stdout.flush()

    # 未选择的类别不展示
    report = merge_reports(rust_report, py_results)
    if allowed is not None:
        report["results"] = [r for r in report["results"] if r["category"] in allowed]
        report["summary"] = {"counts": {}, "problems": []}
        for r in report["results"]:
            report["summary"]["counts"][r["status"]] = report["summary"]["counts"].get(r["status"], 0) + 1
            if r["status"] in ("warn", "fail"):
                report["summary"]["problems"].append(f"[{r['id']}] {r['title']}")

    _print_report(report, pal, set(expand), expand_all)

    if json_out:
        json_out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        typer.echo(f"JSON 报告已写入: {json_out}")
    if txt_out:
        lines = ["# 环境诊断报告", "", f"- 平台: {report['platform']}", f"- 耗时: {report['duration_ms']:.0f}ms", ""]
        for r in report["results"]:
            lines.append(f"## [{r['status']}] {r['title']} (`{r['id']}`)")
            lines += [f"- {d}" for d in r.get("detail", [])]
            if r.get("hint"):
                lines.append(f"- 建议: {r['hint']}")
            lines.append("")
        txt_out.write_text("\n".join(lines), encoding="utf-8")
        typer.echo(f"Markdown 报告已写入: {txt_out}")

    if any(r["status"] == "fail" for r in report["results"]):
        raise typer.Exit(code=1)


@app.command()
def gui() -> None:
    """启动 PySide6 图形界面。"""
    from envdoctor.gui import main as gui_main

    gui_main()


@app.command()
def tui() -> None:
    """启动 Textual 终端界面（可展开收缩诊断树）。"""
    from envdoctor.tui import main as tui_main

    tui_main()


def main() -> None:
    app()


if __name__ == "__main__":
    main()
