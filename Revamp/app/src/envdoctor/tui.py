# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""Textual TUI：终端里的可展开收缩诊断树。

按键：R 运行 · C 取消 · E 展开全部 · X 收起全部 · S 存 JSON · Q 退出。
诊断在后台线程执行（ctypes 调用期间释放 GIL，界面不卡）；
刷新通过取消令牌 + 代数标记完成，过期结果直接丢弃。
"""

from __future__ import annotations

import json
from pathlib import Path

from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.widgets import Footer, Header, Static, Tree

from envdoctor.binding import Core, get_core
from envdoctor.merge import merge_reports
from envdoctor import pychecks

_STATUS_TEXT = {
    "ok": "正常", "warn": "隐患", "fail": "问题", "skip": "跳过",
    "info": "信息", "timeout": "超时",
}


class EnvDoctorTUI(App[None]):
    TITLE = "环境诊断工具 · Revamp (TUI)"
    CSS = """
    #info { height: 3; padding: 0 1; }
    Tree { height: 1fr; }
    """
    BINDINGS = [
        Binding("r", "run", "运行"),
        Binding("c", "cancel", "取消"),
        Binding("e", "expand_all", "展开全部"),
        Binding("x", "collapse_all", "收起全部"),
        Binding("s", "save_json", "存JSON"),
        Binding("q", "quit", "退出"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.core: Core = get_core()
        self._gen = 0
        self._busy = False
        self._report: dict | None = None
        self._token = None

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static("按 [R] 运行诊断；诊断在 Rust 核心并发执行。", id="info")
        tree: Tree = Tree("envdoctor", id="tree")
        tree.show_root = False
        yield tree
        yield Footer()

    def action_run(self) -> None:
        if self._busy:
            self.notify("已有诊断在运行，按 C 可取消", severity="warning")
            return
        self._busy = True
        self._gen += 1
        self._run_checks(self._gen)

    @work(thread=True, exclusive=True, group="run")
    def _run_checks(self, generation: int) -> None:
        token = self.core.new_cancel_token()
        self._token = token
        try:
            def cb(done: int, total: int, current: str) -> None:
                self.app.call_from_thread(
                    self._set_status, f"正在检测 {current}（{done}/{total}）"
                )

            self.app.call_from_thread(self._set_status, "Rust 核心并发检测中…")
            rust_report = self.core.run({"timeout_secs": 25}, progress=cb, cancel=token)
            self.app.call_from_thread(self._set_status, "Python 生态检查中…")
            py_results = pychecks.run_python_checks({"timeout_secs": 25})
            report = merge_reports(rust_report, py_results)
            self.app.call_from_thread(self._finish, report, generation, token)
        except Exception as e:  # noqa: BLE001 —— TUI 必须展示失败原因
            token.close()
            self.app.call_from_thread(
                self._set_status, f"诊断失败: {type(e).__name__}: {e}"
            )
            self._busy = False

    def _set_status(self, text: str) -> None:
        self.query_one("#info", Static).update(text)

    def _finish(self, report: dict, generation: int, token) -> None:
        token.close()
        self._busy = False
        self._report = report
        self._populate(report)
        counts = report["summary"]["counts"]
        summary = "  ".join(f"{_STATUS_TEXT.get(s, s)}:{n}" for s, n in counts.items())
        self._set_status(f"诊断完成 — {summary}")

    def _populate(self, report: dict) -> None:
        tree = self.query_one("#tree", Tree)
        tree.clear()
        if report.get("error"):
            tree.root.set_label(f"引擎错误: {report['error']}")
            tree.root.expand()
            return
        by_cat: dict[str, list[dict]] = {}
        for r in report["results"]:
            by_cat.setdefault(r["category"], []).append(r)
        for cat, rows in by_cat.items():
            counts: dict[str, int] = {}
            for r in rows:
                counts[r["status"]] = counts.get(r["status"], 0) + 1
            summary = "  ".join(f"{_STATUS_TEXT.get(s, s)}:{n}" for s, n in counts.items())
            cat_node = tree.root.add(f"{cat}  ({summary})", expand=True)
            for r in rows:
                check_node = cat_node.add(
                    f"{r['title']}  [{_STATUS_TEXT.get(r['status'], r['status'])}]  {r['duration_ms']:.0f}ms",
                    expand=False,
                )
                for d in r.get("detail", []):
                    check_node.add_leaf(f"· {d}")
                if r.get("hint"):
                    check_node.add_leaf(f"建议: {r['hint']}")
        tree.root.expand()

    def action_cancel(self) -> None:
        if self._token is None:
            return
        self._token.trigger()
        self._set_status("已请求取消当前轮诊断…（引擎在派发间隙生效，剩余项记 SKIP）")

    def action_expand_all(self) -> None:
        self.query_one("#tree", Tree).root.expand_all()

    def action_collapse_all(self) -> None:
        tree = self.query_one("#tree", Tree)
        tree.root.collapse()
        tree.root.expand()

    def action_save_json(self) -> None:
        if not self._report:
            self.notify("还没有可保存的报告", severity="warning")
            return
        path = Path("envdoctor-report.json")
        path.write_text(json.dumps(self._report, ensure_ascii=False, indent=2), encoding="utf-8")
        self.notify(f"已保存: {path.resolve()}")


def main() -> None:
    EnvDoctorTUI().run()


if __name__ == "__main__":
    main()
