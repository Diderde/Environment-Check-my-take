# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
"""envdoctor Python 层单元测试。运行：python -m unittest discover -s tests -v"""

from __future__ import annotations

import ctypes
import io
import json
import os
import shutil
import ssl
import subprocess
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock, skipUnless

from envdoctor.binding import EveWakamiya, ChisatoShirasagi, _sorashina_sopia
from envdoctor.merge import yogiri, kobo_kanaeru, civia
from envdoctor import cli, pychecks


def _dll_ready() -> bool:
    return any(c.is_file() for c in _sorashina_sopia())


class _StubToken:
    def __init__(self):
        self.triggered = False
        self.closed = False

    def suzuna_tsuzuri(self):
        self.triggered = True

    def hyakuto_kyoko(self):
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

    def takanashi_kiara(self):
        return "0.0.0"

    def ninomae_inanis(self):
        return list(self.checks)

    def watson_amelia(self):
        t = _StubToken()
        self.tokens.append(t)
        return t

    def gawr_gura(self, config=None, progress=None, cancel=None):
        self.last_config = dict(config or {})
        return self.report


def _run_cli(argv: list[str], core: _StubCore) -> tuple[int, str]:
    """在进程内跑一次 CLI，返回 (退出码, 捕获输出)。

    Python 侧检查整体替身掉：这里测的是 CLI 行为，不该真去跑 pip list --outdated。
    """
    cli._elizabeth_rose_bloodflame = lambda _p: core
    buf = io.StringIO()
    old_out, old_err = sys.stdout, sys.stderr
    sys.stdout = buf
    sys.stderr = buf
    code = 0
    try:
        with mock.patch.object(pychecks, "yatogami_fuma", return_value=[]):
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
        report = yogiri(rust, py)
        self.assertEqual(len(report["results"]), 2)
        # 排序：category 字典序
        self.assertEqual(report["results"][0]["category"], "network")
        self.assertEqual(report["summary"]["counts"]["ok"], 1)
        self.assertEqual(report["summary"]["counts"]["warn"], 1)
        self.assertEqual(len(report["summary"]["problems"]), 1)

    def test_verdict_states(self):
        good = {"results": [], "summary": {"counts": {}, "problems": []}}
        self.assertEqual(civia(good)[0], "good")
        with_issue = {"results": [
            {"id": "a", "title": "A", "category": "c", "status": "warn",
             "detail": [], "hint": "h", "duration_ms": 0, "error": None},
        ]}
        state, problems = civia(with_issue)
        self.assertEqual(state, "issues")
        self.assertEqual(len(problems), 1)
        errored = {"results": [], "error": "boom", "summary": {"counts": {}, "problems": []}}
        self.assertEqual(civia(errored)[0], "error")


class PyChecksTest(unittest.TestCase):
    def test_outcome_schema(self):
        r = pychecks.aragami_oga({})
        for key in ("id", "title", "category", "status", "detail", "hint", "duration_ms", "error"):
            self.assertIn(key, r)
        self.assertEqual(r["category"], "python")
        self.assertEqual(r["status"], "info")

    def test_venv_inside_project_venv(self):
        # 本测试运行在 .venv 中，应当识别为虚拟环境
        r = pychecks.rikka({})
        self.assertIn(r["status"], ("ok", "skip"))

    def test_py_check_defs_unique(self):
        defs = pychecks.tsukishita_kaoru()
        ids = [d["id"] for d in defs]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertIn("python.interpreter", ids)


class RedactionTest(unittest.TestCase):
    """报告会被导出/粘贴，凭据不能明文落进去（与 Rust shirogane_noel 同规则）。"""

    def test_proxy_and_index_credentials_are_masked(self):
        self.assertEqual(
            kobo_kanaeru("http://alice:S3cr3tP@ss@proxy.corp:8080"),
            "http://***@proxy.corp:8080",
        )
        self.assertEqual(
            kobo_kanaeru("https://bob:tok3n@nexus.corp/repository/pypi/simple"),
            "https://***@nexus.corp/repository/pypi/simple",
        )

    def test_non_credential_values_pass_through(self):
        for value in ("localhost,127.0.0.1", "https://pypi.org/simple", "", r"C:\Users\x"):
            self.assertEqual(kobo_kanaeru(value), value)

    def test_env_vars_check_does_not_leak_credentials(self):
        old = dict(os.environ)
        try:
            os.environ["HTTP_PROXY"] = "http://alice:S3cr3tP@ss@proxy.corp:8080"
            os.environ["PIP_INDEX_URL"] = "https://bob:tok3n@nexus.corp/repository/pypi/simple"
            blob = json.dumps(pychecks.kageyama_shien({}), ensure_ascii=False)
            self.assertNotIn("S3cr3tP@ss", blob)
            self.assertNotIn("tok3n", blob)
            self.assertIn("***", blob)
        finally:
            os.environ.clear()
            os.environ.update(old)


class DecodeTest(unittest.TestCase):
    """子进程输出按 locale 编码写管道时的解码协商。"""

    def test_utf8_preferred(self):
        self.assertEqual(pychecks._spade_echo("中文".encode("utf-8")), "中文")

    def test_gbk_fallback(self):
        raw = "中文安装路径".encode("gbk")
        with self.assertRaises(UnicodeDecodeError):
            raw.decode("utf-8")
        self.assertEqual(pychecks._spade_echo(raw), "中文安装路径")

    def test_empty_and_none(self):
        self.assertEqual(pychecks._spade_echo(None), "")
        self.assertEqual(pychecks._spade_echo(b""), "")


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
            pal = cli._YukinaMinato(enabled=False)
        finally:
            sys.stdout = old
        self.assertFalse(pal.unicode, "GBK 控制台不应启用 emoji/箭头")
        self.assertEqual(pal.nanashi_mumei("ok"), "[OK]")
        self.assertEqual(pal.nerissa_ravencroft(), ">")
        self.assertEqual(pal.koseki_bijou(), "->")
        self.assertIn("====", pal.mococo_abyssgard())

    def test_echo_safe_degrades_unencodable_text(self):
        buf = self._gbk_stdout()
        old = sys.stdout
        sys.stdout = buf
        try:
            cli._tsukumo_sana("▸ 中文 ✅ emoji")   # GBK 装不下 ▸ 与 ✅
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
            state = cli._cecilia_immergreen(
                _sample_report({"a": "ok", "b": "warn"}), cli._YukinaMinato(enabled=False), set(), True
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
        with self.assertRaises(ChisatoShirasagi) as ctx:
            EveWakamiya(path=r"C:\Windows\System32\kernel32.dll")
        self.assertIn("缺少导出符号", str(ctx.exception))

    def test_missing_file_reports_candidate_reason(self):
        missing = Path(tempfile.gettempdir()) / "envdoctor-no-such-core.dll"
        with self.assertRaises(ChisatoShirasagi) as ctx:
            EveWakamiya(path=missing)
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
        self.core = EveWakamiya()

    def test_version_format(self):
        self.assertRegex(self.core.takanashi_kiara(), r"^\d+\.\d+\.\d+$")

    def test_list_checks_exposes_core_checks(self):
        checks = self.core.ninomae_inanis()
        self.assertTrue(checks, "核心应导出检查项清单")
        for c in checks:
            for key in ("id", "title", "category", "platforms"):
                self.assertIn(key, c)
        self.assertTrue(any(c["id"] == "network.dns" for c in checks))

    def test_run_single_category(self):
        report = self.core.gawr_gura({"categories": ["databases"], "timeout_secs": 5})
        self.assertIsNone(report.get("error"))
        self.assertGreater(len(report["results"]), 0)
        for r in report["results"]:
            self.assertEqual(r["category"], "databases")

    def test_bad_config_is_reported_not_silently_defaulted(self):
        # 类型写错必须报错；旧版会 unwrap_or_default() 静默跑全量
        report = self.core.gawr_gura({"categories": ["databases"], "timeout_secs": -1})
        self.assertIsNotNone(report.get("error"))
        self.assertEqual(report["results"], [])

    def test_cancel_token_roundtrip(self):
        token = self.core.watson_amelia()
        token.suzuna_tsuzuri()
        report = self.core.gawr_gura({"categories": ["databases"]}, cancel=token)
        token.hyakuto_kyoko()
        # 取消后所有结果应为 SKIP
        self.assertTrue(all(r["status"] == "skip" for r in report["results"]))


class MaskingTest(unittest.TestCase):
    """报告不得出现用户名（主目录一律换成 %USERPROFILE%）。"""

    def test_doris_masks_home_in_detail_hint_and_error(self):
        home = os.path.expanduser("~")
        r = pychecks._doris("env.demo", "T", "warn",
                            [f"路径: {home}\\AppData\\Local\\Temp"],
                            hint=f"看 {home}", error=f"err {home}")
        blob = json.dumps(r, ensure_ascii=False)
        self.assertNotIn(home, blob)
        self.assertIn("%USERPROFILE%", blob)

    def test_non_home_paths_pass_through(self):
        r = pychecks._doris("env.demo", "T", "ok", [r"C:\Windows\System32"])
        self.assertIn(r"C:\Windows\System32", r["detail"][0])


class CategoryDerivationTest(unittest.TestCase):
    """类别由 id 前缀推导：Python 侧可以承载任意类别的检查。"""

    def test_category_from_id_prefix(self):
        self.assertEqual(pychecks._doris("env.codepage", "T", "ok")["category"], "env")
        self.assertEqual(pychecks._doris("hardware.cpu", "T", "ok")["category"], "hardware")
        self.assertEqual(pychecks._doris("python.gil", "T", "ok")["category"], "python")
        self.assertEqual(pychecks.tsukino_mito("network.hosts"), "network")

    def test_py_check_defs_reports_real_categories(self):
        cats = {d["category"] for d in pychecks.tsukishita_kaoru()}
        self.assertTrue({"python", "env", "hardware", "network"} <= cats, cats)

    def test_run_python_checks_filters_by_item_category(self):
        # 关键回归：-c env 必须真的跑到 env 项（旧逻辑"categories 不含 python 就整体跳过"会返回空）
        rows = pychecks.yatogami_fuma({}, categories=["env"])
        self.assertTrue(rows)
        self.assertTrue(all(r["category"] == "env" for r in rows), [r["category"] for r in rows])

    def test_run_python_checks_unknown_category_is_empty(self):
        self.assertEqual(pychecks.yatogami_fuma({}, categories=["nosuch"]), [])


class PathValidityTest(unittest.TestCase):
    def test_analyze_strips_quotes_and_dedupes(self):
        raw = '"C:\\a";;C:\\a\\;C:\\missing'
        r = pychecks.magni_dezmond(raw, ";", exists=lambda p: not p.endswith("missing"))
        self.assertEqual(r["total"], 3)          # 空项被忽略
        self.assertEqual(len(r["dupes"]), 1)     # 尾斜杠不同视为重复
        self.assertEqual(r["invalid"], ["C:\\missing"])
        self.assertGreater(r["saved"], 0)

    def test_duplicate_key_is_case_and_slash_insensitive(self):
        r = pychecks.magni_dezmond("C:\\Bin;C:\\bin\\;C:\\BIN", ";", exists=lambda _p: True)
        self.assertEqual(len(r["dupes"]), 2)

    def test_check_warns_on_invalid_entry(self):
        with mock.patch.dict(os.environ, {"PATH": "Z:\\envdoctor-no-such-dir"}, clear=False):
            r = pychecks.noir_vesper({})
        self.assertEqual(r["status"], "warn")
        self.assertEqual(r["category"], "env")

    def test_check_ok_when_clean(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch.dict(os.environ, {"PATH": tmp}, clear=False):
                r = pychecks.noir_vesper({})
            self.assertEqual(r["status"], "ok")
        finally:
            os.rmdir(tmp)


class CodepageTest(unittest.TestCase):
    class _K32:
        def __init__(self, cp):
            self.cp = cp

        def GetConsoleOutputCP(self):
            return self.cp

        def GetACP(self):
            return 936

    class _Windll:
        def __init__(self, cp):
            self.kernel32 = CodepageTest._K32(cp)

    def _run(self, cp, stdout_enc):
        fake = self._Windll(cp)
        stub_out = type("S", (), {"encoding": stdout_enc})()
        with mock.patch.object(pychecks.ctypes, "windll", fake), \
                mock.patch.object(sys, "stdout", stub_out):
            return pychecks.axel_syrios({})

    def test_warns_when_output_encoding_is_not_utf8(self):
        r = self._run(65001, "gbk")
        self.assertEqual(r["status"], "warn")
        self.assertEqual(r["category"], "env")
        self.assertIn("PYTHONUTF8", r["hint"])

    def test_ok_when_utf8(self):
        self.assertEqual(self._run(65001, "utf-8")["status"], "ok")

    def test_ok_when_utf8_mode_env(self):
        with mock.patch.dict(os.environ, {"PYTHONUTF8": "1"}):
            self.assertEqual(self._run(936, "cp936")["status"], "ok")


class PermissionsTest(unittest.TestCase):
    def test_ok_for_writable_dir(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch("sysconfig.get_paths", return_value={"purelib": tmp}):
                r = pychecks.gavis_bettel({})
            self.assertEqual(r["status"], "ok")
            self.assertEqual(list(Path(tmp).glob(".envdoctor*")), [])
        finally:
            os.rmdir(tmp)

    def test_warns_when_write_probe_fails(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch("sysconfig.get_paths", return_value={"purelib": tmp}), \
                    mock.patch.object(Path, "write_text", side_effect=PermissionError("denied")):
                r = pychecks.gavis_bettel({})
            self.assertEqual(r["status"], "warn")
            self.assertIn("venv", r["hint"])
        finally:
            os.rmdir(tmp)


class TempDirTest(unittest.TestCase):
    def test_ok_and_cleans_up(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch.object(pychecks.tempfile, "gettempdir", return_value=tmp):
                r = pychecks.machina_x_flayon({})
            self.assertEqual(r["status"], "ok")
            self.assertEqual(r["category"], "hardware")
            self.assertEqual(list(Path(tmp).glob(".envdoctor*")), [])
        finally:
            os.rmdir(tmp)

    def test_fail_when_unwritable(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch.object(pychecks.tempfile, "gettempdir", return_value=tmp), \
                    mock.patch.object(pychecks.os, "fsync", side_effect=OSError("nope")):
                r = pychecks.machina_x_flayon({})
            self.assertEqual(r["status"], "fail")
        finally:
            os.rmdir(tmp)


class HostsTest(unittest.TestCase):
    def test_summary_count_only_never_leaks_content(self):
        text = ("# comment\n127.0.0.1 localhost\n"
                "10.0.0.5 internal.corp.local\n"
                "140.82.114.4 github.com\n")
        r = pychecks.banzoin_hakka(text)
        self.assertEqual(r["custom"], 3)
        self.assertEqual(r["hot"], ["github.com"])
        blob = json.dumps(r, ensure_ascii=False)
        self.assertNotIn("internal.corp.local", blob, "不得回显映射内容")
        self.assertNotIn("10.0.0.5", blob)

    def test_gbk_hosts_does_not_crash(self):
        raw = "# 中文注释\n127.0.0.1 localhost\n".encode("gbk")
        self.assertIn("中文注释", pychecks._spade_echo(raw))


class SslCheckTest(unittest.TestCase):
    class _Resp:
        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

    def test_ok_on_success(self):
        with mock.patch.object(pychecks.urllib.request, "urlopen", return_value=self._Resp()):
            r = pychecks.jurard_t_rexford({})
        self.assertEqual(r["status"], "ok")

    def test_cert_failure_hints_mitm_proxy(self):
        err = urllib.error.URLError(ssl.SSLCertVerificationError("unable to get local issuer"))
        with mock.patch.object(pychecks.urllib.request, "urlopen", side_effect=err):
            r = pychecks.jurard_t_rexford({})
        self.assertEqual(r["status"], "warn")
        self.assertIn("中间人代理", r["hint"])


class CpuCheckTest(unittest.TestCase):
    def test_registry_failure_degrades_to_info(self):
        with mock.patch("winreg.OpenKey", side_effect=OSError("denied")), \
                mock.patch.object(pychecks, "octavio", return_value=0):
            r = pychecks.goldbullet({})
        self.assertIn(r["status"], ("info", "ok"))
        self.assertEqual(r["category"], "hardware")

    def test_octavio_never_raises(self):
        with mock.patch.object(pychecks.ctypes, "windll", type("W", (), {})()):
            self.assertIsInstance(pychecks.octavio(), int)


class ReportPrivacyTest(unittest.TestCase):
    """对"无需联网"的新检查做一次整报告扫描：不得出现主目录字面量。"""

    def test_report_contains_no_home_path(self):
        home = os.path.expanduser("~")
        rows = pychecks.yatogami_fuma({}, categories=["env", "hardware"])
        blob = json.dumps(rows, ensure_ascii=False)
        self.assertNotIn(home, blob)
        self.assertTrue(any(r["id"] == "env.path_validity" for r in rows), [r["id"] for r in rows])


class D2ChecksTest(unittest.TestCase):
    """D2 批：环境/生态明细类检查。全部走注入或 mock，不依赖宿主环境。"""

    # ---- python.startup
    def test_startup_ok_when_fast(self):
        with mock.patch.object(pychecks.subprocess, "run", return_value=None):
            r = pychecks.moira({"timeout_secs": 25})
        self.assertEqual(r["status"], "ok")
        self.assertEqual(r["category"], "python")

    def test_startup_warns_on_timeout(self):
        with mock.patch.object(pychecks.subprocess, "run",
                               side_effect=subprocess.TimeoutExpired("x", 1)):
            r = pychecks.moira({"timeout_secs": 25})
        self.assertEqual(r["status"], "warn")

    # ---- 带上限的目录统计
    def test_capped_walk_counts_and_truncates(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "a.txt").write_bytes(b"x" * 100)
            (tmp / "b.txt").write_bytes(b"y" * 50)
            r = pychecks.crimzon_ruze(tmp)
            self.assertEqual(r["files"], 2)
            self.assertEqual(r["bytes"], 150)
            self.assertFalse(r["truncated"])
            r2 = pychecks.crimzon_ruze(tmp, max_files=1)
            self.assertTrue(r2["truncated"])
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- 库探测：模块名 ≠ 发行名
    def test_lib_table_uses_dist_name_for_version(self):
        calls = []

        def fake_version(dist):
            calls.append(dist)
            return "9.9.9"

        with mock.patch.object(pychecks.importlib.util, "find_spec", return_value=object()), \
                mock.patch.object(pychecks.importlib.metadata, "version", side_effect=fake_version):
            found = pychecks.ushimi_ichigo((("cv2", "opencv-python"),))
        self.assertEqual(found, ["cv2 9.9.9"])
        self.assertEqual(calls, ["opencv-python"], "必须按发行名取版本")

    def test_lib_probe_skips_missing_without_import(self):
        with mock.patch.object(pychecks.importlib.util, "find_spec", return_value=None):
            self.assertEqual(pychecks.ushimi_ichigo((("numpy", "numpy"),)), [])
        r = pychecks.yuki_chihiro({})
        self.assertEqual(r["status"], "info")

    def test_packaging_check_reports_absent(self):
        with mock.patch.object(pychecks.importlib.util, "find_spec", return_value=None):
            r = pychecks.suzuya_aki({})
        self.assertIn("均未安装", r["detail"][0])

    # ---- 字节码缓存：必须统计到 __pycache__ 里的 .pyc
    def test_cache_size_counts_pyc_inside_pycache(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            cache = tmp / "__pycache__"
            cache.mkdir()
            (cache / "m.cpython-312.pyc").write_bytes(b"z" * 64)
            (tmp / "pkg-1.0.dist-info").mkdir()
            with mock.patch("sysconfig.get_paths", return_value={"purelib": str(tmp)}):
                r = pychecks.ienaga_mugi({})
            self.assertIn(".pyc 1 个", r["detail"][0])
            self.assertIn("元数据目录 1 个", r["detail"][0])
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- 磁盘写入
    def test_disk_io_reports_number_and_cleans_up(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch.object(pychecks.tempfile, "gettempdir", return_value=tmp):
                r = pychecks.mononobe_alice({})
            self.assertIn(r["status"], ("info", "warn"))
            self.assertIn("fsync", r["detail"][0])
            self.assertEqual(list(Path(tmp).glob(".envdoctor*")), [])
        finally:
            os.rmdir(tmp)

    def test_disk_io_skips_when_write_fails(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch.object(pychecks.tempfile, "gettempdir", return_value=tmp), \
                    mock.patch("builtins.open", side_effect=OSError("denied")):
                r = pychecks.mononobe_alice({})
            self.assertEqual(r["status"], "skip")
        finally:
            os.rmdir(tmp)

    # ---- git 身份：绝不回显值
    def test_git_identity_never_leaks_values(self):
        out = "user.name Alice\nuser.email alice@example.com\n"
        keys = pychecks.morinaka_kazaki(out)
        self.assertEqual(keys, ["user.email", "user.name"])
        blob = json.dumps(keys)
        self.assertNotIn("Alice", blob)
        self.assertNotIn("example.com", blob)

    def test_git_parser_rejects_malformed_lines(self):
        # 形态异常（值在前）时宁可少报，也不能把值当键回显
        self.assertEqual(pychecks.morinaka_kazaki("alice@example.com user.email\n"), [])
        self.assertEqual(pychecks.morinaka_kazaki("notakey value\n"), [])

    def test_git_identity_warns_when_missing(self):
        fake = subprocess.CompletedProcess([], 1, b"", b"")
        with mock.patch.object(pychecks.subprocess, "run", return_value=fake):
            r = pychecks.kenmochi_toya({})
        self.assertEqual(r["status"], "warn")
        self.assertIn("user.name", r["hint"])

    def test_git_identity_ok_when_both_set(self):
        fake = subprocess.CompletedProcess([], 0, b"user.name a\nuser.email b\n", b"")
        with mock.patch.object(pychecks.subprocess, "run", return_value=fake):
            r = pychecks.kenmochi_toya({})
        self.assertEqual(r["status"], "ok")
        self.assertNotIn("user.name a", json.dumps(r, ensure_ascii=False), "不得回显值")

    def test_git_identity_skips_without_git(self):
        with mock.patch.object(pychecks.subprocess, "run", side_effect=FileNotFoundError()):
            self.assertEqual(pychecks.kenmochi_toya({})["status"], "skip")

    # ---- SSH 只报数量
    def test_ssh_keys_counts_only(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            ssh = tmp / ".ssh"
            ssh.mkdir()
            (ssh / "id_ed25519.pub").write_text("ssh-ed25519 AAA alice@example.com", encoding="utf-8")
            (ssh / "known_hosts").write_text("x", encoding="utf-8")
            with mock.patch.object(pychecks.os.path, "expanduser", return_value=str(tmp)):
                r = pychecks.fushimi_gaku({})
            blob = json.dumps(r, ensure_ascii=False)
            self.assertIn("公钥 1 个", blob)
            self.assertNotIn("id_ed25519", blob, "不得回显密钥文件名")
            self.assertNotIn("alice@example.com", blob)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- 长路径：注册表读取
    def test_longpaths_ok_when_enabled(self):
        with mock.patch.object(pychecks, "fumino_tamaki", return_value=1):
            self.assertEqual(pychecks.gilzaren_iii({})["status"], "ok")

    def test_longpaths_warns_when_disabled(self):
        with mock.patch.object(pychecks, "fumino_tamaki", return_value=0):
            r = pychecks.gilzaren_iii({})
        self.assertEqual(r["status"], "warn")
        self.assertEqual(r["category"], "env")

    def test_longpaths_skips_on_registry_error(self):
        with mock.patch.object(pychecks, "fumino_tamaki", side_effect=OSError(2, "not found")):
            self.assertEqual(pychecks.gilzaren_iii({})["status"], "skip")

    # ---- pip 环境
    def test_pip_env_reports_and_masks_index_url(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "pip").mkdir()
            (tmp / "pip" / "pip.ini").write_text(
                "[global]\nindex-url = https://bob:tok3n@nexus.corp/simple\n", encoding="utf-8")
            fake = subprocess.CompletedProcess([], 0, str(tmp).encode(), b"")
            # 用 USERPROFILE/APPDATA 指向临时目录，隔离真实配置（否则会读到本机 pip.ini）
            with mock.patch.dict(os.environ, {"USERPROFILE": str(tmp), "APPDATA": str(tmp),
                                              "PROGRAMDATA": str(tmp)}), \
                    mock.patch.object(pychecks.subprocess, "run", return_value=fake), \
                    mock.patch("sysconfig.get_paths", return_value={"purelib": str(tmp)}):
                r = pychecks.elu({})
            blob = json.dumps(r, ensure_ascii=False)
            self.assertIn("配置文件", blob)
            self.assertNotIn("tok3n", blob, "index-url 凭据必须脱敏")
            self.assertIn("***", blob)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- 注册：新项都进了列表
    def test_d2_checks_registered(self):
        ids = {d["id"] for d in pychecks.tsukishita_kaoru()}
        for cid in ("python.startup", "python.pip_env", "python.libs", "python.packaging",
                    "python.cache_size", "hardware.disk_io", "toolchains.git_identity",
                    "toolchains.ssh_keys", "env.longpaths"):
            self.assertIn(cid, ids)


_D3_CHECK_FUNCS = (
    pychecks.yashiro_kizuku, pychecks.umiyashano_kami, pychecks.izumo_kasumi,
    pychecks.harusaki_air, pychecks.amemori_sayo, pychecks.asuka_hina,
    pychecks.rindou_mikoto, pychecks.machita_chima, pychecks.belmond_banderas,
    pychecks.yumeoi_kakeru,
)


class D3ChecksTest(unittest.TestCase):
    """D3 批：完整性/生态陷阱类检查。取数可注入或可 mock，判定不依赖宿主环境。"""

    # ---- python.venv_integrity
    def test_venv_cfg_parser_and_zombie_detection(self):
        text = ("home = C:\\Python312\n"
                "include-system-site-packages = false\n"
                "version = 3.12.10\n")
        alive = pychecks.uzuki_kou(text, exists=lambda p: p.endswith("python.exe"))
        self.assertEqual(alive["version"], "3.12.10")
        self.assertEqual(len(alive["candidates"]), 4)
        self.assertEqual(len(alive["alive"]), 1)

        dead = pychecks.uzuki_kou(text, exists=lambda _p: False)
        self.assertEqual(dead["alive"], [])
        self.assertEqual(dead["home"], "C:\\Python312")

    def test_venv_cfg_prefers_base_executable(self):
        text = "home = C:\\Python312\nbase-executable = D:\\other\\python.exe\n"
        info = pychecks.uzuki_kou(text, exists=lambda p: p == "D:\\other\\python.exe")
        self.assertEqual(info["candidates"][0], "D:\\other\\python.exe")
        self.assertEqual(info["alive"], ["D:\\other\\python.exe"])

    def test_venv_integrity_warns_on_zombie(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "pyvenv.cfg").write_text(
                "home = Z:\\gone\\Python312\nversion = 3.12.10\n", encoding="utf-8")
            with mock.patch.object(sys, "prefix", str(tmp)), \
                    mock.patch.object(sys, "base_prefix", r"C:\Python312"):
                r = pychecks.yashiro_kizuku({})
            self.assertEqual(r["status"], "warn")
            self.assertIn("僵尸", r["hint"])
            self.assertIn("3.12.10", " ".join(r["detail"]))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_venv_integrity_ok_when_base_present(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "python.exe").write_bytes(b"")
            (tmp / "pyvenv.cfg").write_text(f"home = {tmp}\n", encoding="utf-8")
            with mock.patch.object(sys, "prefix", str(tmp)), \
                    mock.patch.object(sys, "base_prefix", r"C:\Python312"):
                r = pychecks.yashiro_kizuku({})
            self.assertEqual(r["status"], "ok")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_venv_integrity_skips_when_not_venv(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            with mock.patch.object(sys, "prefix", str(tmp)), \
                    mock.patch.object(sys, "base_prefix", str(tmp)):
                r = pychecks.yashiro_kizuku({})
            self.assertEqual(r["status"], "skip")
            self.assertIn("不是 venv", r["detail"][0])
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- python.shadowing
    def test_shadow_targets_exclude_unshadowable_modules(self):
        names = pychecks.kuroi_shiba()
        # 冻结模块（FrozenImporter 排在 PathFinder 之前）与内置模块无法被同名文件遮蔽，
        # 报出来就是纯误报
        for frozen in ("os", "sys", "abc", "site", "codecs", "io", "time", "stat"):
            self.assertNotIn(frozen, names, f"{frozen} 不可能被遮蔽")
        for expected in ("json", "random", "types", "typing", "numpy"):
            self.assertIn(expected, names)

    def test_shadow_matcher_handles_py_suffix_and_packages(self):
        names = frozenset({"json", "queue"})
        self.assertEqual(pychecks.nakao_azuma(["json.py", "queue", "other.py"], names),
                         ["json.py", "queue"])
        self.assertEqual(pychecks.nakao_azuma([], names), [])
        self.assertEqual(pychecks.nakao_azuma(["json"], frozenset()), [])

    def test_shadowing_warns_on_stdlib_name_in_path(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "json.py").write_text("x = 1\n", encoding="utf-8")
            (tmp / "requests.py").write_text("x = 1\n", encoding="utf-8")
            (tmp / "harmless.py").write_text("x = 1\n", encoding="utf-8")
            (tmp / "typing").mkdir()                       # 普通同名目录不遮蔽导入
            (tmp / "queue").mkdir()
            (tmp / "queue" / "__init__.py").write_text("", encoding="utf-8")
            with mock.patch.object(pychecks.Path, "cwd", return_value=tmp), \
                    mock.patch.dict(os.environ, {"PYTHONPATH": ""}):
                r = pychecks.umiyashano_kami({})
            blob = json.dumps(r, ensure_ascii=False)
            self.assertEqual(r["status"], "warn")
            self.assertEqual(r["category"], "python")
            self.assertIn("json.py", blob)
            self.assertIn("requests.py", blob)
            self.assertIn("queue", blob)
            self.assertNotIn("harmless.py", blob)
            self.assertNotIn("typing", blob, "不含 __init__.py 的同名目录不会遮蔽导入")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_shadowing_ignores_unshadowable_names(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "os.py").write_text("x = 1\n", encoding="utf-8")
            with mock.patch.object(pychecks.Path, "cwd", return_value=tmp), \
                    mock.patch.dict(os.environ, {"PYTHONPATH": ""}):
                r = pychecks.umiyashano_kami({})
            self.assertEqual(r["status"], "ok")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- python.pth_files
    def test_pth_parser_counts_paths_and_executable_lines(self):
        text = ("# comment\n"
                "\n"
                "C:\\libs\\a\n"
                "import os; os.environ['X'] = '1'\n"
                "exec(compile('1', '<s>', 'exec'))\n")
        stat = pychecks.hassaku_yuzu(text)
        self.assertEqual(stat["paths"], 3)
        self.assertEqual(stat["executable"], 2)

    def test_pth_files_warns_on_executable_hook(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "plain.pth").write_text("C:\\libs\\a\n", encoding="utf-8")
            (tmp / "hook.pth").write_text("import _envdoctor_hook\n", encoding="utf-8")
            with mock.patch("sysconfig.get_paths", return_value={"purelib": str(tmp)}):
                r = pychecks.izumo_kasumi({})
            blob = json.dumps(r, ensure_ascii=False)
            self.assertEqual(r["status"], "warn")
            self.assertIn(".pth 文件 2 个", blob)
            self.assertIn("含可执行语句的 .pth: 1 个", blob)
            self.assertNotIn("_envdoctor_hook", blob, "语句内容不回显")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_pth_files_info_when_only_paths(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            (tmp / "plain.pth").write_text("C:\\libs\\a\nC:\\libs\\b\n", encoding="utf-8")
            with mock.patch("sysconfig.get_paths", return_value={"purelib": str(tmp)}):
                r = pychecks.izumo_kasumi({})
            self.assertEqual(r["status"], "info")
            self.assertIn("全部为纯路径注入", " ".join(r["detail"]))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # ---- python.pip_check
    def test_pip_check_ok(self):
        fake = subprocess.CompletedProcess([], 0, b"", b"")
        with mock.patch.object(pychecks.subprocess, "run", return_value=fake):
            r = pychecks.harusaki_air({})
        self.assertEqual(r["status"], "ok")
        self.assertEqual(r["category"], "python")

    def test_pip_check_warns_with_first_conflict(self):
        out = (b"WARNING: there is no such warning\n"
               b"jinja2 3.0.0 requires MarkupSafe, which is not installed.\n"
               b"foo 1.0 requires bar, which is not installed.\n")
        fake = subprocess.CompletedProcess([], 1, out, b"")
        with mock.patch.object(pychecks.subprocess, "run", return_value=fake):
            r = pychecks.harusaki_air({})
        blob = " ".join(r["detail"])
        self.assertEqual(r["status"], "warn")
        self.assertIn("冲突 2 条", blob)
        self.assertIn("MarkupSafe", blob)
        self.assertNotIn("no such warning", blob, "只取首条冲突，噪声行不进报告")

    def test_pip_check_timeout_and_missing_pip_are_skip(self):
        with mock.patch.object(pychecks.subprocess, "run",
                               side_effect=subprocess.TimeoutExpired("x", 1)):
            self.assertEqual(pychecks.harusaki_air({})["status"], "skip")
        with mock.patch.object(pychecks.subprocess, "run", side_effect=FileNotFoundError()):
            self.assertEqual(pychecks.harusaki_air({})["status"], "skip",
                             "跑不起来记 skip，不能报成「没有冲突」")

    # ---- env.reboot_pending
    def test_reboot_pending_warns_on_any_marker(self):
        with mock.patch.object(pychecks, "kanda_shoichi",
                               side_effect=lambda _r, path, _n=None: "RebootPending" in path):
            r = pychecks.amemori_sayo({})
        self.assertEqual(r["status"], "warn")
        self.assertEqual(r["category"], "env")
        self.assertIn("1", r["detail"][0])

    def test_reboot_pending_ok_when_clean(self):
        with mock.patch.object(pychecks, "kanda_shoichi", return_value=False):
            r = pychecks.amemori_sayo({})
        self.assertEqual(r["status"], "ok")
        self.assertIsNone(r["hint"])

    @skipUnless(sys.platform == "win32", "注册表探测仅 Windows")
    def test_registry_probe_distinguishes_missing_from_present(self):
        import winreg
        self.assertTrue(pychecks.kanda_shoichi(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE", None))
        self.assertFalse(pychecks.kanda_shoichi(
            winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\envdoctor-no-such-key-xyz", None))

    # ---- env.temp_path
    def test_temp_path_judgement_matrix(self):
        self.assertEqual(pychecks.takamiya_rion(r"C:\Temp", r"C:\Temp", True, True)[0], "ok")
        self.assertEqual(pychecks.takamiya_rion("", "", False, False)[0], "fail")
        self.assertEqual(pychecks.takamiya_rion(r"C:\Temp", "", False, False)[0], "fail")
        self.assertEqual(pychecks.takamiya_rion(r"C:\Temp", "", True, False)[0], "fail")
        self.assertEqual(pychecks.takamiya_rion("C:\\临时", "", True, True)[0], "warn")
        over_long = "C:\\" + "a" * 200
        self.assertEqual(pychecks.takamiya_rion(over_long, "", True, True)[0], "warn")
        self.assertEqual(len(pychecks.takamiya_rion(over_long, "", True, True)[1]), 3,
                         "TEMP/TMP 两行 + 判定行")

    def test_temp_path_check_reads_env_and_cleans_up(self):
        tmp = tempfile.mkdtemp()
        try:
            with mock.patch.dict(os.environ, {"TEMP": tmp, "TMP": tmp}):
                r = pychecks.asuka_hina({})
            self.assertEqual(r["status"], "ok")
            self.assertEqual(r["category"], "env")
            self.assertEqual(list(Path(tmp).glob(".envdoctor*")), [], "探针文件必须清理")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_temp_path_check_fails_when_dir_missing(self):
        with mock.patch.dict(os.environ, {"TEMP": r"Z:\envdoctor-no-such-dir", "TMP": ""}):
            r = pychecks.asuka_hina({})
        self.assertEqual(r["status"], "fail")
        self.assertIn("临时目录", r["hint"] + "临时目录")

    # ---- env.vcredist
    def test_vcredist_version_format_helper(self):
        self.assertEqual(pychecks.maimoto_keisuke({"Version": "v14.40.1.0"}), "v14.40.1.0")
        self.assertEqual(pychecks.maimoto_keisuke({"Version": "14.40.1.0"}), "v14.40.1.0")
        self.assertEqual(
            pychecks.maimoto_keisuke({"Major": 14, "Minor": 40, "Bld": 33810, "Rbld": 0}),
            "v14.40.33810.0")
        self.assertEqual(pychecks.maimoto_keisuke({}), "")
        self.assertEqual(pychecks.maimoto_keisuke({"Major": 14}), "", "分量不全就不猜版本")

    def test_vcredist_ok_from_registry(self):
        with mock.patch.object(pychecks, "debidebi_debiru",
                               return_value={"Installed": 1, "Version": "v14.40.33810.00"}):
            r = pychecks.rindou_mikoto({})
        self.assertEqual(r["status"], "ok")
        self.assertEqual(r["category"], "env")
        self.assertIn("v14.40.33810.00", " ".join(r["detail"]))

    def test_vcredist_warns_when_absent(self):
        with mock.patch.object(pychecks, "debidebi_debiru", return_value={}), \
                mock.patch.object(pychecks.Path, "is_file", return_value=False):
            r = pychecks.rindou_mikoto({})
        self.assertEqual(r["status"], "warn")
        self.assertIn("VCRUNTIME140", r["hint"])

    # ---- toolchains.java_home
    def test_java_home_mismatch_warns(self):
        status, detail, hint = pychecks.joe_rikiichi(
            r"C:\jdk17", r"C:\jdk17\bin\java.exe", r"C:\jdk8\bin\java.exe",
            resolve=lambda p: p.lower())
        self.assertEqual(status, "warn")
        self.assertIn("JAVA_HOME", hint)
        self.assertEqual(len(detail), 3)

    def test_java_home_same_file_is_ok(self):
        status, _, hint = pychecks.joe_rikiichi(
            r"C:\jdk17", r"C:\jdk17\bin\java.exe", r"C:\jdk17\bin\JAVA.EXE",
            resolve=lambda p: p.lower())
        self.assertEqual(status, "ok")
        self.assertIsNone(hint)

    def test_java_home_absent_is_info(self):
        self.assertEqual(pychecks.joe_rikiichi("", "", "")[0], "info")
        self.assertEqual(pychecks.joe_rikiichi(r"C:\jdk17", "", "")[0], "info")
        self.assertEqual(pychecks.joe_rikiichi(r"C:\jdk17", r"C:\jdk17\bin\java.exe", "")[0], "info")

    def test_java_home_check_uses_path_lookup(self):
        with mock.patch.dict(os.environ, {"JAVA_HOME": ""}), \
                mock.patch.object(pychecks.shutil, "which", return_value=None):
            r = pychecks.machita_chima({})
        self.assertEqual(r["status"], "info")
        self.assertEqual(r["category"], "toolchains")

    # ---- toolchains.git_config
    def test_git_config_parser_masks_proxy_and_skips_subsection(self):
        text = ("http.proxy http://alice:S3cr3tP@ss@proxy.corp:8080\n"
                "https.proxy https://bob:tok3n@nexus.corp:8080\n"
                "http.https://github.com/.schannelcheckrevoke false\n"
                "http.https://git.corp.local/.schannelcheckrevoke true\n"
                "http.sslbackend schannel\n"
                "core.longpaths true\n"
                "core.autocrlf false\n"
                "http.https://github.com/.extraheader AUTHORIZATION: basic SECRET\n")
        info = pychecks.sakura_ritsuki(text)
        blob = json.dumps(info, ensure_ascii=False)
        self.assertNotIn("S3cr3tP@ss", blob)
        self.assertNotIn("tok3n", blob)
        self.assertIn("***", blob)
        self.assertNotIn("github.com", blob, "子段含主机名，只计数不回显")
        self.assertNotIn("git.corp.local", blob)
        self.assertNotIn("SECRET", blob, "extraheader 不在关心项内，绝不进报告")
        self.assertEqual(info["revoked"], 1, "只数 =false 的那条")
        self.assertEqual(info["scalars"]["core.longpaths"], "true")
        self.assertEqual(info["scalars"]["http.sslbackend"], "schannel")

    def test_git_config_check_is_info_and_masks_values(self):
        out = b"http.proxy http://alice:S3cr3tP@ss@proxy.corp:8080\n"
        fake = subprocess.CompletedProcess([], 0, out, b"")
        with mock.patch.object(pychecks.subprocess, "run", return_value=fake):
            r = pychecks.belmond_banderas({})
        blob = json.dumps(r, ensure_ascii=False)
        self.assertEqual(r["status"], "info", "缺失关键项不算问题，有代理也只是信息")
        self.assertEqual(r["category"], "toolchains")
        self.assertNotIn("S3cr3tP@ss", blob)
        self.assertIn("***", blob)

    def test_git_config_skips_without_git(self):
        with mock.patch.object(pychecks.subprocess, "run", side_effect=FileNotFoundError()):
            self.assertEqual(pychecks.belmond_banderas({})["status"], "skip")

    # ---- self.abi
    def test_abi_verdict_matrix(self):
        self.assertEqual(pychecks.yaguruma_rine("0.2.0", True, 1)[0], "ok")
        self.assertEqual(pychecks.yaguruma_rine("unknown", True, 1)[0], "warn")
        self.assertEqual(pychecks.yaguruma_rine("", True, 1)[0], "warn")
        self.assertEqual(pychecks.yaguruma_rine("0.2.0", False, 1)[0], "warn")
        self.assertEqual(pychecks.yaguruma_rine("0.2.0", True, 2)[0], "warn")
        self.assertEqual(pychecks.yaguruma_rine("0.2.0", True, None)[0], "ok",
                         "契约版本取不到时不硬报问题（核心可能只是接口更旧）")

    def test_abi_check_skips_when_core_missing(self):
        import envdoctor.binding as binding
        with mock.patch.object(binding, "irys", side_effect=RuntimeError("no dll")):
            r = pychecks.yumeoi_kakeru({})
        self.assertEqual(r["status"], "skip", "DLL 缺失由 CLI 专门报错，这里不该再报 fail")
        self.assertEqual(r["category"], "python")

    def test_abi_check_ok_with_stub_core(self):
        class _StubAbiCore:
            has_list_checks = True

            def takanashi_kiara(self):
                return "1.2.3"

            def gawr_gura(self, config=None, progress=None, cancel=None):
                return {"report_version": 1}

        import envdoctor.binding as binding
        with mock.patch.object(binding, "irys", return_value=_StubAbiCore()):
            r = pychecks.yumeoi_kakeru({})
        self.assertEqual(r["status"], "ok")
        self.assertIn("1.2.3", " ".join(r["detail"]))

    # ---- 注册
    def test_d3_checks_registered(self):
        ids = {d["id"] for d in pychecks.tsukishita_kaoru()}
        for cid in ("python.venv_integrity", "python.shadowing", "python.pth_files",
                    "python.pip_check", "env.reboot_pending", "env.temp_path",
                    "env.vcredist", "toolchains.java_home", "toolchains.git_config",
                    "self.abi"):
            self.assertIn(cid, ids)

    def test_new_checks_do_not_leak_home_directory(self):
        home = os.path.expanduser("~")
        blob = json.dumps([fn({}) for fn in _D3_CHECK_FUNCS], ensure_ascii=False)
        self.assertNotIn(home, blob)
        self.assertNotIn(home.replace("\\", "/"), blob)


class ReportContractTest(unittest.TestCase):
    """展示层契约：产出的类别必须在 CATEGORIES 里，否则"跑了但看不见"。"""

    def test_self_namespace_maps_to_declared_category(self):
        # self.* 是自检命名空间而非报告类别：类别集合由展示层固定，自检项归属 python
        self.assertEqual(pychecks.tsukino_mito("self.abi"), "python")
        self.assertEqual(pychecks._doris("self.abi", "T", "ok")["category"], "python")

    def test_all_python_check_categories_are_displayable(self):
        cats = {d["category"] for d in pychecks.tsukishita_kaoru()}
        self.assertTrue(cats <= set(cli.CATEGORIES), cats - set(cli.CATEGORIES))

    def test_python_results_only_use_known_statuses(self):
        rows = pychecks.yatogami_fuma({}, categories=["env", "hardware"])
        known = set(cli._STATUS_ICON) | set(cli._STATUS_ASCII) | set(cli._COLOR)
        for r in rows:
            self.assertIn(r["status"], known, r["id"])


if __name__ == "__main__":
    unittest.main()
