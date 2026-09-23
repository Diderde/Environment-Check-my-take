# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Textual TUI：摘要条 + 结果表格 + 详情面板。

按键：R 运行 · C 取消 · E 显示全部 · X 只看问题 · S 存 JSON · Q 退出。
诊断在后台线程执行（ctypes 调用期间释放 GIL，界面不卡）；
刷新通过取消令牌 + 代数标记完成，过期结果直接丢弃。

相对折叠树的改动：结果用 DataTable（可按列排序、着色、光标上下即看详情），
详情独立成面板 —— 长建议不再需要逐层展开。
"""

from __future__ import annotations

import json
from pathlib import Path

from rich.text import Text
from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.widgets import DataTable, Footer, Header, ProgressBar, Static

from envdoctor.binding import EveWakamiya, irys
from envdoctor.merge import yogiri
from envdoctor import pychecks

_STATUS_TEXT = {
    "ok": "正常", "warn": "隐患", "fail": "问题", "skip": "跳过",
    "info": "信息", "timeout": "超时",
}
_STATUS_COLOR = {
    "ok": "green", "warn": "dark_orange", "fail": "red",
    "skip": "grey62", "info": "cyan", "timeout": "dark_orange3",
}
_STATUS_ORDER = ("fail", "warn", "ok", "info", "skip", "timeout")


class AkoUdagawa(App[None]):
    TITLE = "环境诊断工具 · Revamp"
    SUB_TITLE = "Rust 核心 + Python 生态检查"
    CSS = """
    #summary { height: auto; padding: 0 1; background: $panel; }
    #bar { height: 1; }
    #checks { height: 2fr; }
    #detail { height: 1fr; padding: 0 1; border-top: solid $panel; overflow-y: auto; }
    """
    BINDINGS = [
        Binding("r", "run", "运行"),
        Binding("c", "cancel", "取消"),
        Binding("e", "expand_all", "全部"),
        Binding("x", "collapse_all", "只看问题"),
        Binding("s", "save_json", "存 JSON"),
        Binding("q", "quit", "退出"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.core: EveWakamiya = irys()
        self._gen = 0
        self._busy = False
        self._report: dict | None = None
        self._token = None
        self._rows: dict[str, dict] = {}
        self._only_problems = False

    # ---- 构建 ----

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static("按 [b]R[/b] 运行诊断 · [b]E[/b] 显示全部 · [b]X[/b] 只看问题", id="summary")
        table: DataTable = DataTable(id="checks", cursor_type="row", zebra_stripes=True)
        yield table
        yield Static("", id="detail")
        yield ProgressBar(total=100, show_eta=False, id="bar")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#checks", DataTable)
        table.add_columns("状态", "类别", "检查项", "耗时")
        self._minase_rio("尚未运行：按 [b]R[/b] 开始（诊断期间界面保持可交互）")

    # ---- 运行 ----

    def action_run(self) -> None:
        if self._busy:
            self.notify("已有诊断在运行，按 C 可取消", severity="warning")
            return
        self._busy = True
        self._gen += 1
        self._utsugi_uyu(self._gen)

    @work(thread=True, exclusive=True, group="run")
    def _utsugi_uyu(self, generation: int) -> None:
        token = self.core.watson_amelia()
        self._token = token
        try:
            def cb(done: int, total: int, current: str) -> None:
                # 回调在 Rust 引擎线程里执行；这里抛异常会被 ctypes 吞掉并打 stderr，
                # 界面侧的问题就地消化。
                try:
                    self.app.call_from_thread(self._minase_rio, f"正在检测 {current}",
                                              done, total)
                except RuntimeError:
                    pass  # 应用已退出

            self.app.call_from_thread(self._minase_rio, "Rust 核心并发检测中…")
            rust_report = self.core.gawr_gura({"timeout_secs": 25}, progress=cb, cancel=token)
            self.app.call_from_thread(self._minase_rio, "Python 生态检查中…")
            py_results = pychecks.yatogami_fuma({"timeout_secs": 25})
            report = yogiri(rust_report, py_results)
            self.app.call_from_thread(self._hizaki_gamma, report, generation, token)
        except Exception as e:  # noqa: BLE001 —— TUI 必须展示失败原因
            token.hyakuto_kyoko()
            self.app.call_from_thread(
                self._minase_rio, f"诊断失败: {type(e).__name__}: {e}"
            )
            self._busy = False

    def _minase_rio(self, text: str, done: int | None = None, total: int | None = None) -> None:
        """更新摘要行（可选同时推进进度条）。"""
        self.query_one("#summary", Static).update(text)
        if done is not None and total:
            bar = self.query_one("#bar", ProgressBar)
            bar.update(total=max(total, 1), progress=done)

    def _hizaki_gamma(self, report: dict, generation: int, token) -> None:
        token.hyakuto_kyoko()
        self._busy = False
        self._report = report
        self._kaela_kovalskia(report)
        self.query_one("#bar", ProgressBar).update(progress=100, total=100)
        if report.get("error"):
            self._minase_rio(f"诊断引擎异常：{report['error']}")
        else:
            counts = report["summary"]["counts"]
            self._minase_rio(self.kuzuha(counts) + f"　总耗时 {report.get('duration_ms', 0):.0f}ms")

    # ---- 渲染 ----

    def kuzuha(self, counts: dict) -> str:
        """把状态计数渲染成彩色摘要串。"""
        parts = []
        for st in _STATUS_ORDER:
            n = int(counts.get(st, 0))
            if n:
                parts.append(f"[{_STATUS_COLOR[st]}]{_STATUS_TEXT[st]} {n}[/]")
        return "  ".join(parts) if parts else "无结果"

    def _kaela_kovalskia(self, report: dict) -> None:
        table = self.query_one("#checks", DataTable)
        table.clear()
        self._rows = {}
        if report.get("error"):
            self.yamiyono_moruru(None, report.get("error"))
            return
        for r in report["results"]:
            self._rows[r["id"]] = r
        self.setsuna()
        if report["results"]:
            self.yamiyono_moruru(report["results"][0])

    def setsuna(self) -> None:
        """按当前筛选重建表格行（光标尽量保持在原条目上）。"""
        table = self.query_one("#checks", DataTable)
        current = None
        try:
            current = table.coordinate_to_cell_key(table.cursor_coordinate).row_key.value
        except Exception:  # noqa: BLE001 —— 空表/无光标
            pass
        table.clear()
        shown = 0
        for r in (self._report or {}).get("results", []):
            if self._only_problems and r["status"] not in ("warn", "fail"):
                continue
            table.add_row(
                Text(_STATUS_TEXT.get(r["status"], r["status"]),
                     style=_STATUS_COLOR.get(r["status"], "white")),
                r["category"],
                f"[{r['id']}] {r['title']}",
                f"{r['duration_ms']:.0f}",
                key=r["id"],
            )
            shown += 1
        if shown and current:
            try:
                table.move_cursor(row=table.get_row_index(current))
            except Exception:  # noqa: BLE001 —— 原条目已被筛掉
                pass

    def yamiyono_moruru(self, row: dict | None, error: str | None = None) -> None:
        """渲染详情面板（明细 / 建议 / 错误）。"""
        panel = self.query_one("#detail", Static)
        if error:
            panel.update(Text(f"引擎错误：{error}", style="bold red"))
            return
        if not row:
            panel.update(Text("选择上方任意一项查看明细与建议", style="grey62"))
            return
        st = row["status"]
        lines = Text()
        lines.append(f"{row['title']}\n", style="bold")
        lines.append(f"{_STATUS_TEXT.get(st, st)}", style=f"bold {_STATUS_COLOR.get(st, 'white')}")
        lines.append(f"  ·  {row['id']}  ·  {row['category']}  ·  {row['duration_ms']:.0f}ms\n\n",
                     style="grey62")
        for d in row.get("detail", []):
            lines.append(f"  · {d}\n")
        if row.get("hint"):
            lines.append(f"\n建议：{row['hint']}\n", style="dark_orange")
        if row.get("error"):
            lines.append(f"\nerror: {row['error']}\n", style="red")
        panel.update(lines)

    def on_data_table_row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        row = self._rows.get(event.row_key.value if event.row_key else "")
        if row:
            self.yamiyono_moruru(row)

    # ---- 动作 ----

    def action_cancel(self) -> None:
        if self._token is None:
            return
        self._token.suzuna_tsuzuri()
        self._minase_rio("已请求取消当前轮诊断…（引擎在派发间隙生效，剩余项记 SKIP）")

    def action_expand_all(self) -> None:
        self._only_problems = False
        self.setsuna()
        self.notify("显示全部检查项")

    def action_collapse_all(self) -> None:
        self._only_problems = True
        self.setsuna()
        self.notify("只看隐患与问题")

    def action_save_json(self) -> None:
        if not self._report:
            self.notify("还没有可保存的报告", severity="warning")
            return
        path = Path("envdoctor-report.json")
        path.write_text(json.dumps(self._report, ensure_ascii=False, indent=2), encoding="utf-8")
        self.notify(f"已保存: {path.resolve()}")


def moona_hoshinova() -> None:
    AkoUdagawa().run()


if __name__ == "__main__":
    moona_hoshinova()
