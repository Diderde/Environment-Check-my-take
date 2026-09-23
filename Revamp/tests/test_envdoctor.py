# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""envdoctor Python 层单元测试。运行：python -m unittest discover -s tests -v"""

from __future__ import annotations

import ctypes
import unittest
from pathlib import Path
from unittest import skipUnless

from envdoctor.binding import Core, _dll_candidates
from envdoctor.merge import merge_reports, verdict
from envdoctor import pychecks


def _dll_ready() -> bool:
    return any(c.is_file() for c in _dll_candidates())


class MergeTest(unittest.TestCase):
    def test_merge_sorts_and_counts(self):
        rust = {
            "results": [
                {"id": "network.dns", "title": "DNS", "category": "network",
                 "status": "ok", "detail": [], "hint": None, "duration_ms": 1.0, "error": None},
            ],
            "platform": "windows",
            "duration_ms": 2.0,
        }
        py = [
            {"id": "python.venv", "title": "虚拟环境", "category": "python",
             "status": "warn", "detail": [], "hint": "用 venv", "duration_ms": 0.5, "error": None},
        ]
        report = merge_reports(rust, py)
        self.assertEqual(len(report["results"]), 2)
        # 排序：category 字典序
        self.assertEqual(report["results"][0]["category"], "network")
        self.assertEqual(report["summary"]["counts"]["ok"], 1)
        self.assertEqual(report["summary"]["counts"]["warn"], 1)
        self.assertEqual(len(report["summary"]["problems"]), 1)

    def test_verdict_states(self):
        good = {"results": [], "summary": {"counts": {}, "problems": []}}
        self.assertEqual(verdict(good)[0], "good")
        with_issue = {"results": [
            {"id": "a", "title": "A", "category": "c", "status": "warn",
             "detail": [], "hint": "h", "duration_ms": 0, "error": None},
        ]}
        state, problems = verdict(with_issue)
        self.assertEqual(state, "issues")
        self.assertEqual(len(problems), 1)
        errored = {"results": [], "error": "boom", "summary": {"counts": {}, "problems": []}}
        self.assertEqual(verdict(errored)[0], "error")


class PyChecksTest(unittest.TestCase):
    def test_outcome_schema(self):
        r = pychecks.check_gil({})
        for key in ("id", "title", "category", "status", "detail", "hint", "duration_ms", "error"):
            self.assertIn(key, r)
        self.assertEqual(r["category"], "python")
        self.assertEqual(r["status"], "info")

    def test_venv_inside_project_venv(self):
        # 本测试运行在 .venv 中，应当识别为虚拟环境
        r = pychecks.check_venv({})
        self.assertIn(r["status"], ("ok", "skip"))

    def test_py_check_defs_unique(self):
        defs = pychecks.py_check_defs()
        ids = [d["id"] for d in defs]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertIn("python.interpreter", ids)


@skipUnless(_dll_ready(), "Rust 核心 DLL 未构建，跳过绑定测试")
class BindingTest(unittest.TestCase):
    def setUp(self):
        self.core = Core()

    def test_version_format(self):
        self.assertRegex(self.core.version(), r"^\d+\.\d+\.\d+$")

    def test_run_single_category(self):
        report = self.core.run({"categories": ["databases"], "timeout_secs": 5})
        self.assertIsNone(report.get("error"))
        self.assertGreater(len(report["results"]), 0)
        for r in report["results"]:
            self.assertEqual(r["category"], "databases")

    def test_cancel_token_roundtrip(self):
        token = self.core.new_cancel_token()
        token.trigger()
        report = self.core.run({"categories": ["databases"]}, cancel=token)
        token.close()
        # 取消后所有结果应为 SKIP
        self.assertTrue(all(r["status"] == "skip" for r in report["results"]))


if __name__ == "__main__":
    unittest.main()
