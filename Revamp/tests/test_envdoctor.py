# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""envdoctor Python 层单元测试。运行：python -m unittest discover -s tests -v"""

from __future__ import annotations

import ctypes
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock, skipUnless

from envdoctor.binding import Core, CoreNotAvailable, _dll_candidates
from envdoctor.merge import merge_reports, redact_url, verdict
from envdoctor import cli, pychecks


def _dll_ready() -> bool:
    return any(c.is_file() for c in _dll_candidates())


class _StubToken:
    def __init__(self):
        self.triggered = False
        self.closed = False

    def trigger(self):
        self.triggered = True

    def close(self):
        self.closed = True


class _StubCore:
    """最小核心替身：用于在无 DLL 环境下测 CLI 行为（退出码/参数解析/列表）。"""

    path = Path("<stub>")

    def __init__(self, report: dict | None = None, checks: list[dict] | None = None):
        self.report = report if report is not None else {
            "report_version": 1, "source": "rust", "platform": "windows",
            "generated_at_unix": 0, "duration_ms": 1.0, "error": None,
            "results": [], "summary": {"counts": {}, "problems": []},
        }
        self.checks = checks if checks is not None else [
            {"id": "git", "title": "Git", "category": "toolchains", "platforms": []},
            {"id": "make", "title": "Make", "category": "toolchains", "platforms": []},
            {"id": "node", "title": "Node.js", "category": "toolchains", "platforms": []},
        ]
        self.last_config: dict | None = None
        self.tokens: list[_StubToken] = []

    def version(self):
        return "0.0.0"

    def list_checks(self):
        return list(self.checks)

    def new_cancel_token(self):
        t = _StubToken()
        self.tokens.append(t)
        return t

    def run(self, config=None, progress=None, cancel=None):
        self.last_config = dict(config or {})
        return self.report


def _run_cli(argv: list[str], core: _StubCore) -> tuple[int, str]:
    """在进程内跑一次 CLI，返回 (退出码, 捕获输出)。

    Python 侧检查整体替身掉：这里测的是 CLI 行为，不该真去跑 pip list --outdated。
    """
    cli._load_core = lambda _p: core
    buf = io.StringIO()
    old_out, old_err = sys.stdout, sys.stderr
    sys.stdout = buf
    sys.stderr = buf
    code = 0
    try:
        with mock.patch.object(pychecks, "run_python_checks", return_value=[]):
            cli.app(argv)
    except SystemExit as e:
        code = int(e.code or 0)
    finally:
        sys.stdout, sys.stderr = old_out, old_err
    return code, buf.getvalue()


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


class RedactionTest(unittest.TestCase):
    """报告会被导出/粘贴，凭据不能明文落进去（与 Rust mask_credentials 同规则）。"""

    def test_proxy_and_index_credentials_are_masked(self):
        self.assertEqual(
            redact_url("http://alice:S3cr3tP@ss@proxy.corp:8080"),
            "http://***@proxy.corp:8080",
        )
        self.assertEqual(
            redact_url("https://bob:tok3n@nexus.corp/repository/pypi/simple"),
            "https://***@nexus.corp/repository/pypi/simple",
        )

    def test_non_credential_values_pass_through(self):
        for value in ("localhost,127.0.0.1", "https://pypi.org/simple", "", r"C:\Users\x"):
            self.assertEqual(redact_url(value), value)

    def test_env_vars_check_does_not_leak_credentials(self):
        old = dict(os.environ)
        try:
            os.environ["HTTP_PROXY"] = "http://alice:S3cr3tP@ss@proxy.corp:8080"
            os.environ["PIP_INDEX_URL"] = "https://bob:tok3n@nexus.corp/repository/pypi/simple"
            blob = json.dumps(pychecks.check_env_vars({}), ensure_ascii=False)
            self.assertNotIn("S3cr3tP@ss", blob)
            self.assertNotIn("tok3n", blob)
            self.assertIn("***", blob)
        finally:
            os.environ.clear()
            os.environ.update(old)


class DecodeTest(unittest.TestCase):
    """子进程输出按 locale 编码写管道时的解码协商。"""

    def test_utf8_preferred(self):
        self.assertEqual(pychecks._decode("中文".encode("utf-8")), "中文")

    def test_gbk_fallback(self):
        raw = "中文安装路径".encode("gbk")
        with self.assertRaises(UnicodeDecodeError):
            raw.decode("utf-8")
        self.assertEqual(pychecks._decode(raw), "中文安装路径")

    def test_empty_and_none(self):
        self.assertEqual(pychecks._decode(None), "")
        self.assertEqual(pychecks._decode(b""), "")


def _sample_report(statuses: dict[str, str]) -> dict:
    results = [
        {"id": f"x.{k}", "title": f"标题 {k}", "category": "databases", "status": v,
         "detail": ["明细 · 带装饰字符 ✅"], "hint": None, "duration_ms": 1.0, "error": None}
        for k, v in statuses.items()
    ]
    counts: dict[str, int] = {}
    problems = []
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
        if r["status"] in ("warn", "fail"):
            problems.append(f"[{r['id']}] {r['title']}")
    return {
        "report_version": 1, "source": "merged(rust+python)", "platform": "windows",
        "generated_at_unix": 0, "duration_ms": 1.0, "error": None,
        "results": results, "summary": {"counts": counts, "problems": problems},
    }


class CliOutputTest(unittest.TestCase):
    """P0 回归：cp936（中文 Windows 默认）下打印必须降级而不是抛 UnicodeEncodeError。"""

    def _gbk_stdout(self):
        return io.TextIOWrapper(io.BytesIO(), encoding="gbk", errors="strict", newline="")

    def test_palette_detects_gbk_and_falls_back_to_ascii(self):
        old = sys.stdout
        sys.stdout = self._gbk_stdout()
        try:
            pal = cli._Palette(enabled=False)
        finally:
            sys.stdout = old
        self.assertFalse(pal.unicode, "GBK 控制台不应启用 emoji/箭头")
        self.assertEqual(pal.icon("ok"), "[OK]")
        self.assertEqual(pal.bullet(), ">")
        self.assertEqual(pal.arrow(), "->")
        self.assertIn("====", pal.rule())

    def test_echo_safe_degrades_unencodable_text(self):
        buf = self._gbk_stdout()
        old = sys.stdout
        sys.stdout = buf
        try:
            cli._echo_safe("▸ 中文 ✅ emoji")   # GBK 装不下 ▸ 与 ✅
            buf.flush()
            written = buf.buffer.getvalue()
        finally:
            sys.stdout = old
        self.assertIn("中文".encode("gbk"), written)

    def test_print_report_survives_gbk_console(self):
        buf = self._gbk_stdout()
        old = sys.stdout
        sys.stdout = buf
        try:
            state = cli._print_report(
                _sample_report({"a": "ok", "b": "warn"}), cli._Palette(enabled=False), set(), True
            )
            buf.flush()
            written = buf.buffer.getvalue().decode("gbk")
        finally:
            sys.stdout = old
        self.assertEqual(state, "issues")
        self.assertIn("诊断结论", written)


class CliBehaviorTest(unittest.TestCase):
    """CLI 参数与退出码语义（用核心替身，不需要 DLL）。"""

    def test_engine_error_exits_nonzero(self):
        core = _StubCore(report={
            "results": [], "summary": {"counts": {}, "problems": []},
            "error": "引擎内部 panic: boom", "platform": "windows", "duration_ms": 1.0,
        })
        code, out = _run_cli(["run", "-c", "databases", "--no-color"], core)
        self.assertEqual(code, 1, f"引擎异常必须非零退出；输出:\n{out}")
        self.assertIn("诊断引擎异常", out)

    def test_fail_status_exits_nonzero(self):
        core = _StubCore(report=_sample_report({"a": "fail"}))
        code, _ = _run_cli(["run", "-c", "databases", "--no-color"], core)
        self.assertEqual(code, 1)

    def test_ok_status_exits_zero(self):
        core = _StubCore(report=_sample_report({"a": "ok"}))
        code, _ = _run_cli(["run", "-c", "databases", "--no-color"], core)
        self.assertEqual(code, 0)

    def test_require_accepts_comma_and_repeat(self):
        core = _StubCore()
        _run_cli(
            ["run", "-c", "databases", "--no-color",
             "--require", "git,make", "--require", "node"],
            core,
        )
        self.assertEqual(core.last_config["required"], ["git", "make", "node"])

    def test_unknown_require_name_warns_instead_of_silently_ignoring(self):
        core = _StubCore()
        _, out = _run_cli(
            ["run", "-c", "databases", "--no-color", "--require", "git,nosuchtool"], core
        )
        self.assertIn("nosuchtool", out)
        self.assertIn("将被忽略", out)

    def test_list_checks_covers_core_and_python(self):
        core = _StubCore()
        code, out = _run_cli(["--list-checks"], core)
        self.assertEqual(code, 0)
        self.assertIn("toolchains", out)          # 核心侧检查项
        self.assertIn("python.interpreter", out)  # Python 侧检查项

    def test_no_args_runs_diagnosis_instead_of_printing_help(self):
        core = _StubCore()
        _, out = _run_cli([], core)
        self.assertIn("正在运行诊断", out)
        self.assertIsNotNone(core.last_config, f"无参调用应当真的跑一轮诊断；实际输出:\n{out}")

    def test_timeout_has_lower_bound(self):
        core = _StubCore()
        code, out = _run_cli(["run", "-c", "databases", "--no-color", "--timeout", "0"], core)
        self.assertEqual(code, 2, f"--timeout 0 应被参数校验拒绝；输出:\n{out}")
        self.assertIsNone(core.last_config, "非法 --timeout 不该真的跑起来")


@skipUnless(sys.platform == "win32", "需要系统 DLL 模拟异构加载")
class CoreLoadRobustnessTest(unittest.TestCase):
    def test_loadable_dll_without_symbols_is_core_not_available(self):
        # kernel32 能加载但没有任何 envdoctor 符号：以前会漏成裸 AttributeError
        with self.assertRaises(CoreNotAvailable) as ctx:
            Core(path=r"C:\Windows\System32\kernel32.dll")
        self.assertIn("缺少导出符号", str(ctx.exception))

    def test_missing_file_reports_candidate_reason(self):
        missing = Path(tempfile.gettempdir()) / "envdoctor-no-such-core.dll"
        with self.assertRaises(CoreNotAvailable) as ctx:
            Core(path=missing)
        self.assertIn("文件不存在", str(ctx.exception))


@skipUnless(_dll_ready(), "Rust 核心 DLL 未构建，跳过 GBK 端到端测试")
class CliEncodingEndToEndTest(unittest.TestCase):
    """P0 端到端回归：强制子进程 stdout 用 GBK（中文 Windows 默认控制台/管道编码）。"""

    def test_gbk_pipe_completes_and_writes_exports(self):
        with tempfile.TemporaryDirectory() as tmp:
            out_json = Path(tmp) / "report.json"
            env = dict(os.environ)
            env["PYTHONIOENCODING"] = "gbk"
            env["PYTHONUTF8"] = "0"
            proc = subprocess.run(
                [sys.executable, "-m", "envdoctor", "run", "-c", "databases",
                 "--no-color", "--json", str(out_json)],
                capture_output=True, env=env, timeout=180,
            )
            stderr = proc.stderr.decode("utf-8", "replace")
            self.assertNotIn("UnicodeEncodeError", stderr, stderr)
            self.assertEqual(proc.returncode, 0, stderr)
            self.assertTrue(out_json.is_file(), "导出不得因打印失败而丢失")
            json.loads(out_json.read_text(encoding="utf-8"))


@skipUnless(_dll_ready(), "Rust 核心 DLL 未构建，跳过绑定测试")
class BindingTest(unittest.TestCase):
    def setUp(self):
        self.core = Core()

    def test_version_format(self):
        self.assertRegex(self.core.version(), r"^\d+\.\d+\.\d+$")

    def test_list_checks_exposes_core_checks(self):
        checks = self.core.list_checks()
        self.assertTrue(checks, "核心应导出检查项清单")
        for c in checks:
            for key in ("id", "title", "category", "platforms"):
                self.assertIn(key, c)
        self.assertTrue(any(c["id"] == "network.dns" for c in checks))

    def test_run_single_category(self):
        report = self.core.run({"categories": ["databases"], "timeout_secs": 5})
        self.assertIsNone(report.get("error"))
        self.assertGreater(len(report["results"]), 0)
        for r in report["results"]:
            self.assertEqual(r["category"], "databases")

    def test_bad_config_is_reported_not_silently_defaulted(self):
        # 类型写错必须报错；旧版会 unwrap_or_default() 静默跑全量
        report = self.core.run({"categories": ["databases"], "timeout_secs": -1})
        self.assertIsNotNone(report.get("error"))
        self.assertEqual(report["results"], [])

    def test_cancel_token_roundtrip(self):
        token = self.core.new_cancel_token()
        token.trigger()
        report = self.core.run({"categories": ["databases"]}, cancel=token)
        token.close()
        # 取消后所有结果应为 SKIP
        self.assertTrue(all(r["status"] == "skip" for r in report["results"]))


if __name__ == "__main__":
    unittest.main()
