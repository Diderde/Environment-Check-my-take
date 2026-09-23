# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""PySide6 图形界面：分类/检查/明细三级可展开收缩树。

相对旧版 GUI 的关键修复：
- 运行在 QThread 中，进度经 Qt 信号（队列连接）回主线程，不再跨线程摸控件；
- 取消令牌经 C ABI 传入引擎，刷新 = 取消旧轮 + 新代数标记，过期结果直接丢弃，
  旧版"刷新串项/重复输出"的竞态不复存在；
- 异常路径完整：引擎错误显示在界面上，而不是被吞掉。
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QBrush, QColor
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QMainWindow,
    QProgressBar,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from envdoctor.binding import Core, get_core
from envdoctor.merge import merge_reports
from envdoctor import pychecks

_STATUS_COLOR = {
    "ok": "#2e7d32",
    "warn": "#e65100",
    "fail": "#c62828",
    "skip": "#9e9e9e",
    "info": "#1565c0",
    "timeout": "#bf360c",
}
_STATUS_TEXT = {
    "ok": "正常", "warn": "隐患", "fail": "问题", "skip": "跳过",
    "info": "信息", "timeout": "超时",
}


class RunWorker(QThread):
    progress_sig = Signal(int, int, str)
    done_sig = Signal(dict, int)

    def __init__(self, core: Core, config: dict, generation: int, parent=None):
        super().__init__(parent)
        self.core = core
        self.config = config
        self.generation = generation
        self.token = core.new_cancel_token()

    def run(self) -> None:  # QThread 入口（工作线程）
        def cb(done: int, total: int, current: str) -> None:
            # 回调在 Rust 引擎线程里执行：这里抛异常会被 ctypes 吞掉并污染 stderr，
            # 因此任何界面侧问题都就地消化。
            try:
                self.progress_sig.emit(done, total, current)
            except RuntimeError:
                pass  # 窗口/接收者已销毁

        try:
            rust_report = self.core.run(self.config, progress=cb, cancel=self.token)
            py_results = pychecks.run_python_checks(self.config)
            report = merge_reports(rust_report, py_results)
        except Exception as e:  # noqa: BLE001 —— 界面必须拿到失败原因而不是静默
            report = {"results": [], "summary": {"counts": {}, "problems": []},
                      "error": f"{type(e).__name__}: {e}", "platform": "", "duration_ms": 0}
        finally:
            # 令牌必须活到 core.run 返回之后才能回收（引擎运行期间持有它的引用）
            self.token.close()
        self.done_sig.emit(report, self.generation)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("环境诊断工具 · Revamp")
        self.resize(1080, 700)
        self.core: Core = get_core()
        self._gen = 0
        self._worker: RunWorker | None = None
        self._last_report: dict | None = None

        central = QWidget()
        layout = QVBoxLayout(central)
        buttons = QHBoxLayout()
        self.btn_run = QPushButton("▶ 运行检测")
        self.btn_cancel = QPushButton("✖ 取消")
        self.btn_expand = QPushButton("展开全部")
        self.btn_collapse = QPushButton("收起全部")
        self.btn_export = QPushButton("导出 JSON")
        for b in (self.btn_run, self.btn_cancel, self.btn_expand, self.btn_collapse, self.btn_export):
            buttons.addWidget(b)
        layout.addLayout(buttons)

        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(["条目", "状态", "耗时(ms)"])
        self.tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.tree)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        layout.addWidget(self.progress)
        self.setCentralWidget(central)

        self.btn_run.clicked.connect(self.start_run)
        self.btn_cancel.clicked.connect(self.cancel_run)
        self.btn_expand.clicked.connect(self.tree.expandAll)
        self.btn_collapse.clicked.connect(self.tree.collapseAll)
        self.btn_export.clicked.connect(self.export_json)
        self.btn_cancel.setEnabled(False)

    # ---- 动作 ----

    def start_run(self) -> None:
        if self._worker is not None and self._worker.isRunning():
            return
        self._gen += 1
        cfg = {"timeout_secs": 25}
        self._worker = RunWorker(self.core, cfg, self._gen)
        self._worker.progress_sig.connect(self.on_progress)
        self._worker.done_sig.connect(lambda report, gen: self.on_done(report, gen))
        self.btn_run.setEnabled(False)
        self.btn_cancel.setEnabled(True)
        self.progress.setValue(0)
        self._worker.start()

    def cancel_run(self) -> None:
        if self._worker is not None:
            self._worker.token.trigger()

    def closeEvent(self, event) -> None:
        """关窗时取消诊断并等它收尾。

        这里**不用** `QThread::terminate()`：Rust 引擎会在 ctypes 回调里回到 Python，
        而回调期间 GIL 由 ctypes 持有 —— 强杀线程可能停在这一帧上，导致解释器死锁或
        状态损坏。取消令牌已让引擎尽快返回；即便它没来得及结束，进程退出时线程会被
        系统收回，Qt 也会在窗口销毁时断开队列连接，不会有"对着已销毁控件发信号"的问题。
        """
        worker = self._worker
        if worker is not None and worker.isRunning():
            worker.token.trigger()
            if not worker.wait(5000):
                self.statusBar().showMessage("诊断线程未在 5s 内结束，随窗口一起退出")
        event.accept()

    def export_json(self) -> None:
        if not self._last_report:
            return
        path, _ = QFileDialog.getSaveFileName(self, "导出 JSON", "envdoctor-report.json", "JSON (*.json)")
        if path:
            Path(path).write_text(
                json.dumps(self._last_report, ensure_ascii=False, indent=2), encoding="utf-8"
            )

    # ---- 槽 ----

    def on_progress(self, done: int, total: int, current: str) -> None:
        self.progress.setMaximum(max(total, 1))
        self.progress.setValue(done)
        self.statusBar().showMessage(f"正在检测: {current} ({done}/{total})")

    def on_done(self, report: dict, generation: int) -> None:
        # 代数不符 = 已被新一轮取代的过期结果，直接丢弃（修复旧版刷新竞态的关键）
        if generation != self._gen:
            return
        self.btn_run.setEnabled(True)
        self.btn_cancel.setEnabled(False)
        self.progress.setValue(self.progress.maximum())
        self._last_report = report
        self._populate(report)
        counts = report["summary"]["counts"]
        summary = "  ".join(f"{_STATUS_TEXT.get(s, s)}: {n}" for s, n in counts.items())
        self.statusBar().showMessage(f"诊断完成 — {summary}")

    def _populate(self, report: dict) -> None:
        self.tree.clear()
        if report.get("error"):
            item = QTreeWidgetItem([f"引擎错误: {report['error']}", "", ""])
            item.setForeground(0, QBrush(QColor(_STATUS_COLOR["fail"])))
            self.tree.addTopLevelItem(item)
            return
        by_cat: dict[str, list[dict]] = {}
        for r in report["results"]:
            by_cat.setdefault(r["category"], []).append(r)

        for cat, rows in by_cat.items():
            counts: dict[str, int] = {}
            for r in rows:
                counts[r["status"]] = counts.get(r["status"], 0) + 1
            head = QTreeWidgetItem([cat, "  ".join(f"{_STATUS_TEXT.get(s, s)}:{n}" for s, n in counts.items()), ""])
            f = head.font(0)
            f.setBold(True)
            head.setFont(0, f)
            self.tree.addTopLevelItem(head)
            for r in rows:
                check_item = QTreeWidgetItem(
                    [f"[{r['id']}] {r['title']}", _STATUS_TEXT.get(r["status"], r["status"]),
                     f"{r['duration_ms']:.0f}"]
                )
                color = QColor(_STATUS_COLOR.get(r["status"], "#000000"))
                check_item.setForeground(0, QBrush(color))
                check_item.setForeground(1, QBrush(color))
                head.addChild(check_item)
                for d in r.get("detail", []):
                    check_item.addChild(QTreeWidgetItem([f"· {d}", "", ""]))
                if r.get("hint"):
                    hint_item = QTreeWidgetItem([f"建议: {r['hint']}", "", ""])
                    hint_item.setForeground(0, QBrush(QColor(_STATUS_COLOR["warn"])))
                    check_item.addChild(hint_item)
        self.tree.expandAll()


def main() -> None:
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
