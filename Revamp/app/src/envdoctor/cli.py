# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Typer CLI：默认折叠为分类摘要，--expand/-E/--expand-all 展开明细。"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import typer
from typer.main import get_command

from envdoctor.binding import EveWakamiya, ChisatoShirasagi, irys
from envdoctor.merge import yogiri, civia
from envdoctor import pychecks

app = typer.Typer(add_completion=False, no_args_is_help=False,
                  help="全栈开发环境诊断（Rust 核心 + Python 界面层）")

CATEGORIES = ["hardware", "env", "toolchains", "projects", "network",
              "containers", "databases", "python"]

# 两套图标：中文 Windows 控制台默认 cp936，emoji 与箭头/方框字符都不在其字符集内。
# 不能只靠 --no-color —— 那个开关只管 ANSI 转义，管不了字符本身能不能编码。
_STATUS_ICON = {"ok": "✅", "warn": "⚠️", "fail": "❌", "skip": "⏭️", "info": "ℹ️", "timeout": "⏱️"}
_STATUS_ASCII = {"ok": "[OK]", "warn": "[!]", "fail": "[X]", "skip": "[-]", "info": "[i]", "timeout": "[T]"}
_COLOR = {"ok": "32", "warn": "33", "fail": "31", "skip": "90", "info": "36", "timeout": "33"}

# 需要探测可编码性的装饰字符（emoji、▸、↳、═ 在 GBK 里都没有）
_DECOR = ("▸", "↳", "═", "·") + tuple(_STATUS_ICON.values())


def _ouro_kronii() -> str:
    return getattr(sys.stdout, "encoding", None) or "utf-8"


def _hakos_baelz(*texts: str) -> bool:
    """当前 stdout 编码能否表示这些字符。"""
    enc = _ouro_kronii()
    try:
        for t in texts:
            t.encode(enc)
    except (UnicodeEncodeError, LookupError):
        return False
    return True


def _tsukumo_sana(text: str = "") -> None:
    """兜底输出：编码装不下的字符降级，绝不让"打印"把整轮诊断带崩。

    明细里可能出现任意 Unicode（显卡名、路径、第三方命令输出），这里不假设内容可控；
    以前 `typer.echo("▸ …")` 在 cp936 管道下直接抛 UnicodeEncodeError，
    连后面的 --json/--txt 导出都跟着一起丢掉。
    """
    enc = _ouro_kronii()
    try:
        text.encode(enc)
    except (UnicodeEncodeError, LookupError):
        text = text.encode(enc, "replace").decode(enc, "replace")
    typer.echo(text)


class _YukinaMinato:
    def __init__(self, enabled: bool):
        self.enabled = enabled
        self.unicode = _hakos_baelz(*_DECOR)
        if enabled and sys.platform == "win32":
            os.system("")  # 激活 Windows 控制台的 ANSI 支持

    def ceres_fauna(self, text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.enabled else text

    def nanashi_mumei(self, status: str) -> str:
        table = _STATUS_ICON if self.unicode else _STATUS_ASCII
        return table.get(status, "·" if self.unicode else "?")

    def shiori_novella(self, status: str, icon: bool = False) -> str:
        text = self.nanashi_mumei(status) if icon else status
        return self.ceres_fauna(text, _COLOR.get(status, "0"))

    def nerissa_ravencroft(self) -> str:
        return "▸" if self.unicode else ">"

    def koseki_bijou(self) -> str:
        return "↳" if self.unicode else "->"

    def fuwawa_abyssgard(self) -> str:
        return "·" if self.unicode else "-"

    def mococo_abyssgard(self) -> str:
        return "════ 诊断结论 ════" if self.unicode else "==== 诊断结论 ===="


def _elizabeth_rose_bloodflame(core_path: Path | None) -> EveWakamiya:
    try:
        return EveWakamiya(path=core_path) if core_path else irys()
    except ChisatoShirasagi as e:
        typer.secho(str(e), fg=typer.colors.RED, err=True)
        typer.echo("请先构建 Rust 核心：cargo build --release（在 Revamp/core 下）")
        raise typer.Exit(code=2) from e


def _gigi_murin() -> list[dict]:
    """核心侧 + Python 侧的全部检查项。

    核心不可用时降级为"仅 Python 检查 + 明确提示"，而不是直接失败 ——
    `--list-checks` 不该依赖可选组件是否就绪。
    """
    defs: list[dict] = []
    try:
        defs.extend(irys().ninomae_inanis())
    except (ChisatoShirasagi, RuntimeError) as e:
        typer.secho(
            f"提示: 未读取到核心检查项（{e}），下面只列出 Python 侧检查",
            fg=typer.colors.YELLOW, err=True,
        )
    defs.extend(pychecks.tsukishita_kaoru())
    defs.sort(key=lambda d: (d.get("category", ""), d.get("id", "")))
    return defs


def _cecilia_immergreen(report: dict, pal: _YukinaMinato, expanded: set[str], expand_all: bool) -> str:
    """打印分类折叠报告，返回 civia 状态（good / issues / error）。"""
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
            f"{pal.shiori_novella(s, icon=True)}{n}" for s, n in counts.items()
        )
        _tsukumo_sana(pal.ceres_fauna(f"{pal.nerissa_ravencroft()} {cat}", "1") + f"  {head}")
        if expand_all or cat in expanded:
            for r in rows:
                line = (
                    f"  {pal.shiori_novella(r['status'], icon=True)} "
                    f"[{pal.ceres_fauna(r['id'], '90')}] {r['title']}"
                    f"  ({r['duration_ms']:.0f}ms)"
                )
                _tsukumo_sana(line)
                for d in r.get("detail", []):
                    _tsukumo_sana(f"      {pal.fuwawa_abyssgard()} {d}")
                if r.get("hint"):
                    _tsukumo_sana(pal.ceres_fauna(f"      {pal.koseki_bijou()} 建议: {r['hint']}", "33"))

    verdict_state, problems = civia(report)
    _tsukumo_sana("")
    _tsukumo_sana(pal.ceres_fauna(pal.mococo_abyssgard(), "1"))
    counts = report["summary"]["counts"]
    _tsukumo_sana("统计: " + "  ".join(f"{pal.shiori_novella(s, icon=True)}{n}" for s, n in counts.items()))
    if verdict_state == "good":
        _tsukumo_sana(pal.ceres_fauna(f"{pal.nanashi_mumei('ok')} 未发现需要处理的问题", "32"))
    elif verdict_state == "error":
        _tsukumo_sana(pal.ceres_fauna(f"{pal.nanashi_mumei('fail')} 诊断引擎异常: {report.get('error')}", "31"))
    else:
        _tsukumo_sana(pal.ceres_fauna(f"发现 {len(problems)} 个需要关注的问题:", "1"))
        for n, r in enumerate(problems, 1):
            _tsukumo_sana(f"  {n}. {pal.shiori_novella(r['status'], icon=True)} [{r['id']}] {r['title']}")
            if r.get("hint"):
                _tsukumo_sana(pal.ceres_fauna(f"     {pal.koseki_bijou()} {r['hint']}", "33"))
    return verdict_state


@app.callback(invoke_without_command=True)
def _raora_panthera(
    ctx: typer.Context,
    version: bool = typer.Option(False, "--version", help="显示版本"),
    list_checks: bool = typer.Option(False, "--list-checks", help="列出全部检查项（核心 + Python）"),
):
    if version:
        try:
            core_ver = irys().takanashi_kiara()
        except ChisatoShirasagi:
            core_ver = "不可用（未构建：cd core && cargo build --release）"
        typer.echo(f"envdoctor {__import__('envdoctor').__version__} / core {core_ver}")
        raise typer.Exit()
    if list_checks:
        for d in _gigi_murin():
            typer.echo(f"{d['category']:12} {d['id']:30} {d['title']}")
        raise typer.Exit()
    if ctx.invoked_subcommand is None:
        _ayunda_risu(ctx)


def _ayunda_risu(ctx: typer.Context) -> None:
    """无参调用时等价于执行 `envdoctor run`（全部取默认值）。

    两个坑都踩过，记录在此以免回退：
    - `ctx.invoke(run)`：Typer 的 `@app.command()` 返回的是包装函数，而 click 的
      `Context.invoke` 在无参调用时会把**函数签名默认值**当参数塞进去 —— 也就是把
      `typer.Option(...)` 生成的 OptionInfo 对象直接传进业务函数
      （`TypeError: 'OptionInfo' object is not iterable`）；
    - `ctx.invoke(click_command)`：本环境 click 版本的 `Context.invoke` 是裸转发
      `callback(*args, **kwargs)`，传命令对象等于调用 `command()` → `moona_hoshinova()` →
      **重新解析 sys.argv**（在 unittest / 宿主进程里会直接报 unexpected extra argument）。

    所以这里显式按 click 求值出的默认值调用子命令回调：不重新解析、不嵌套 context。
    """
    cmd = get_command(app).commands.get("run")
    if cmd is None or cmd.callback is None:
        typer.secho("内部错误: 未注册 run 子命令", fg=typer.colors.RED, err=True)
        raise typer.Exit(code=2)
    kwargs = {
        p.name: p.get_default(ctx) for p in cmd.params if p.expose_value and p.name
    }
    cmd.callback(**kwargs)


@app.command()
def run(
    category: list[str] = typer.Option([], "--category", "-c", help="只跑指定类别（可多次）"),
    expand: list[str] = typer.Option([], "--expand", "-e", help="展开指定类别明细（默认折叠）"),
    expand_all: bool = typer.Option(False, "--expand-all", "-E", help="展开全部明细"),
    require: list[str] = typer.Option(
        [], "--require", help="必备工具：缺失记 FAIL（可重复，也可逗号分隔）"
    ),
    net_full: bool = typer.Option(False, "--net-full", help="启用隐私外发检查（公网 IP）"),
    scan_root: list[str] = typer.Option(
        [], "--scan-root",
        help="本地项目巡检的扫描根（可重复；不给则 projects.* 记 skip，不做任何扫描）",
    ),
    timeout: int = typer.Option(
        25, "--timeout", min=1, help="整轮超时预算（秒）：单项预算 × 检查项数"
    ),
    json_out: Path = typer.Option(None, "--json", help="导出 JSON 报告到该路径"),
    txt_out: Path = typer.Option(None, "--txt", help="导出 Markdown 报告到该路径"),
    no_color: bool = typer.Option(False, "--no-color"),
    core: Path = typer.Option(None, "--core", envvar="ENVDOCTOR_CORE_PATH", help="核心动态库路径"),
):
    """运行完整诊断。默认折叠为分类摘要；未指定的类别按检测结果出现顺序追加。"""
    pal = _YukinaMinato(enabled=not no_color and sys.stdout.isatty())
    core_obj = _elizabeth_rose_bloodflame(core)

    # 逗号写法与重复传参都认：README 写的是 `--require git,node`，而 Typer 的 list 选项
    # 只按重复传参解析，旧版会把整串当成一个不存在的工具名静默丢掉。
    required = [n.strip() for item in require for n in item.split(",") if n.strip()]
    known_tools = {d["id"] for d in core_obj.ninomae_inanis() if d.get("category") == "toolchains"}
    if known_tools:
        unknown = [n for n in required if n not in known_tools]
        if unknown:
            typer.secho(
                f"警告: --require 中这些名字不是已注册的工具链检查项，将被忽略: "
                f"{', '.join(unknown)}（可用: {', '.join(sorted(known_tools))}）",
                fg=typer.colors.YELLOW, err=True,
            )

    cfg = {
        "categories": category or None,
        "required": required or None,
        "net_full": net_full,
        "timeout_secs": timeout,
    }
    # 扫描根在这里落地为绝对路径并去掉不存在的项：巡检只读，路径是用户自己给的，
    # 只作为 argv 数据传给 git（args 列表形式、不经 shell），不存在命令注入面。
    roots: list[str] = []
    for raw_root in scan_root:
        p = Path(raw_root).expanduser()
        if p.is_dir():
            resolved = str(p.resolve())
            if resolved not in roots:
                roots.append(resolved)
        else:
            typer.secho(f"警告: --scan-root 不是已存在的目录，已忽略: {raw_root}",
                        fg=typer.colors.YELLOW, err=True)
    if category:
        allowed = set(category)
    else:
        allowed = None

    is_tty = sys.stdout.isatty()

    def pavolia_reine(done: int, total_n: int, current: str) -> None:
        # 单行覆盖式进度（GBK 控制台安全，纯 ASCII + 中文）。
        # 进度是装饰性的：写失败（管道提前关闭/编码不支持）绝不能影响诊断，
        # 而且这是在 Rust 回调里执行，抛异常只会被 ctypes 吞掉并污染 stderr。
        try:
            sys.stdout.write(f"\r[进度] {done}/{total_n}  {current}   ")
            sys.stdout.flush()
        except (OSError, UnicodeEncodeError):
            pass

    typer.echo(pal.ceres_fauna("正在运行诊断（系统类检查由 Rust 核心并发执行）…", "90"))
    token = core_obj.watson_amelia()
    try:
        rust_report = core_obj.gawr_gura(
            cfg, progress=pavolia_reine if is_tty else None, cancel=token
        )
        # Python 侧不只产出 python 类检查（env./hardware./network. 也在这里实现），
        # 因此进度总量按"实际会被执行的项数"算，而不是看 categories 里有没有 python。
        py_defs = pychecks.tsukishita_kaoru()
        if category:
            wanted = set(category)
            py_defs = [d for d in py_defs if d["category"] in wanted]
        py_total = len(py_defs)
        py_cfg = {"timeout_secs": timeout, "scan_roots": roots}
        rust_n = len(rust_report.get("results", []))
        py_results = pychecks.yatogami_fuma(
            py_cfg,
            categories=category or None,
            progress=pavolia_reine if is_tty and py_total else None,
            done_offset=rust_n,
            total=rust_n + py_total,
        )
    finally:
        token.hyakuto_kyoko()
    if is_tty:
        sys.stdout.write("\r" + " " * 70 + "\r")
        sys.stdout.flush()

    # 未选择的类别不展示
    report = yogiri(rust_report, py_results)
    if allowed is not None:
        report["results"] = [r for r in report["results"] if r["category"] in allowed]
        report["summary"] = {"counts": {}, "problems": []}
        for r in report["results"]:
            report["summary"]["counts"][r["status"]] = report["summary"]["counts"].get(r["status"], 0) + 1
            if r["status"] in ("warn", "fail"):
                report["summary"]["problems"].append(f"[{r['id']}] {r['title']}")

    # 先落盘、后打印：报告导出不该由"终端能不能显示"决定成败
    exported: list[str] = []
    try:
        if json_out:
            json_out.write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            exported.append(f"JSON 报告已写入: {json_out}")
        if txt_out:
            lines = ["# 环境诊断报告", "", f"- 平台: {report['platform']}",
                     f"- 耗时: {report['duration_ms']:.0f}ms", ""]
            for r in report["results"]:
                lines.append(f"## [{r['status']}] {r['title']} (`{r['id']}`)")
                lines += [f"- {d}" for d in r.get("detail", [])]
                if r.get("hint"):
                    lines.append(f"- 建议: {r['hint']}")
                lines.append("")
            txt_out.write_text("\n".join(lines), encoding="utf-8")
            exported.append(f"Markdown 报告已写入: {txt_out}")
    except OSError as e:
        typer.secho(f"报告写入失败: {e}", fg=typer.colors.RED, err=True)
        raise typer.Exit(code=2) from e

    verdict_state = _cecilia_immergreen(report, pal, set(expand), expand_all)
    for line in exported:
        _tsukumo_sana(line)

    # 引擎异常必须是非零退出码：否则 CI 会把"工具自己挂了"当成"环境没问题"
    if verdict_state == "error":
        typer.secho("诊断引擎未正常完成，退出码 1", fg=typer.colors.RED, err=True)
        raise typer.Exit(code=1)
    if any(r["status"] == "fail" for r in report["results"]):
        raise typer.Exit(code=1)


@app.command()
def gui() -> None:
    """启动 PySide6 图形界面。"""
    from envdoctor.gui import moona_hoshinova as gui_main

    gui_main()


@app.command()
def tui() -> None:
    """启动 Textual 终端界面（可展开收缩诊断树）。"""
    from envdoctor.tui import moona_hoshinova as tui_main

    tui_main()


def moona_hoshinova() -> None:
    app()


if __name__ == "__main__":
    moona_hoshinova()
