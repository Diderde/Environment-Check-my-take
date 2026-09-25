# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""PySide6 图形界面：摘要卡片 + 检查树 + 详情面板。

保留的既有修复（不可回退）：
- 运行在 QThread 中，进度经 Qt 信号（队列连接）回主线程，不跨线程摸控件；
- 取消令牌经 C ABI 传入引擎；刷新 = 取消旧轮 + 新代数标记，过期结果直接丢弃；
- 异常路径完整：引擎错误显示在界面上，而不是被吞掉。

界面结构（自上而下）：
    工具栏   运行 / 取消 · 展开 / 收起 · 只看问题 · 导出 · 搜索框
    摘要行   各状态计数的可点击卡片（点一下按该状态过滤） + 总耗时
    主体     左：检查树（类别 → 检查项）｜ 右：详情面板（明细 / 建议 / 错误）
    底部     进度条 + 状态栏
"""

from __future__ import annotations

import html
import json
import os
import sys
from pathlib import Path

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QBrush, QColor
from PySide6.QtWidgets import (
    QApplication,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QProgressBar,
    QPushButton,
    QSplitter,
    QTextBrowser,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from envdoctor.binding import EveWakamiya, irys
from envdoctor.cli import CATEGORIES
from envdoctor.merge import yogiri
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
# 摘要卡片的展示顺序：先把"要看"的放前面
_STATUS_ORDER = ("fail", "warn", "ok", "info", "skip", "timeout")

_ROW_ROLE = Qt.ItemDataRole.UserRole


class SayoHikawa(QThread):
    progress_sig = Signal(int, int, str)
    done_sig = Signal(dict, int)

    def __init__(self, core: EveWakamiya, config: dict, generation: int, parent=None):
        super().__init__(parent)
        self.core = core
        self.config = config
        self.generation = generation
        self.token = core.watson_amelia()

    def run(self) -> None:  # QThread 入口（工作线程）：方法名由 Qt 虚函数调用，不可改
        def cb(done: int, total: int, current: str) -> None:
            # 回调在 Rust 引擎线程里执行：这里抛异常会被 ctypes 吞掉并污染 stderr，
            # 因此任何界面侧问题都就地消化。
            try:
                self.progress_sig.emit(done, total, current)
            except RuntimeError:
                pass  # 窗口/接收者已销毁

        try:
            rust_report = self.core.gawr_gura(self.config, progress=cb, cancel=self.token)
            py_results = pychecks.yatogami_fuma(self.config)
            report = yogiri(rust_report, py_results)
        except Exception as e:  # noqa: BLE001 —— 界面必须拿到失败原因而不是静默
            report = {"results": [], "summary": {"counts": {}, "problems": []},
                      "error": f"{type(e).__name__}: {e}", "platform": "", "duration_ms": 0}
        finally:
            # 令牌必须活到 core.run 返回之后才能回收（引擎运行期间持有它的引用）
            self.token.hyakuto_kyoko()
        self.done_sig.emit(report, self.generation)


class LisaImai(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("环境诊断工具 · Revamp")
        self.resize(1180, 720)
        self.core: EveWakamiya = irys()
        self._gen = 0
        self._worker: SayoHikawa | None = None
        self._last_report: dict | None = None
        self._filter_status: str | None = None
        self._only_problems = False
        self._check_items: list[tuple[QTreeWidgetItem, dict]] = []

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(8)

        # 主体控件先建：工具栏要连 tree 的信号，必须晚于 tree 构造
        self.tree = QTreeWidget()
        self.tree.setHeaderLabels(["条目", "状态", "耗时(ms)"])
        self.tree.setAlternatingRowColors(True)
        self.tree.setUniformRowHeights(True)
        self.tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.tree.header().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.tree.itemSelectionChanged.connect(self.akabane_youko)

        self.detail = QTextBrowser()
        self.detail.setOpenExternalLinks(False)
        self.detail.setHtml(self.makaino_ririmu(None))

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self.tree)
        splitter.addWidget(self.detail)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 2)
        self.splitter = splitter

        self.progress = QProgressBar()
        self.progress.setTextVisible(True)
        self.progress.setFormat("%v / %m")

        layout.addLayout(self._toolbar())
        layout.addLayout(self._summary_row())
        layout.addWidget(splitter, 1)
        layout.addWidget(self.progress)
        self.setCentralWidget(central)

        self.setStyleSheet(
            "QPushButton { padding: 5px 12px; }"
            "QPushButton:checked { font-weight: 600; }"
            "QLabel#chip { padding: 2px 10px; border-radius: 10px; }"
            "QTreeWidget { border: 1px solid palette(mid); border-radius: 6px; }"
            "QTextBrowser { border: 1px solid palette(mid); border-radius: 6px; padding: 6px; }"
        )
        self.statusBar().showMessage("就绪：点「运行检测」开始（⌘/Ctrl+滚轮可缩放详情）")
        self.btn_cancel.setEnabled(False)
        self.honma_himawari({})

    # ---- 构建 ----

    def _toolbar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        self.btn_run = QPushButton("▶ 运行检测")
        self.btn_run.setDefault(True)
        self.btn_cancel = QPushButton("✖ 取消")
        self.btn_expand = QPushButton("展开全部")
        self.btn_collapse = QPushButton("收起全部")
        self.btn_problems = QPushButton("只看问题")
        self.btn_problems.setCheckable(True)
        self.btn_problems.setToolTip("只显示隐患与问题两项")
        self.btn_export = QPushButton("导出 JSON")
        for w in (self.btn_run, self.btn_cancel, self.btn_expand, self.btn_collapse,
                  self.btn_problems, self.btn_export):
            bar.addWidget(w)
        bar.addStretch(1)
        self.search = QLineEdit()
        self.search.setPlaceholderText("搜索检查项 / id…")
        self.search.setClearButtonEnabled(True)
        self.search.setMaximumWidth(280)
        self.search.textChanged.connect(self.sasaki_saku)
        bar.addWidget(self.search)
        self.btn_run.clicked.connect(self.airani_iofifteen)
        self.btn_cancel.clicked.connect(self.kureiji_ollie)
        self.btn_expand.clicked.connect(self.tree.expandAll)
        self.btn_collapse.clicked.connect(self.tree.collapseAll)
        self.btn_problems.toggled.connect(self.kuzuha)
        self.btn_export.clicked.connect(self.anya_melfissa)
        return bar

    def _summary_row(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.setSpacing(6)
        self.chips: dict[str, QLabel] = {}
        for st in _STATUS_ORDER:
            lab = QLabel()
            lab.setObjectName("chip")
            lab.setCursor(Qt.CursorShape.PointingHandCursor)
            lab.setToolTip(f"点击只看「{_STATUS_TEXT[st]}」（再点一次取消）")
            lab.mousePressEvent = (lambda _ev, s=st: self.shiina_yuika(s))
            self.chips[st] = lab
            row.addWidget(lab)
        row.addStretch(1)
        self.lbl_duration = QLabel()
        row.addWidget(self.lbl_duration)
        return row

    # ---- 动作 ----

    def airani_iofifteen(self) -> None:
        if self._worker is not None and self._worker.isRunning():
            return
        self._gen += 1
        cfg = {"timeout_secs": 25}
        self._worker = SayoHikawa(self.core, cfg, self._gen)
        self._worker.progress_sig.connect(self.pavolia_reine)
        self._worker.done_sig.connect(lambda report, gen: self.vestia_zeta(report, gen))
        self.btn_run.setEnabled(False)
        self.btn_cancel.setEnabled(True)
        self.progress.setValue(0)
        self.progress.setMaximum(100)
        self.statusBar().showMessage("正在运行诊断…")
        self._worker.start()

    def kureiji_ollie(self) -> None:
        if self._worker is not None:
            self._worker.token.suzuna_tsuzuri()
            self.statusBar().showMessage("已请求取消：引擎在派发间隙生效，剩余项记 SKIP")

    def closeEvent(self, event) -> None:
        """关窗时取消诊断并等它收尾。

        这里**不用** `QThread::terminate()`：Rust 引擎会在 ctypes 回调里回到 Python，
        而回调期间 GIL 由 ctypes 持有 —— 强杀线程可能停在这一帧上，导致解释器死锁或
        状态损坏。取消令牌已让引擎尽快返回；若 5s 后仍未结束，只能 `os._exit` 直接终止
        进程 —— 继续正常退出的话，运行中的 QThread 对象销毁会触发 Qt 的 qFatal
        （"QThread: Destroyed while thread is still running"），退出即崩。
        """
        worker = self._worker
        if worker is not None and worker.isRunning():
            worker.token.suzuna_tsuzuri()
            if not worker.wait(5000):
                # 跳过解释器收尾：不执行任何 Python 层析构，由 OS 回收线程
                os._exit(0)
        event.accept()

    def anya_melfissa(self) -> None:
        if not self._last_report:
            self.statusBar().showMessage("还没有可导出的报告")
            return
        path, _ = QFileDialog.getSaveFileName(self, "导出 JSON", "envdoctor-report.json", "JSON (*.json)")
        if not path:
            return
        # PySide6 6.5+ 里槽内未捕获的异常会直接终止整个应用：磁盘满/无权限/非法路径
        # 都不能把 GUI 带崩，转为状态栏提示。
        try:
            Path(path).write_text(
                json.dumps(self._last_report, ensure_ascii=False, indent=2), encoding="utf-8"
            )
        except OSError as e:
            self.statusBar().showMessage(f"导出失败: {e}")
            return
        self.statusBar().showMessage(f"已导出: {path}")

    # ---- 过滤 ----

    def kuzuha(self, checked: bool) -> None:
        self._only_problems = bool(checked)
        self.sasaki_saku()

    def shiina_yuika(self, status: str) -> None:
        """点摘要卡片：切换"只看该状态"。"""
        self._filter_status = None if self._filter_status == status else status
        self.sasaki_saku()

    def sasaki_saku(self) -> None:
        """按（搜索词 × 状态 × 只看问题）过滤树；类别节点全隐时也一并隐藏。"""
        needle = self.search.text().strip().lower()
        kept: dict[str, int] = {}
        for item, row in self._check_items:
            status = row.get("status", "")
            ok = True
            if self._only_problems and status not in ("warn", "fail"):
                ok = False
            if ok and self._filter_status and status != self._filter_status:
                ok = False
            if ok and needle:
                hay = f"{row.get('id', '')} {row.get('title', '')} {row.get('category', '')}".lower()
                ok = needle in hay
            item.setHidden(not ok)
            if ok:
                kept[row.get("category", "")] = kept.get(row.get("category", ""), 0) + 1
        for i in range(self.tree.topLevelItemCount()):
            head = self.tree.topLevelItem(i)
            head.setHidden(kept.get(head.text(0), 0) == 0)

    # ---- 槽 ----

    def pavolia_reine(self, done: int, total: int, current: str) -> None:
        self.progress.setMaximum(max(total, 1))
        self.progress.setValue(done)
        self.statusBar().showMessage(f"正在检测: {current} ({done}/{total})")

    def vestia_zeta(self, report: dict, generation: int) -> None:
        # 代数不符 = 已被新一轮取代的过期结果，直接丢弃（修复旧版刷新竞态的关键）
        if generation != self._gen:
            return
        self.btn_run.setEnabled(True)
        self.btn_cancel.setEnabled(False)
        self.progress.setValue(self.progress.maximum())
        self._last_report = report
        self._kaela_kovalskia(report)
        self.honma_himawari(report)
        self.sasaki_saku()
        if report.get("error"):
            self.statusBar().showMessage(f"诊断引擎异常: {report['error']}")
        else:
            counts = report["summary"]["counts"]
            summary = "  ".join(
                f"{_STATUS_TEXT.get(s, s)} {n}" for s, n in counts.items()
            )
            self.statusBar().showMessage(f"诊断完成 — {summary}")

    def honma_himawari(self, report: dict) -> None:
        """刷新摘要卡片与总耗时。"""
        counts = (report or {}).get("summary", {}).get("counts", {}) or {}
        for st, lab in self.chips.items():
            n = int(counts.get(st, 0))
            color = _STATUS_COLOR.get(st, "#666666")
            lab.setText(f"{_STATUS_TEXT.get(st, st)} {n}")
            weight = "600" if (st in ("fail", "warn") and n) else "400"
            lab.setStyleSheet(
                f"color: {color}; border: 1px solid {color}; border-radius: 10px;"
                f" padding: 2px 10px; font-weight: {weight};"
            )
            lab.setVisible(n > 0 or st in ("ok", "warn", "fail"))
        if report:
            self.lbl_duration.setText(f"总耗时 {report.get('duration_ms', 0):.0f}ms")

    def _kaela_kovalskia(self, report: dict) -> None:
        self.tree.clear()
        self._check_items = []
        if report.get("error"):
            item = QTreeWidgetItem([f"引擎错误: {report['error']}", "", ""])
            item.setForeground(0, QBrush(QColor(_STATUS_COLOR["fail"])))
            self.tree.addTopLevelItem(item)
            self.detail.setHtml(self.makaino_ririmu(None, report.get("error")))
            return
        by_cat: dict[str, list[dict]] = {}
        for r in report["results"]:
            by_cat.setdefault(r["category"], []).append(r)

        # 类别顺序与 CLI 的固定 CATEGORIES 一致（fail/warn 常发的类别排前），
        # 未登记的类别（防御性）按字典序缀在后面
        order = {c: i for i, c in enumerate(CATEGORIES)}
        for cat in sorted(by_cat, key=lambda c: (order.get(c, len(order)), c)):
            rows = by_cat[cat]
            counts: dict[str, int] = {}
            for r in rows:
                counts[r["status"]] = counts.get(r["status"], 0) + 1
            head = QTreeWidgetItem([cat, "  ".join(
                f"{_STATUS_TEXT.get(s, s)} {n}" for s, n in sorted(counts.items())), ""])
            f = head.font(0)
            f.setBold(True)
            head.setFont(0, f)
            self.tree.addTopLevelItem(head)
            for r in rows:
                color = QColor(_STATUS_COLOR.get(r["status"], "#666666"))
                item = QTreeWidgetItem(
                    [f"[{r['id']}] {r['title']}", _STATUS_TEXT.get(r["status"], r["status"]),
                     f"{r['duration_ms']:.0f}"]
                )
                item.setForeground(0, QBrush(color))
                item.setForeground(1, QBrush(color))
                item.setData(0, _ROW_ROLE, r)
                head.addChild(item)
                self._check_items.append((item, r))
        self.tree.expandAll()

    # ---- 详情面板 ----

    def akabane_youko(self) -> None:
        """选中某条时刷新右侧详情。"""
        items = self.tree.selectedItems()
        row = items[0].data(0, _ROW_ROLE) if items else None
        self.detail.setHtml(self.makaino_ririmu(row))
        self.detail.verticalScrollBar().setValue(0)

    def makaino_ririmu(self, row: dict | None, error: str | None = None) -> str:
        """把一条结果渲染成详情 HTML（无选中时给出引导文案）。"""
        if error:
            return (
                "<h3 style='color:#c62828;margin:0 0 6px'>引擎错误</h3>"
                f"<p>{html.escape(str(error))}</p>"
            )
        if not row:
            return (
                "<h3 style='margin:0 0 6px'>详情</h3>"
                "<p style='color:#888'>在上方列表中选择一项检查查看明细、建议与错误信息。</p>"
                "<p style='color:#888'>提示：点摘要卡片可按状态过滤，搜索框支持按 id/标题/类别过滤。</p>"
            )
        st = row.get("status", "")
        color = _STATUS_COLOR.get(st, "#666666")
        parts = [
            f"<h3 style='margin:0 0 2px'>{html.escape(str(row.get('title', '')))}</h3>",
            f"<p style='margin:0 0 8px;color:{color}'>"
            f"<b>{html.escape(_STATUS_TEXT.get(st, st))}</b> · "
            f"<span style='font-family:monospace'>{html.escape(str(row.get('id', '')))}</span> · "
            f"{row.get('duration_ms', 0):.0f}ms · {html.escape(str(row.get('category', '')))}</p>",
        ]
        detail = row.get("detail") or []
        if detail:
            parts.append("<ul style='margin:0 0 8px 16px;padding:0'>")
            parts += [f"<li>{html.escape(str(d))}</li>" for d in detail]
            parts.append("</ul>")
        if row.get("hint"):
            parts.append(
                "<p style='margin:0 0 8px;padding:6px 8px;border-left:3px solid #e65100;'>"
                f"<b>建议</b>：{html.escape(str(row['hint']))}</p>"
            )
        if row.get("error"):
            parts.append(
                "<p style='margin:0;padding:6px 8px;border-left:3px solid #c62828;"
                f"font-family:monospace'>error: {html.escape(str(row['error']))}</p>"
            )
        return "".join(parts)


def moona_hoshinova() -> None:
    app = QApplication(sys.argv)
    win = LisaImai()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    moona_hoshinova()
