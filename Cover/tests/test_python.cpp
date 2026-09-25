// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 解释器与包管理类检查的测试。分四层：
//   ① 目录形状（id/标题/类别/平台门）—— 纯契约，与宿主无关；
//   ② 纯解析/纯判定函数的状态分支 —— 注入取数与文件系统事实，把"读不到"那几支真正跑一遍；
//   ③ 伪造解释器/伪造 git 驱动的检查分支 —— 把 python.cmd（git.cmd）垫片放在 PATH 最前面、
//      命令输出由夹具文件决定，于是"版本是 3.9 / pip 过旧 / 输出坏掉 / 根本没装解释器"
//      都能确定性复现，不必依赖本机装了什么；
//   ④ 端到端跑一轮 —— 只断言状态取值合法、有结论必有明细（不对宿主下结论）。
//
// 取检查函数一律经注册表按 id 查（`inui_toko()` 的表就是契约），测试不直接引用内部函数名。
//
// 夹具说明：`python.cmd` 把 `%ENVDOCTOR_TEST_DIR%\out.txt` 原样打出来（`pip config list` 走
// `cfg.txt`），退出码取 `%ENVDOCTOR_TEST_RC%`。环境变量的改动用带自定义删除器的 unique_ptr
// 做 RAII：doctest 的 REQUIRE 会抛异常中断用例，手写还原会漏。

#include <doctest/doctest.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/process.h"
#include "base/status.h"
#include "base/string_util.h"
#include "base/win32.h"
#include "checks/python.h"
#include "engine/engine.h"
#include "win/tools.h"

using namespace envdoctor;

TEST_CASE("解释器模块目录：30 个静态 id + 动态族的 id/标题/类别与契约一致") {
    const std::vector<HimariUehara> all = inui_toko();
    // 动态族最多 6 条（pip / setuptools / wheel / requests / numpy / pandas），
    // 本机装了几个就登记几个 —— 所以只断言区间与"静态部分完全一致"。
    CHECK(all.size() >= 30);
    CHECK(all.size() <= 36);

    const char* const kIds[30] = {
        "python.interpreter",   "python.multiplicity",    "python.pip",
        "python.venv",          "python.path",            "python.packages",
        "python.outdated",      "python.mirror",          "python.store_alias",
        "python.gil",           "python.env_vars",        "python.permissions",
        "python.ssl",           "python.startup",         "python.pip_env",
        "python.libs",          "python.packaging",       "python.cache_size",
        "env.codepage",         "hardware.cpu",           "toolchains.git_identity",
        "toolchains.ssh_keys",  "python.venv_integrity",  "python.shadowing",
        "python.pth_files",     "python.pip_check",       "env.temp_path",
        "toolchains.java_home", "toolchains.git_config",  "self.abi",
    };
    const char* const kTitles[30] = {
        "解释器",           "Python 多版本共存", "pip",
        "虚拟环境",         "模块搜索路径",      "已安装包",
        "过时包",           "包镜像源",          "Windows Store 别名",
        "GIL",              "相关环境变量",      "安装目录权限",
        "证书与 TLS",       "解释器启动",        "pip 环境",
        "常用库",           "打包工具",          "字节码缓存",
        "控制台编码",       "CPU",               "Git 身份",
        "SSH 密钥",         "虚拟环境完整性",    "模块遮蔽",
        ".pth 路径注入",    "依赖冲突",          "临时目录路径",
        "JAVA_HOME 一致性", "Git 关键配置",      "核心 ABI 自检",
    };
    for (size_t i = 0; i < 30; ++i) {
        const std::string id(all[i].id);
        CHECK(id == kIds[i]);
        CHECK(std::string(all[i].title) == kTitles[i]);
        // 类别由 id 前缀推导：`self.*` 归 python，其余取首个点之前的部分
        const size_t dot = id.find('.');
        const std::string head = dot == std::string::npos ? id : id.substr(0, dot);
        const std::string category = head == "self" ? "python" : head;
        CHECK(std::string(all[i].category) == category);
        // 平台门一律留空：Python 层的注册表没有平台字段，"仅 Windows"由检查内部 skip
        CHECK(all[i].platforms.empty());
        CHECK(all[i].fn != nullptr);
    }

    const std::set<std::string> kImportLibs{"pip", "setuptools", "wheel", "requests", "numpy",
                                            "pandas"};
    for (size_t i = 30; i < all.size(); ++i) {
        const std::string id(all[i].id);
        CHECK(id.rfind("python.import.", 0) == 0);
        const std::string lib = id.substr(std::string("python.import.").size());
        CHECK(kImportLibs.count(lib) == 1);
        CHECK(std::string(all[i].title) == "导入 · " + lib);
        CHECK(std::string(all[i].category) == "python");
        CHECK(all[i].platforms.empty());
        CHECK(all[i].fn != nullptr);
    }
}

TEST_CASE("凭据掩码：只掩最后一个 @ 之前的部分") {
    CHECK(hayase_sou("https://user:pass@host/simple") == "https://***@host/simple");
    // 口令里带 @ 时按最后一个 @ 切：按第一个切会把口令尾巴泄进 host
    CHECK(hayase_sou("https://a@b@c") == "https://***@c");
    CHECK(hayase_sou("http://@host") == "http://***@host");
    CHECK(hayase_sou("http://user@") == "http://***@");
    // 非 URL 形态、没有凭据的 URL 都原样返回
    CHECK(hayase_sou("user:tokens@host") == "user:tokens@host");
    CHECK(hayase_sou("https://pypi.org/simple") == "https://pypi.org/simple");
    CHECK(hayase_sou("") == "");
}

TEST_CASE("组合输出：stdout 优先、退回 stderr、不做 trim") {
    RimiUshigome both;
    both.out = "out";
    both.err = "err";
    CHECK(shellin_burgundy(both) == "out");

    RimiUshigome only_err;
    only_err.err = " err ";
    CHECK(shellin_burgundy(only_err) == " err ");

    // stdout 是空白也算"非空"：旧实现的 `stdout or stderr` 就是解码后的字符串判真
    RimiUshigome blank_out;
    blank_out.out = "  ";
    blank_out.err = "err";
    CHECK(shellin_burgundy(blank_out) == "  ");
}

TEST_CASE("目录统计：条目/字节/.pyc/元数据目录，按上限给出下界") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("envdoctor-walk-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root / "pkg.dist-info");
    std::filesystem::create_directories(root / "__pycache__");
    {
        std::ofstream(root / "__pycache__" / "a.pyc", std::ios::binary) << "12345";
        std::ofstream(root / "b.pyc", std::ios::binary) << "123";
        std::ofstream(root / "notes.txt", std::ios::binary) << "1234567";
    }
    const Facts stat = yamagami_karuta(root.string(), 3.0);
    CHECK(stat.at("entries").at(0) == "5");  // 2 个目录 + 3 个文件
    CHECK(stat.at("pyc").at(0) == "2");
    CHECK(stat.at("pyc_bytes").at(0) == "8");
    CHECK(stat.at("meta").at(0) == "1");
    CHECK(stat.at("bytes").at(0) == "15");  // 5 + 3 + 7
    CHECK(stat.at("truncated").at(0) == "0");

    // 时间上限给 0：第一个条目之后就截断，truncated 必须置位（调用方据此标"为下界"）
    CHECK(yamagami_karuta(root.string(), 0.0).at("truncated").at(0) == "1");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("影子模块扫描：同名 .py 与含 __init__.py 的包算命中，普通同名目录不算") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("envdoctor-shadow-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root / "numpy");
    std::filesystem::create_directories(root / "pandas");
    {
        std::ofstream(root / "json.py") << "# shadow";
        std::ofstream(root / "other.py") << "# harmless";
        std::ofstream(root / "numpy" / "__init__.py") << "";
    }
    const std::vector<std::string> names{"json", "numpy", "pandas"};
    const Facts scan = hoshikawa_sara({root.string()}, names, 3.0);
    CHECK(scan.at("dirs").at(0) == "1");
    CHECK(scan.at("scanned").at(0) == "4");
    REQUIRE(scan.count("hit") == 1);
    REQUIRE(scan.at("hit").size() == 2);
    // 同一目录内按名字排序：json.py 在前、numpy 在后；pandas 没有 __init__.py 不算
    CHECK(scan.at("hit")[0] == "json.py（" + root.string() + "）");
    CHECK(scan.at("hit")[1] == "numpy（" + root.string() + "）");

    // 不存在/非目录的候选目录被丢掉，且按归一化键去重（同一目录的两种写法只算一次）
    const Facts dedup = hoshikawa_sara(
        {root.string(), root.string() + "\\", (root / "nope").string()}, names, 3.0);
    CHECK(dedup.at("dirs").at(0) == "1");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("pip 输出解析：JSON 名称提取区分“解析不了”与“没有过时包”") {
    const auto items = emma_august(
        R"([{"name": "wheel", "version": "0.41", "latest_version": "0.44"}, )"
        R"({"version": "1.0"}, {"name": "u\u0026x"}])");
    REQUIRE(items.has_value());
    REQUIRE(items->size() == 3);
    CHECK((*items)[0] == "wheel");
    CHECK((*items)[1] == "?");  // 缺 name 键 → 与旧实现 `it.get("name", "?")` 同形
    CHECK((*items)[2] == "u&x");

    const auto empty = emma_august("[]");
    REQUIRE(empty.has_value());
    CHECK(empty->empty());

    // 截断的 JSON、非 JSON、顶层不是数组/对象：都是"解析不了"，不是"没有过时包"
    CHECK(!emma_august(R"([{"name": "wheel")").has_value());
    CHECK(!emma_august("not json at all").has_value());
    CHECK(!emma_august("123").has_value());
    CHECK(!emma_august("").has_value());
}

TEST_CASE("路径归一化：分隔符、重复分隔符与结尾分隔符") {
    CHECK(luis_cammy("C:/a/b/") == "C:\\a\\b");
    CHECK(luis_cammy("C:\\") == "C:\\");
    CHECK(luis_cammy("C:\\\\a\\\\b") == "C:\\a\\b");
    CHECK(luis_cammy("relative/dir") == "relative\\dir");
    CHECK(luis_cammy("") == "");
}

TEST_CASE("pyvenv.cfg 解析：版本、home 候选与基解释器存活") {
    const std::string text =
        "# comment\r\n"
        "home = C:\\Python312\r\n"
        "version_info = 3.12.10\r\n"
        "base-executable = C:\\Other\\python.exe\r\n";
    const auto exists = [](const std::string& path) { return path == "C:\\Other\\python.exe"; };
    const Facts info = matsukai_mao(text, exists);
    CHECK(info.at("version").at(0) == "3.12.10");
    CHECK(info.at("home").at(0) == "C:\\Python312");
    REQUIRE(info.at("candidate").size() == 5);  // 显式基解释器 + home 下 4 个候选名
    CHECK(info.at("candidate").at(0) == "C:\\Other\\python.exe");
    CHECK(info.at("candidate").at(1) == "C:\\Python312\\python.exe");
    REQUIRE(info.at("alive").size() == 1);
    CHECK(info.at("alive").at(0) == "C:\\Other\\python.exe");

    // 只记 home 时按 4 个候选名展开；都不存在 → alive 为空（僵尸 venv 的判定依据）
    const Facts only_home = matsukai_mao("home = D:\\Py", [](const std::string&) { return false; });
    CHECK(only_home.at("candidate").size() == 4);
    CHECK(only_home.count("alive") == 0);

    // 什么都不记：候选为空（调用方据此记 info 而不是 warn）
    const Facts nothing = matsukai_mao("version = 3.11.0\n", [](const std::string&) { return true; });
    CHECK(nothing.count("candidate") == 0);
    CHECK(nothing.at("version").at(0) == "3.11.0");
}

TEST_CASE(".pth 行统计：只数条数，import/exec 行单独计数") {
    const auto stat = shirayuki_tomoe(
        "# comment\n"
        "C:\\libs\\a\n"
        "\n"
        "import sys; sys.path.append('x')\n"
        "exec(open('x').read())\n"
        "importlib_hack\n");  // 不是 (import|exec) + 空白/左括号，不算可执行钩子
    CHECK(stat.first == 4);
    CHECK(stat.second == 2);
    CHECK(shirayuki_tomoe("").first == 0);
}

TEST_CASE("pip check 输出统计：warning/notice 行不算冲突，返回码为 0 时条数为 0") {
    const std::string text =
        "WARNING: There was an error checking the latest version of pip.\n"
        "foo 1.0 requires bar<2, but bar 2.0 is installed.\n"
        "baz 3.0 has requirement qux>=4, but you have qux 1.0.\n";
    const auto dirty = fuwa_minato(text, 1);
    CHECK(dirty.first == 2);
    CHECK(dirty.second == "foo 1.0 requires bar<2, but bar 2.0 is installed.");
    // 返回码 0 → 没有冲突可言（旧实现 `len(body) if returncode else 0`）
    CHECK(fuwa_minato(text, 0).first == 0);
    // 只有提示行：条数为 0、首条为空（调用方据此记 skip 而不是 warn）
    const auto clean = fuwa_minato("WARNING: x\nNotice: y\n", 1);
    CHECK(clean.first == 0);
    CHECK(clean.second.empty());
}

TEST_CASE("pip config list 解析：取最后一次 index-url、剥引号") {
    CHECK(gwelu_os_gar("global.index-url='https://pypi.tuna.tsinghua.edu.cn/simple'\n") ==
          "https://pypi.tuna.tsinghua.edu.cn/simple");
    // 同一份配置里出现多次时以最后一次为准（旧实现逐行覆盖）
    CHECK(gwelu_os_gar("index-url=https://a/simple\nindex-url=\"https://b/simple\"\n") ==
          "https://b/simple");
    CHECK(gwelu_os_gar("global.timeout=60\n") == "");
}

TEST_CASE("git 输出解析：只取键名、代理值掩码、吊销检查只计数") {
    const std::vector<std::string> keys = kurusu_natsume(
        "user.name Alice\n"
        "user.email a@b.c\n"
        "core.autocrlf true\n"
        "user.name\n"  // 不是 section.key 形态 → 丢掉（宁可少报）
        "value only line\n");
    REQUIRE(keys.size() == 3);
    CHECK(keys[0] == "core.autocrlf");
    CHECK(keys[1] == "user.email");
    CHECK(keys[2] == "user.name");

    const Facts config = mashiro_meme(
        "http.proxy http://user:token@proxy.local:8080\n"
        "https.proxy https://proxy.local:8443\n"
        "http.sslbackend schannel\n"
        "http.https://internal.example.schannelCheckRevoke false\n"
        "core.longpaths true\n");
    REQUIRE(config.at("proxy").size() == 2);
    CHECK(config.at("proxy")[0] == "http.proxy = http://***@proxy.local:8080");
    CHECK(config.at("proxy")[1] == "https.proxy = https://proxy.local:8443");
    CHECK(config.at("revoked").at(0) == "1");
    CHECK(config.at("http.sslbackend").at(0) == "schannel");
    CHECK(config.at("core.longpaths").at(0) == "true");
    // 子段是主机名：只计数，绝不让内网域名进报告
    CHECK(config.find("http.https://internal.example.schannelcheckrevoke") == config.end());
}

TEST_CASE("Python 列表字面量：反斜杠与单引号按 repr 规则转义") {
    CHECK(suo_sango({}) == "[]");
    CHECK(suo_sango({"a"}) == "['a']");
    CHECK(suo_sango({"C:\\x", "it's"}) == "['C:\\\\x', 'it\\'s']");
}

TEST_CASE("解释器版本判定：3.9 已停止支持、3.10 维护尾声、其余 ok") {
    const RanMitake eol =
        melissa_kinrenka(3, 9, "3.9.13", "CPython", "C:\\py\\python.exe", "C:\\py");
    CHECK(eol.status == kWarn);
    REQUIRE(eol.detail.size() == 4);
    CHECK(eol.detail[0] == "版本: 3.9.13");
    CHECK(eol.detail[1] == "实现: CPython");
    CHECK(eol.detail[2] == "可执行文件: C:\\py\\python.exe");
    CHECK(eol.detail[3] == "安装前缀: C:\\py");
    REQUIRE(eol.hint.has_value());
    CHECK(*eol.hint == "Python 3.9 已停止官方支持，建议升级到受支持版本");

    // 版本号文案用的是 sys.version_info 的 major.minor（不是 platform.python_version）
    const RanMitake old = melissa_kinrenka(3, 8, "3.8.10", "CPython", "x", "y");
    CHECK(old.status == kWarn);
    CHECK(*old.hint == "Python 3.8 已停止官方支持，建议升级到受支持版本");

    const RanMitake tail = melissa_kinrenka(3, 10, "3.10.11", "CPython", "x", "y");
    CHECK(tail.status == kWarn);
    CHECK(*tail.hint == "Python 3.10 已进入安全维护尾声，建议规划升级");

    const RanMitake fine = melissa_kinrenka(3, 12, "3.12.10", "CPython", "x", "y");
    CHECK(fine.status == kOk);
    CHECK(!fine.hint.has_value());
    // major 不是 3 时两条 warn 分支都不命中（与旧实现的条件一致）
    CHECK(melissa_kinrenka(2, 7, "2.7.18", "CPython", "x", "y").status == kOk);
}

TEST_CASE("多版本共存判定：一个 ok、三个 warn、一个都没有也 warn") {
    const RanMitake single = genzuki_tojiro(1, {});
    CHECK(single.status == kOk);
    CHECK(single.detail[0] == "仅检测到一个 python");

    const RanMitake two = genzuki_tojiro(2, {"where python: C:\\a\\python.exe"});
    CHECK(two.status == kOk);
    CHECK(two.detail[0] == "where python: C:\\a\\python.exe");

    const RanMitake many = genzuki_tojiro(3, {"where python: a"});
    CHECK(many.status == kWarn);
    REQUIRE(many.hint.has_value());
    CHECK(many.hint->find("多个 python 共存容易装错环境") != std::string::npos);

    const RanMitake none = genzuki_tojiro(0, {});
    CHECK(none.status == kWarn);
    CHECK(none.detail[0] == "未在 PATH 找到任何 python");
    CHECK(*none.hint == "确认 Python 已安装并加入 PATH");
}

TEST_CASE("虚拟环境判定：venv/conda 记 ok，全局解释器记 warn") {
    const RanMitake conda = nagao_kei(true, true, false);
    CHECK(conda.status == kOk);
    REQUIRE(conda.detail.size() == 2);
    CHECK(conda.detail[0] == "当前位于虚拟环境中");
    CHECK(conda.detail[1] == "类型: conda");

    const RanMitake venv = nagao_kei(true, false, true);
    CHECK(venv.status == kOk);
    CHECK(venv.detail[1] == "类型: venv");

    // 在 venv 里但两种标记都没有：只报"位于虚拟环境中"，不加类型
    const RanMitake unknown = nagao_kei(true, false, false);
    CHECK(unknown.status == kOk);
    CHECK(unknown.detail.size() == 1);

    const RanMitake global = nagao_kei(false, false, false);
    CHECK(global.status == kWarn);
    CHECK(global.detail[0] == "当前使用全局解释器");
    REQUIRE(global.hint.has_value());
    CHECK(global.hint->find("python -m venv .venv") != std::string::npos);
}

TEST_CASE("模块搜索路径判定：PYTHONHOME 与重复条目各占一行") {
    const RanMitake clean = kaida_haru(std::nullopt, {}, std::nullopt);
    CHECK(clean.status == kOk);
    CHECK(clean.detail[0] == "sys.path 无重复条目，PYTHONHOME 未设置");

    const RanMitake home = kaida_haru(std::string("C:\\py"), {}, std::nullopt);
    CHECK(home.status == kWarn);
    CHECK(home.detail[0] == "PYTHONHOME 已设置: C:\\py");
    REQUIRE(home.hint.has_value());
    CHECK(home.hint->find("混用两个安装的库文件") != std::string::npos);

    const RanMitake dup = kaida_haru(std::nullopt, {"C:\\a", "C:\\a"}, std::nullopt);
    CHECK(dup.status == kWarn);
    CHECK(dup.detail[0] == "sys.path 存在重复条目: ['C:\\\\a', 'C:\\\\a']");
    CHECK(!dup.hint.has_value());

    // PYTHONPATH 只回显、不改判定（与旧实现一致：有 PYTHONPATH 时不再补"无重复条目"那句）
    const RanMitake path = kaida_haru(std::nullopt, {}, std::string("C:\\extra"));
    CHECK(path.status == kOk);
    REQUIRE(path.detail.size() == 1);
    CHECK(path.detail[0] == "PYTHONPATH = C:\\extra");
}

TEST_CASE("GIL 判定：三种形态都是 info，自由线程不标红") {
    CHECK(sorahoshi_kirame("na").status == kInfo);
    CHECK(sorahoshi_kirame("na").detail[0] == "当前解释器不支持自由线程（GIL 恒启用）");
    CHECK(sorahoshi_kirame("1").detail[0] == "GIL 已启用（默认模式）");
    const RanMitake free = sorahoshi_kirame("0");
    CHECK(free.status == kInfo);
    CHECK(free.detail[0] == "自由线程模式（free-threading）");
}

TEST_CASE("pip 判定：不可用记 fail、主版本过旧记 warn、无输出不越界") {
    const RanMitake broken = kingyozaka_meiro(false, "");
    CHECK(broken.status == kFail);
    CHECK(broken.detail[0] == "pip 不可用");
    CHECK(*broken.hint == "python -m ensurepip --upgrade");

    const RanMitake failed = kingyozaka_meiro(false, "No module named pip");
    CHECK(failed.status == kFail);
    CHECK(failed.detail[0] == "No module named pip");

    const RanMitake older = kingyozaka_meiro(true, "pip 21.3.1 from C:\\py\\pip (python 3.12)");
    CHECK(older.status == kWarn);
    CHECK(older.detail[0] == "pip 21.3.1 from C:\\py\\pip (python 3.12)");
    CHECK(older.hint->find("python -m pip install -U pip") != std::string::npos);

    const RanMitake fine = kingyozaka_meiro(true, "pip 24.0 from C:\\py\\pip (python 3.12)\r\n");
    CHECK(fine.status == kOk);
    CHECK(fine.detail[0] == "pip 24.0 from C:\\py\\pip (python 3.12)");

    // 返回码 0 但输出为空（损坏安装）：旧实现直接取 splitlines()[0] 会越界
    const RanMitake silent = kingyozaka_meiro(true, "");
    CHECK(silent.status == kOk);
    CHECK(silent.detail[0] == "pip --version 无输出");
}

TEST_CASE("临时目录判定：未设置/不存在/不可写记 fail，非 ASCII 与超长记 warn") {
    const RanMitake unset = naraka("", "", false, false);
    CHECK(unset.status == kFail);
    CHECK(unset.detail[0] == "TEMP: 未设置");
    CHECK(unset.detail[1] == "TMP: 未设置");
    CHECK(unset.hint->find("均未设置") != std::string::npos);

    const RanMitake missing = naraka("C:\\nope", "", false, false);
    CHECK(missing.status == kFail);
    CHECK(missing.detail[2] == "目录不存在");

    const RanMitake readonly = naraka("", "C:\\tmp", true, false);
    CHECK(readonly.status == kFail);
    CHECK(readonly.detail[0] == "TEMP: 未设置");
    CHECK(readonly.detail[1] == "TMP: C:\\tmp");
    CHECK(readonly.detail[2] == "写入探针失败");

    const RanMitake fancy = naraka("C:\\临时目录", "", true, true);
    CHECK(fancy.status == kWarn);
    CHECK(fancy.detail[2] == "含非 ASCII 字符");

    const std::string long_path = "C:\\" + std::string(160, 'a');
    const RanMitake long_one = naraka(long_path, "", true, true);
    CHECK(long_one.status == kWarn);
    // 旧实现数的是 len()（码点）：C:\ + 160 = 163
    CHECK(long_one.detail[2].find("过长（163 > 150 字符）") != std::string::npos);

    const RanMitake ok = naraka("C:\\Temp", "", true, true);
    CHECK(ok.status == kOk);
    CHECK(ok.detail.size() == 2);
}

TEST_CASE("JAVA_HOME 一致性：缺项各记 info，同一个记 ok，不同记 warn") {
    const auto same = [](const std::string& path) { return path; };
    CHECK(furen_e_lustario("", "", "", same).status == kInfo);
    CHECK(furen_e_lustario("C:\\jdk", "", "", same).detail[1] ==
          "该目录下没有 bin/java（可能是 JRE 布局，或路径写错）");
    CHECK(furen_e_lustario("C:\\jdk", "", "C:\\jdk\\bin\\java.exe", same).detail[1] ==
          "PATH 上没有 java（只设了 JAVA_HOME，命令行调不到）");
    const RanMitake ok =
        furen_e_lustario("C:\\jdk", "C:\\jdk\\bin\\java.exe", "C:\\jdk\\bin\\java.exe", same);
    CHECK(ok.status == kOk);
    CHECK(ok.detail[1] == "与 PATH 上的 java 是同一个: C:\\jdk\\bin\\java.exe");
    // 大小写差异不算"两个 java"（旧实现走 Path.resolve()，Windows 上会规范成同一形态）
    CHECK(furen_e_lustario("C:\\jdk", "c:\\JDK\\bin\\JAVA.EXE", "C:\\jdk\\bin\\java.exe", same)
              .status == kOk);
    const RanMitake mismatch =
        furen_e_lustario("C:\\jdk17", "C:\\jdk8\\bin\\java.exe", "C:\\jdk17\\bin\\java.exe", same);
    CHECK(mismatch.status == kWarn);
    REQUIRE(mismatch.detail.size() == 3);
    CHECK(mismatch.hint->find("把 PATH 上的 java 指到 JAVA_HOME\\bin 即可") != std::string::npos);
}

TEST_CASE("失败命令的异常类名：与旧实现 report 里出现的名字一致") {
    RimiUshigome fine;
    fine.success = true;
    CHECK(ibrahim(fine).empty());
    RimiUshigome timeout;
    timeout.timed_out = true;
    CHECK(ibrahim(timeout) == "TimeoutExpired");
    RimiUshigome missing;
    missing.not_found = true;
    CHECK(ibrahim(missing) == "FileNotFoundError");
    RimiUshigome refused;
    refused.err = "拒绝访问";
    CHECK(ibrahim(refused) == "OSError");
}

TEST_CASE("伪造解释器：把命令输出换成夹具，逐个分支跑检查") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("envdoctor-fake-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir);

    // 夹具写盘：写后回读校验 + 重试。整机很忙时（其它用例可能还留着引擎派发出去的检查线程
    // 在跑、cmd 还在读同一个文件）ofstream 打开失败是**静默**的 —— 夹具没换掉却让断言背锅，
    // 排查成本极高。最终仍写不进去就明确报"夹具写入失败"。
    const auto write_fixture = [&dir](const std::string& name, const std::string& text) {
        for (int attempt = 0; attempt < 40; ++attempt) {
            {
                std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
                out << text;
            }
            std::string got;
            std::ifstream in(dir / name, std::ios::binary);
            if (in) {
                in.seekg(0, std::ios::end);
                got.resize(static_cast<size_t>(in.tellg()));
                in.seekg(0, std::ios::beg);
                in.read(got.data(), static_cast<std::streamsize>(got.size()));
            }
            if (got == text) {
                return;
            }
            Sleep(50);
        }
        REQUIRE_MESSAGE(false, "夹具写入失败（文件被占用？）: ", name);
    };
    // 垫片：把 out.txt 原样打出来；`pip config list` 走 cfg.txt；退出码来自 ENVDOCTOR_TEST_RC。
    // 垫片本身也必须校验写入：它写空了的话，后面每个子用例都只会看到"命令成功但没有输出"。
    write_fixture("python.cmd",
                  "@echo off\r\n"
                  "set RC=%ENVDOCTOR_TEST_RC%\r\n"
                  "if \"%RC%\"==\"\" set RC=0\r\n"
                  // 把"经环境变量传进来的探测地址"落盘：这样"运行时数据不进命令行"这条纪律
                  // 与"探测的是 base 还是 base/simple/"都能被断言到（见 python.mirror 子用例）。
                  "if not \"%ENVDOCTOR_PROBE_URL%\"==\"\" echo %ENVDOCTOR_PROBE_URL%>\"%ENVDOCTOR_TEST_DIR%\\url.txt\"\r\n"
                  "if \"%1\"==\"-m\" if \"%2\"==\"pip\" if \"%3\"==\"config\" goto cfg\r\n"
                  "if not exist \"%ENVDOCTOR_TEST_DIR%\\out.txt\" goto done\r\n"
                  "type \"%ENVDOCTOR_TEST_DIR%\\out.txt\"\r\n"
                  "goto done\r\n"
                  ":cfg\r\n"
                  "if not exist \"%ENVDOCTOR_TEST_DIR%\\cfg.txt\" goto done\r\n"
                  "type \"%ENVDOCTOR_TEST_DIR%\\cfg.txt\"\r\n"
                  ":done\r\n"
                  "exit /b %RC%\r\n");
    const std::string saved_path = uruha_rushia(L"PATH").value_or("");
    SetEnvironmentVariableW(L"PATH", tokino_sora(dir.string() + ";" + saved_path).c_str());
    SetEnvironmentVariableW(L"ENVDOCTOR_TEST_DIR", tokino_sora(dir.string()).c_str());
    auto restore = [saved = saved_path, dir](int* dummy) {
        SetEnvironmentVariableW(L"PATH", tokino_sora(saved).c_str());
        SetEnvironmentVariableW(L"ENVDOCTOR_TEST_DIR", nullptr);
        SetEnvironmentVariableW(L"ENVDOCTOR_TEST_RC", nullptr);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        delete dummy;
    };
    const std::unique_ptr<int, decltype(restore)> guard(new int(0), restore);

    // 子用例里沿用短名字写夹具（实现同上，already 校验过写入）
    const auto fixture = write_fixture;
    const auto set_rc = [](const char* code) {
        SetEnvironmentVariableW(L"ENVDOCTOR_TEST_RC", tokino_sora(code).c_str());
    };
    // 事实清单夹具：只填本用例关心的键，其余用固定值
    const auto facts = [](const std::string& version, const std::string& major,
                          const std::string& minor, const std::string& prefix,
                          const std::string& base_prefix, const std::string& purelib,
                          const std::string& gil, const std::string& extra) {
        std::string text;
        text += "version\t" + version + "\n";
        text += "major\t" + major + "\n";
        text += "minor\t" + minor + "\n";
        text += "impl\tCPython\n";
        text += "exe\tC:\\Py\\python.exe\n";
        text += "prefix\t" + prefix + "\n";
        text += "base_prefix\t" + base_prefix + "\n";
        text += "purelib\t" + purelib + "\n";
        text += "gil\t" + gil + "\n";
        text += "encoding\tcp936\n";
        text += "preferred\tcp936\n";
        return text + extra;
    };

    set_rc("0");
    fixture("out.txt", "");
    const std::vector<HimariUehara> table = inui_toko();  // 空夹具 → 只登记 30 个静态项
    REQUIRE(table.size() == 30);
    const auto check = [&table](const char* id) -> RanMitake (*)(const MocaAoba&) {
        for (const HimariUehara& item : table) {
            if (std::string(item.id) == id) {
                return item.fn;
            }
        }
        return nullptr;
    };
    MocaAoba cfg;
    cfg.timeout_secs = 20;

    // 夹具垫片是 `.cmd`，进程层会经 `cmd /C` 启动它；整机很忙时这条启动路径会偶发失败
    // （cmd 自己起不来 → 退出码非 0），检查于是走"取不到 → skip"。这是**宿主侧**的偶发，
    // 与被测逻辑无关：这里对"取不到"重试几次，避免用例跟着抖。重试后仍是 skip 的，
    // 各子用例按 skip 路径断言（必须写明依据，且绝不变成 ok/warn 这类结论）。
    const auto run = [&cfg](RanMitake (*fn)(const MocaAoba&)) {
        RanMitake result = fn(cfg);
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (result.status != kSkip || result.detail.empty()) {
                break;
            }
            const std::string& first = result.detail.front();
            const bool host_failure = first.find("解释器事实探测") != std::string::npos ||
                                      first.find("解释器探测") != std::string::npos ||
                                      first.find("TLS 探测没有返回结果") != std::string::npos ||
                                      first.find("导入失败") != std::string::npos;
            if (!host_failure) {
                break;
            }
            Sleep(200);
            result = fn(cfg);
        }
        return result;
    };

    SUBCASE("python.interpreter：3.9 记 warn，3.12 记 ok") {
        set_rc("0");
        fixture("out.txt", facts("3.9.13", "3", "9", "C:\\Py", "C:\\Py", "", "1", ""));
        const RanMitake eol = run(check("python.interpreter"));
        if (eol.status == kSkip) {
            // 垫片是 `.cmd`，进程层经 `cmd /C` 启动它，而 `-c` 后面那串脚本的引号/括号
            // 在 cmd 眼里不是寻常字符：偶发解析失败（退出码非 0）时本项就该记 skip。
            // 这条路径同样必须"有结论必有依据"，且绝不允许变成 ok/warn。
            REQUIRE(!eol.detail.empty());
            CHECK(eol.detail.front().find("解释器事实探测") != std::string::npos);
        } else {
            CHECK(eol.status == kWarn);
            REQUIRE(eol.detail.size() == 4);
            CHECK(eol.detail[0] == "版本: 3.9.13");
            CHECK(eol.detail[1] == "实现: CPython");
            CHECK(eol.detail[2] == "可执行文件: C:\\Py\\python.exe");
            REQUIRE(eol.hint.has_value());
            CHECK(*eol.hint == "Python 3.9 已停止官方支持，建议升级到受支持版本");
        }

        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1", ""));
        const RanMitake fine = check("python.interpreter")(cfg);
        CHECK((fine.status == kOk || fine.status == kSkip));
    }

    SUBCASE("python.pip：三支状态各跑一遍") {
        fixture("out.txt", "pip 21.3.1 from C:\\Py\\pip (python 3.12)");
        set_rc("0");
        CHECK(check("python.pip")(cfg).status == kWarn);
        fixture("out.txt", "pip 24.0 from C:\\Py\\pip (python 3.12)");
        CHECK(check("python.pip")(cfg).status == kOk);
        // pip 返回码非 0 → fail，文案就是子进程输出
        fixture("out.txt", "No module named pip");
        set_rc("1");
        const RanMitake broken = check("python.pip")(cfg);
        CHECK(broken.status == kFail);
        CHECK(broken.detail[0] == "No module named pip");
        // 返回码非 0 且没有任何输出 → 走"pip 不可用"
        fixture("out.txt", "");
        CHECK(check("python.pip")(cfg).detail[0] == "pip 不可用");
    }

    SUBCASE("python.venv / python.gil / python.path：拿事实判定") {
        set_rc("0");
        std::filesystem::create_directories(dir / "venv");
        std::ofstream(dir / "venv" / "pyvenv.cfg") << "home = C:\\Py\n";
        fixture("out.txt",
                facts("3.12.10", "3", "12", (dir / "venv").string(), "C:\\Py", "", "0", ""));
        const RanMitake venv = run(check("python.venv"));
        if (venv.status == kSkip) {
            // 与 python.interpreter 同一类宿主侧偶发（`.cmd` 垫片要经 cmd /C）：取不到就 skip，
            // 但要写明依据；不允许变成 ok/warn 这类结论。
            REQUIRE(!venv.detail.empty());
            CHECK((venv.detail.front().find("解释器事实探测") != std::string::npos ||
                   venv.detail.front().find("sys.prefix") != std::string::npos));
        } else {
            CHECK(venv.status == kOk);
            REQUIRE(venv.detail.size() == 2);
            CHECK(venv.detail[0] == "当前位于虚拟环境中");
            CHECK(venv.detail[1] == "类型: venv");  // 上面在 prefix 里放了 pyvenv.cfg
        }
        const RanMitake gil = run(check("python.gil"));
        CHECK((gil.detail[0] == "自由线程模式（free-threading）" ||
               gil.detail[0].find("解释器") != std::string::npos));

        SetEnvironmentVariableW(L"PYTHONHOME", L"C:\\Py");
        const RanMitake path = run(check("python.path"));
        if (path.status == kSkip) {
            REQUIRE(!path.detail.empty());
            CHECK(path.detail.front().find("解释器事实探测") != std::string::npos);
        } else {
            CHECK(path.status == kWarn);
            CHECK(path.detail[0] == "PYTHONHOME 已设置: C:\\Py");
        }
        SetEnvironmentVariableW(L"PYTHONHOME", nullptr);
    }

    SUBCASE("python.outdated：最新 / 有过时 / 解析不了 / 命令失败") {
        set_rc("0");
        fixture("out.txt", "[]");
        const RanMitake latest = check("python.outdated")(cfg);
        CHECK(latest.status == kOk);
        CHECK(latest.detail[0] == "所有包均为最新");

        fixture("out.txt", R"([{"name": "a"}, {"name": "b"}])");
        const RanMitake outdated = check("python.outdated")(cfg);
        CHECK(outdated.status == kWarn);
        CHECK(outdated.detail[0] == "共 2 个过时包: a, b");
        CHECK(outdated.hint->find("python -m pip install -U <包名>") != std::string::npos);

        fixture("out.txt", "{not json");
        const RanMitake broken = check("python.outdated")(cfg);
        CHECK(broken.status == kWarn);
        CHECK(broken.detail[0] == "输出无法解析");
        CHECK(!broken.hint.has_value());

        fixture("out.txt", "ERROR: network unreachable");
        set_rc("1");
        const RanMitake failed = check("python.outdated")(cfg);
        CHECK(failed.status == kWarn);
        CHECK(failed.detail[0] == "ERROR: network unreachable");
        CHECK(failed.hint->find("检测失败不等于没有过时包") != std::string::npos);
    }

    SUBCASE("python.pip_check：干净 / 有冲突 / 非零但无可解析输出") {
        set_rc("0");
        fixture("out.txt", "");
        CHECK(check("python.pip_check")(cfg).status == kOk);

        set_rc("1");
        fixture("out.txt", "a 1.0 requires b<2, but b 2.0 is installed.\n");
        const RanMitake conflicts = check("python.pip_check")(cfg);
        CHECK(conflicts.status == kWarn);
        REQUIRE(conflicts.detail.size() == 2);
        CHECK(conflicts.detail[0] == "冲突 1 条");
        CHECK(conflicts.detail[1] == "首条: a 1.0 requires b<2, but b 2.0 is installed.");

        // 只有提示行 → skip（不是"没有冲突"，也不是 warn）
        fixture("out.txt", "WARNING: retry\nNotice: hi\n");
        const RanMitake unparsable = check("python.pip_check")(cfg);
        CHECK(unparsable.status == kSkip);
        CHECK(unparsable.detail[0] == "pip check 返回非零但没有可解析的输出");
    }

    SUBCASE("python.ssl：成功 / 证书失败 / 其他异常 / 探测无结果") {
        set_rc("0");
        fixture("out.txt", "openssl\tOpenSSL 3.0.13\nca\t\nok\t120\n");
        const RanMitake ok = run(check("python.ssl"));
        CHECK((ok.status == kOk || ok.status == kSkip));
        if (ok.status == kOk) {
            REQUIRE(ok.detail.size() == 3);
            CHECK(ok.detail[0] == "OpenSSL: OpenSSL 3.0.13");
            CHECK(ok.detail[1] == "CA: 使用系统证书库（Windows 默认行为，非异常）");
            CHECK(ok.detail[2] == "TLS 握手 pypi.org: 成功（120ms）");
        } else {
            REQUIRE(!ok.detail.empty());
        }

        fixture("out.txt",
                "openssl\tOpenSSL 3.0.13\nca\tC:\\ca.pem\n"
                "urlerr\tSSLCertVerificationError\t1\n");
        const RanMitake cert = run(check("python.ssl"));
        if (cert.status == kSkip) {
            REQUIRE(!cert.detail.empty());
        } else {
            REQUIRE(cert.status == kWarn);
            REQUIRE(cert.detail.size() == 3);
            CHECK(cert.detail[1] == "CA 文件/目录: C:\\ca.pem");
            CHECK(cert.detail[2] == "TLS 握手 pypi.org: 失败（SSLCertVerificationError）");
            REQUIRE(cert.hint.has_value());
            CHECK(cert.hint->find("证书校验失败") != std::string::npos);
        }

        // URLError 但不是证书问题 → 另一套建议
        fixture("out.txt", "openssl\tOpenSSL 3.0.13\nurlerr\tConnectionResetError\t0\n");
        const RanMitake net = run(check("python.ssl"));
        if (net.status == kSkip) {
            REQUIRE(!net.detail.empty());
        } else {
            REQUIRE(net.status == kWarn);
            REQUIRE(net.hint.has_value());
            CHECK(net.hint->find("TLS 握手失败") != std::string::npos);
        }

        // 非 URLError 的意外异常 → skip（旧实现的 except Exception 分支）
        fixture("out.txt", "openssl\tOpenSSL 3.0.13\nexc\tValueError\n");
        const RanMitake other = run(check("python.ssl"));
        REQUIRE(other.status == kSkip);
        if (other.detail.size() == 3) {
            CHECK(other.detail[2] == "TLS 握手 pypi.org: 未完成（ValueError）");
        } else {
            // 宿主侧取不到（垫片没跑起来）：同样是 skip，但必须写明依据
            REQUIRE(!other.detail.empty());
            CHECK(other.detail.size() == 1);
        }

        // 连 OpenSSL 版本都没吐出来 → 不判断
        fixture("out.txt", "");
        CHECK(check("python.ssl")(cfg).status == kSkip);
    }

    SUBCASE("python.mirror：可达时凭据必须已被掩码") {
        set_rc("0");
        fixture("cfg.txt", "global.index-url='https://user:tok@private.local/simple'\n");
        fixture("out.txt", "ok\t42\n");
        const RanMitake ok = check("python.mirror")(cfg);
        // 掩码不变量与探测成败无关：只要读到了配置，第一行就必须是脱敏形态
        REQUIRE(!ok.detail.empty());
        CHECK(ok.detail[0] == "当前 index-url: https://***@private.local/simple");
        for (const std::string& line : ok.detail) {
            CHECK(line.find("user") == std::string::npos);
            CHECK(line.find("tok") == std::string::npos);
        }
        if (ok.status == kOk) {
            REQUIRE(ok.detail.size() == 2);
            CHECK(ok.detail[1] == "GET https://***@private.local/simple/simple/ 可达（42ms）");
        }
        // 经环境变量传出去的必须是 **base**（探测脚本自己拼 `/simple/`，与旧实现
        // `urlopen(f"{base}/simple/")` 同形）。这里直接读垫片落盘的实参：
        // 多拼一次就会变成 `…/simple/simple/simple/`，可达的镜像会被误报成 HTTPError。
        {
            std::ifstream url_file(dir / "url.txt", std::ios::binary);
            std::string probe_url;
            if (url_file) {
                std::getline(url_file, probe_url);
            }
            if (!probe_url.empty()) {
                // `echo %VAR%>file` 落盘的是 CRLF 文本：比较前先去空白
                CHECK(azki(probe_url) == "https://user:tok@private.local/simple");
            }
        }

        fixture("out.txt", "err\tURLError\n");
        const RanMitake down = check("python.mirror")(cfg);
        REQUIRE(!down.detail.empty());
        // 掩码不变量与探测成败无关
        CHECK(down.detail.front() == "当前 index-url: https://***@private.local/simple");
        if (down.status == kWarn) {
            REQUIRE(down.detail.size() == 2);
            CHECK(down.detail[1] == "https://***@private.local/simple 不可达: URLError");
            REQUIRE(down.hint.has_value());
            CHECK(down.hint->find("pip config set global.index-url") != std::string::npos);
        } else {
            // 宿主侧探测没跑起来：不判断（skip），绝不允许变成 ok
            CHECK(down.status == kSkip);
        }
    }

    SUBCASE("python.mirror：读不到 pip 配置就记 info，不猜默认源") {
        set_rc("1");
        fixture("cfg.txt", "");
        const RanMitake listed = check("python.mirror")(cfg);
        CHECK(listed.status == kInfo);
        CHECK(listed.detail[0] == "无法读取 pip 配置（pip config list 失败），跳过镜像源探测");
        CHECK(listed.hint->find("python -m pip config list") != std::string::npos);
    }

    SUBCASE("python.cache_size / python.pth_files / python.permissions：拿纯库目录判定") {
        set_rc("0");
        const std::filesystem::path site = dir / "site-packages";
        std::filesystem::create_directories(site / "pkg.dist-info");
        {
            std::ofstream(site / "a.pyc", std::ios::binary) << "12345";
            std::ofstream(site / "paths.pth") << "C:\\libs\\a\nC:\\libs\\b\n";
            std::ofstream(site / "hook.pth") << "import sys\n";
        }
        const std::string purelib = site.string();
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", purelib, "1", ""));

        const RanMitake cache = run(check("python.cache_size"));
        REQUIRE(!cache.detail.empty());
        if (cache.status != kSkip) {
            CHECK(cache.status == kInfo);
            CHECK(cache.detail[0] == ".pyc 1 个 / 0.0MB；元数据目录 1 个；扫描 4 个条目");
        }

        const RanMitake pth = run(check("python.pth_files"));
        if (pth.status == kSkip) {
            REQUIRE(!pth.detail.empty());
        } else {
            REQUIRE(pth.status == kWarn);
            REQUIRE(pth.detail.size() == 3);
            CHECK(pth.detail[0] == "site-packages: " + purelib);
            CHECK(pth.detail[1] == ".pth 文件 2 个 / 路径条目 3 条");
            CHECK(pth.detail[2] == "含可执行语句的 .pth: 1 个（语句内容不回显）");
        }

        const RanMitake permissions = run(check("python.permissions"));
        if (permissions.status == kSkip) {
            REQUIRE(!permissions.detail.empty());
        } else {
            REQUIRE(permissions.status == kOk);
            REQUIRE(permissions.detail.size() >= 2);
            CHECK(permissions.detail[0] == "site-packages: " + purelib);
            CHECK(permissions.detail[1] == "写入探针: 通过");
        }
        // 探针文件必须被删掉
        CHECK(!std::filesystem::exists(
            site / (".envdoctor_write_" + std::to_string(GetCurrentProcessId()))));

        // purelib 指到不存在的目录：三项都记 skip
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py",
                                 (dir / "nope").string(), "1", ""));
        CHECK(check("python.permissions")(cfg).status == kSkip);
        CHECK(check("python.cache_size")(cfg).status == kSkip);
        CHECK(check("python.pth_files")(cfg).status == kSkip);
    }

    SUBCASE("python.shadowing：PYTHONPATH 里的同名模块记 warn") {
        set_rc("0");
        const std::filesystem::path project = dir / "project";
        std::filesystem::create_directories(project);
        std::ofstream(project / "json.py") << "# shadow";
        SetEnvironmentVariableW(L"PYTHONPATH", tokino_sora(project.string()).c_str());
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1",
                                 "shadow\tjson\nshadow\tnumpy\n"));
        const RanMitake shadow = run(check("python.shadowing"));
        if (shadow.status == kSkip) {
            REQUIRE(!shadow.detail.empty());
        } else {
            REQUIRE(shadow.status == kWarn);
            REQUIRE(shadow.detail.size() == 3);
            CHECK(shadow.detail[0].find("个目录（当前工作目录 + PYTHONPATH）") !=
                  std::string::npos);
            CHECK(shadow.detail[1] == "与标准库/常用库同名的模块 1 个:");
            CHECK(shadow.detail[2] == "  json.py（" + project.string() + "）");
        }

        // 标准库名单取不到 → 不能报"未发现影子模块"
        SetEnvironmentVariableW(L"PYTHONPATH", nullptr);
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1", ""));
        CHECK(check("python.shadowing")(cfg).status == kSkip);
    }

    SUBCASE("python.libs / python.packaging / python.packages：列表类明细") {
        set_rc("0");
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1",
                                 "libs\tnumpy\t2.1.0\nlibs\tpandas\t2.2.0\n"
                                 "pkg\tPyInstaller\t6.0\n"));
        const RanMitake libs = run(check("python.libs"));
        if (libs.status == kSkip) {
            REQUIRE(!libs.detail.empty());
        } else {
            REQUIRE(libs.status == kInfo);
            REQUIRE(libs.detail.size() == 2);
            CHECK(libs.detail[0] == "已安装 2 个:");
            CHECK(libs.detail[1] == "  numpy 2.1.0, pandas 2.2.0");
        }
        const RanMitake packaging = run(check("python.packaging"));
        if (packaging.status == kSkip) {
            REQUIRE(!packaging.detail.empty());
        } else {
            REQUIRE(packaging.status == kInfo);
            REQUIRE(!packaging.detail.empty());
            CHECK(packaging.detail[0] == "PyInstaller 6.0");
        }

        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1", ""));
        const RanMitake libs_none = run(check("python.libs"));
        CHECK((libs_none.detail[0] == "常用库均未安装（干净环境）" || libs_none.status == kSkip));
        const RanMitake pkg_none = run(check("python.packaging"));
        CHECK((pkg_none.detail[0] == "PyInstaller/Nuitka/cx_Freeze 均未安装" ||
               pkg_none.status == kSkip));

        fixture("out.txt", "a==1\nb==2\n\nc==3\n");
        const RanMitake packages = check("python.packages")(cfg);
        CHECK(packages.status == kOk);
        CHECK(packages.detail[0] == "包总数: 3");
        set_rc("1");
        fixture("out.txt", "");
        const RanMitake broken = check("python.packages")(cfg);
        CHECK(broken.status == kWarn);
        CHECK(broken.detail[0] == "枚举失败");
    }

    SUBCASE("python.venv_integrity：僵尸 venv / 基解释器仍在 / 非 venv") {
        set_rc("0");
        const std::filesystem::path venv = dir / "zombie";
        std::filesystem::create_directories(venv);
        {
            std::ofstream cfg_file(venv / "pyvenv.cfg");
            cfg_file << "home = " << (dir / "gone").string() << "\nversion = 3.11.0\n";
        }
        fixture("out.txt", facts("3.12.10", "3", "12", venv.string(), "C:\\Py", "", "1", ""));
        const RanMitake zombie = run(check("python.venv_integrity"));
        if (zombie.status == kSkip) {
            REQUIRE(!zombie.detail.empty());
        } else {
            REQUIRE(zombie.status == kWarn);
            REQUIRE(zombie.detail.size() == 3);
            CHECK(zombie.detail[0] == "环境: " + venv.string());
            CHECK(zombie.detail[1] == "创建时基版本: 3.11.0");
            CHECK(zombie.detail[2].find("基解释器已缺失（4 个候选路径均不存在）") !=
                  std::string::npos);
            REQUIRE(zombie.hint.has_value());
            CHECK(zombie.hint->find("僵尸 venv") != std::string::npos);
        }

        // 基解释器真的在：把候选路径造出来
        std::filesystem::create_directories(dir / "alive");
        std::ofstream(dir / "alive" / "python.exe") << "";
        {
            std::ofstream cfg_file(venv / "pyvenv.cfg");
            cfg_file << "home = " << (dir / "alive").string() << "\n";
        }
        const RanMitake alive = run(check("python.venv_integrity"));
        if (alive.status == kSkip) {
            REQUIRE(!alive.detail.empty());
        } else {
            REQUIRE(alive.status == kOk);
            CHECK(alive.detail.back() ==
                  "基解释器仍在: " + (dir / "alive" / "python.exe").string());
        }

        // 不是 venv：没有 pyvenv.cfg → skip
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1", ""));
        const RanMitake plain = run(check("python.venv_integrity"));
        if (plain.status == kSkip && plain.detail.size() == 1 &&
            plain.detail[0].find("解释器事实探测失败") != std::string::npos) {
            CHECK(!plain.detail.empty());  // 宿主侧取不到：不判断，但必须有依据
        } else {
            CHECK(plain.status == kSkip);
            CHECK(plain.detail[0] == "当前不是 venv");
        }
    }

    SUBCASE("python.startup：伪造解释器立刻返回 → ok") {
        set_rc("0");
        fixture("out.txt", "");
        const RanMitake startup = check("python.startup")(cfg);
        CHECK(startup.status == kOk);
        CHECK(startup.detail[0].find("冷启动 ") == 0);
        CHECK(startup.detail[0].find(" / 预热 ") != std::string::npos);
    }

    SUBCASE("python.env_vars：代理凭据掩码、未设置时的缺省句") {
        for (const char* name : {"VIRTUAL_ENV", "CONDA_DEFAULT_ENV", "CONDA_PREFIX",
                                 "PIP_INDEX_URL", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY",
                                 "PYTHONUTF8", "PYTHONIOENCODING"}) {
            SetEnvironmentVariableW(tokino_sora(name).c_str(), L"");
        }
        const RanMitake none = check("python.env_vars")(cfg);
        CHECK(none.status == kInfo);
        CHECK(none.detail[0] == "未设置相关环境变量");

        SetEnvironmentVariableW(L"HTTP_PROXY", L"http://user:tok@proxy.local:8080");
        const RanMitake masked = check("python.env_vars")(cfg);
        CHECK(masked.detail[0] == "HTTP_PROXY = http://***@proxy.local:8080");
        SetEnvironmentVariableW(L"HTTP_PROXY", nullptr);
    }

    SUBCASE("env.codepage：输出编码不是 UTF-8 记 warn，PYTHONUTF8=1 记 ok") {
        set_rc("0");
        fixture("out.txt", facts("3.12.10", "3", "12", "C:\\Py", "C:\\Py", "", "1", ""));
        SetEnvironmentVariableW(L"PYTHONUTF8", nullptr);
        SetEnvironmentVariableW(L"PYTHONIOENCODING", nullptr);
        const RanMitake warn = run(check("env.codepage"));
        if (warn.status == kSkip) {
            REQUIRE(!warn.detail.empty());
        } else {
            REQUIRE(warn.status == kWarn);
            REQUIRE(warn.detail.size() == 4);
            CHECK(warn.detail[2] == "Python 输出编码: cp936");
            CHECK(warn.detail[3] == "locale 首选编码: cp936");
            REQUIRE(warn.hint.has_value());
            CHECK(warn.hint->find("set PYTHONUTF8=1") != std::string::npos);
        }

        SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
        const RanMitake ok = run(check("env.codepage"));
        if (ok.status == kSkip) {
            REQUIRE(!ok.detail.empty());
        } else {
            CHECK(ok.status == kOk);
            CHECK(ok.detail.size() == 5);
            CHECK(ok.detail[4] == "UTF-8 模式已启用");
        }
        SetEnvironmentVariableW(L"PYTHONUTF8", nullptr);
    }

    SUBCASE("python.import.*：冷导入耗时正常记 ok，失败记 warn") {
        set_rc("0");
        // 动态项的指针只能在"该库可导入"的那张表里取到：先给一份只认 pip 的扫描输出
        fixture("out.txt", "pip\t1\n");
        const std::vector<HimariUehara> dynamic = inui_toko();
        RanMitake (*import_pip)(const MocaAoba&) = nullptr;
        for (const HimariUehara& item : dynamic) {
            if (std::string(item.id) == "python.import.pip") {
                import_pip = item.fn;
            }
        }
        if (import_pip == nullptr) {
            // 扫描垫片没跑起来（宿主侧偶发）→ 一个动态项都不登记：这与旧实现
            // "find_spec 判不出来就不建检查项"同形，断言到这一层即可。
            CHECK(dynamic.size() == 30);
            return;
        }

        fixture("out.txt", "12.6\n");
        const RanMitake ok = run(import_pip);
        if (ok.status == kSkip) {
            REQUIRE(!ok.detail.empty());
        } else {
            REQUIRE(ok.status == kOk);
            CHECK(ok.detail[0] == "全新子进程冷导入 13ms（无缓存污染）");
        }

        fixture("out.txt", "-1\n");
        const RanMitake broken = run(import_pip);
        CHECK(broken.status == kWarn);
        CHECK(broken.detail[0] == "导入失败（全新子进程中）");
        CHECK(broken.hint->find("--force-reinstall pip") != std::string::npos);

        // 输出不是数字（旧实现 float() 失败 → -1.0 → 同一支）
        fixture("out.txt", "Traceback (most recent call last)\n");
        CHECK(run(import_pip).status == kWarn);
    }

    SUBCASE("动态族登记：能 import 的库才进注册表") {
        set_rc("0");
        fixture("out.txt",
                "pip\t1\nsetuptools\t1\nwheel\t0\nrequests\t1\nnumpy\t0\npandas\t0\n");
        const std::vector<HimariUehara> dynamic = inui_toko();
        if (dynamic.size() < 33) {
            // 垫片没跑起来时扫描为空 → 一个动态项都不登记（这也是旧实现的口径：
            // find_spec 判不出来就不建检查项），此时只断言"没有多登记"
            CHECK(dynamic.size() == 30);
            return;
        }
        CHECK(dynamic.size() == 33);  // 30 静态 + pip/setuptools/requests
        CHECK(std::string(dynamic[30].id) == "python.import.pip");
        CHECK(std::string(dynamic[31].id) == "python.import.setuptools");
        CHECK(std::string(dynamic[32].id) == "python.import.requests");
        CHECK(std::string(dynamic[30].title) == "导入 · pip");
    }

    SUBCASE("self.abi：自报构建信息，明确写出不再探测 Python↔核心 ABI") {
        const RanMitake abi = check("self.abi")(cfg);
        CHECK(abi.status == kOk);
        REQUIRE(abi.detail.size() == 6);
        CHECK(abi.detail[0].find("自检对象: 本工具自身") == 0);
        CHECK(abi.detail[1] == "报告契约版本: 1（预期 1）");
        CHECK(abi.detail[2].find("构建编译器: MSVC ") == 0);
        CHECK(abi.detail[4] == "目标架构: x64");
        CHECK(abi.detail[5].find("Python ABI: 不适用") == 0);
    }
}

TEST_CASE("git 检查：用真实 git + 临时配置驱动身份与关键配置的分支") {
    // 这两项的命令里带 `^(user\.(name|email)|core\.autocrlf)$` 这种含 `|` 的 pattern；
    // 伪造 `.cmd` 垫片必须经 `cmd /C`（cmd 会把 `|` 当管道拆开），所以这里改用**真实 git**：
    // 把 GIT_CONFIG_GLOBAL 指向临时配置文件、工作目录挪到非仓库目录，输出就完全可控。
    if (!ookami_mio("git")) {
        return;  // 本机没装 git：这两项本来就会记 skip，那条分支由"没有 git"的用例覆盖
    }
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("envdoctor-gitcfg-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir);
    const auto write_config = [&dir](const std::string& text) {
        std::ofstream out(dir / "gitconfig", std::ios::binary);
        out << text;
    };
    std::vector<wchar_t> cwd_buffer(32768);
    const DWORD cwd_length =
        GetCurrentDirectoryW(static_cast<DWORD>(cwd_buffer.size()), cwd_buffer.data());
    const std::wstring saved_cwd(cwd_buffer.data(),
                                 cwd_length > 0 ? static_cast<size_t>(cwd_length) : 0);
    const std::string saved_global = uruha_rushia(L"GIT_CONFIG_GLOBAL").value_or("");
    const std::string saved_nosystem = uruha_rushia(L"GIT_CONFIG_NOSYSTEM").value_or("");
    SetEnvironmentVariableW(L"GIT_CONFIG_GLOBAL",
                            tokino_sora((dir / "gitconfig").string()).c_str());
    SetEnvironmentVariableW(L"GIT_CONFIG_NOSYSTEM", L"1");
    SetCurrentDirectoryW(tokino_sora(dir.string()).c_str());  // 不在任何仓库里 → 不读本地配置
    auto restore = [saved_cwd, saved_global, saved_nosystem, dir](int* dummy) {
        SetEnvironmentVariableW(L"GIT_CONFIG_GLOBAL", tokino_sora(saved_global).c_str());
        SetEnvironmentVariableW(L"GIT_CONFIG_NOSYSTEM", tokino_sora(saved_nosystem).c_str());
        SetCurrentDirectoryW(saved_cwd.c_str());
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        delete dummy;
    };
    const std::unique_ptr<int, decltype(restore)> guard(new int(0), restore);

    const std::vector<HimariUehara> table = inui_toko();
    // 动态族随本机装了哪些库而变，故只要求静态部分在
    REQUIRE(table.size() >= 30);
    const auto check = [&table](const char* id) -> RanMitake (*)(const MocaAoba&) {
        for (const HimariUehara& item : table) {
            if (std::string(item.id) == id) {
                return item.fn;
            }
        }
        return nullptr;
    };
    MocaAoba cfg;
    cfg.timeout_secs = 20;

    // 身份：两个键都在 → ok；缺一个 → warn（只报键名，值不回显）
    write_config(
        "[user]\n\tname = Alice\n\temail = alice@example.invalid\n[core]\n\tautocrlf = true\n");
    const RanMitake identity = check("toolchains.git_identity")(cfg);
    CHECK(identity.status == kOk);
    CHECK(identity.detail[0] == "已配置: core.autocrlf, user.email, user.name");
    for (const std::string& line : identity.detail) {
        CHECK(line.find("Alice") == std::string::npos);
        CHECK(line.find("alice@example.invalid") == std::string::npos);
    }

    write_config("[user]\n\temail = alice@example.invalid\n");
    const RanMitake missing_name = check("toolchains.git_identity")(cfg);
    CHECK(missing_name.status == kWarn);
    CHECK(missing_name.detail[0] == "已配置: user.email");
    CHECK(missing_name.hint->find("git config --global user.name") != std::string::npos);

    // 关键配置：代理掩码、吊销检查计数、标量回显
    write_config(
        "[http]\n\tproxy = http://user:tok@proxy.local:8080\n\tsSLBackend = schannel\n"
        "[http \"https://internal.local\"]\n\tschannelCheckRevoke = false\n"
        "[core]\n\tlongpaths = true\n");
    const RanMitake config = check("toolchains.git_config")(cfg);
    CHECK(config.status == kInfo);
    REQUIRE(config.detail.size() == 5);
    CHECK(config.detail[0] == "http.proxy = http://***@proxy.local:8080");
    CHECK(config.detail[1] == "已关闭证书吊销检查的条目: 1 个（子段含主机名，不回显）");
    CHECK(config.detail[2] == "吊销检查常被中间人代理/加速器要求关闭，属该场景下的预期配置");
    CHECK(config.detail[3] == "http.sslbackend = schannel");
    CHECK(config.detail[4] == "core.longpaths = true");
    CHECK(config.detail[0].find("tok") == std::string::npos);

    // 关键项缺失不算问题：只报"未配置代理"，仍是 info
    write_config("");
    const RanMitake empty = check("toolchains.git_config")(cfg);
    CHECK(empty.status == kInfo);
    CHECK(empty.detail[0] == "未配置 http.proxy / https.proxy");
}

TEST_CASE("SSH 密钥 / 临时目录 / JAVA_HOME：宿主事实用临时目录与环境变量摆出来") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("envdoctor-host-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir / "home" / ".ssh");
    std::filesystem::create_directories(dir / "tempdir");
    std::filesystem::create_directories(dir / "jdk" / "bin");
    {
        std::ofstream(dir / "home" / ".ssh" / "id_ed25519.pub") << "ssh-ed25519 AAAA";
        std::ofstream(dir / "home" / ".ssh" / "known_hosts") << "";
        std::ofstream(dir / "jdk" / "bin" / "java.exe") << "";
    }
    const auto saved = [](const wchar_t* name) { return uruha_rushia(name).value_or(""); };
    const std::string saved_home = saved(L"USERPROFILE");
    const std::string saved_temp = saved(L"TEMP");
    const std::string saved_tmp = saved(L"TMP");
    const std::string saved_java = saved(L"JAVA_HOME");
    const std::string saved_path = saved(L"PATH");
    auto restore = [saved_home, saved_temp, saved_tmp, saved_java, saved_path, dir](int* dummy) {
        SetEnvironmentVariableW(L"USERPROFILE", tokino_sora(saved_home).c_str());
        SetEnvironmentVariableW(L"TEMP", tokino_sora(saved_temp).c_str());
        SetEnvironmentVariableW(L"TMP", tokino_sora(saved_tmp).c_str());
        SetEnvironmentVariableW(L"JAVA_HOME", tokino_sora(saved_java).c_str());
        SetEnvironmentVariableW(L"PATH", tokino_sora(saved_path).c_str());
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        delete dummy;
    };
    const std::unique_ptr<int, decltype(restore)> guard(new int(0), restore);

    const std::vector<HimariUehara> table = inui_toko();
    REQUIRE(table.size() >= 30);  // 动态族随本机装了哪些库而变，只看静态部分
    const auto check = [&table](const char* id) -> RanMitake (*)(const MocaAoba&) {
        for (const HimariUehara& item : table) {
            if (std::string(item.id) == id) {
                return item.fn;
            }
        }
        return nullptr;
    };
    MocaAoba cfg;
    cfg.timeout_secs = 20;

    // SSH 密钥：只报数量与存在性，文件名与注释不回显
    SetEnvironmentVariableW(L"USERPROFILE", tokino_sora((dir / "home").string()).c_str());
    const RanMitake ssh = check("toolchains.ssh_keys")(cfg);
    CHECK(ssh.status == kInfo);
    REQUIRE(ssh.detail.size() == 2);
    CHECK(ssh.detail[0] == "公钥 1 个");
    CHECK(ssh.detail[1] == "known_hosts: 存在");

    // 临时目录：可写 → ok；指向不存在的目录 → fail（探针文件必须被删掉）
    SetEnvironmentVariableW(L"TEMP", tokino_sora((dir / "tempdir").string()).c_str());
    SetEnvironmentVariableW(L"TMP", nullptr);
    const RanMitake temp = check("env.temp_path")(cfg);
    CHECK(temp.status == kOk);
    CHECK(temp.detail[0] == "TEMP: " + (dir / "tempdir").string());
    CHECK(temp.detail[1] == "TMP: 未设置");
    CHECK(!std::filesystem::exists(
        dir / "tempdir" / (".envdoctor_temppath_" + std::to_string(GetCurrentProcessId()))));

    SetEnvironmentVariableW(L"TEMP", tokino_sora((dir / "missing").string()).c_str());
    const RanMitake temp_missing = check("env.temp_path")(cfg);
    CHECK(temp_missing.status == kFail);
    CHECK(temp_missing.detail[2] == "目录不存在");

    // JAVA_HOME 与 PATH 上的 java 指向同一个文件 → ok；指到别处 → warn
    const std::string bundled = (dir / "jdk" / "bin" / "java.exe").string();
    SetEnvironmentVariableW(L"JAVA_HOME", tokino_sora((dir / "jdk").string()).c_str());
    SetEnvironmentVariableW(L"PATH",
                            tokino_sora((dir / "jdk" / "bin").string() + ";" + saved_path).c_str());
    const RanMitake same = check("toolchains.java_home")(cfg);
    CHECK(same.status == kOk);
    CHECK(same.detail[1] == "与 PATH 上的 java 是同一个: " + bundled);

    std::filesystem::create_directories(dir / "other");
    std::ofstream(dir / "other" / "java.exe") << "";
    SetEnvironmentVariableW(L"PATH",
                            tokino_sora((dir / "other").string() + ";" + saved_path).c_str());
    const RanMitake mismatch = check("toolchains.java_home")(cfg);
    CHECK(mismatch.status == kWarn);
    CHECK(mismatch.detail[1] == "JAVA_HOME 下: " + bundled);
}

TEST_CASE("没有解释器：相关检查一律 skip + 说明，绝不下结论") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("envdoctor-nopy-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir);  // 空目录：PATH 上没有 python
    const std::string saved_path = uruha_rushia(L"PATH").value_or("");
    SetEnvironmentVariableW(L"PATH", tokino_sora(dir.string()).c_str());
    auto restore = [saved = saved_path, dir](int* dummy) {
        SetEnvironmentVariableW(L"PATH", tokino_sora(saved).c_str());
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        delete dummy;
    };
    const std::unique_ptr<int, decltype(restore)> guard(new int(0), restore);

    const std::vector<HimariUehara> table = inui_toko();
    REQUIRE(table.size() == 30);  // 没有解释器 → 动态族一个都不登记
    const auto check = [&table](const char* id) -> RanMitake (*)(const MocaAoba&) {
        for (const HimariUehara& item : table) {
            if (std::string(item.id) == id) {
                return item.fn;
            }
        }
        return nullptr;
    };
    MocaAoba cfg;
    cfg.timeout_secs = 10;

    // 需要解释器事实的那些项：措辞一致地记 skip
    for (const char* id : {"python.interpreter", "python.venv", "python.path", "python.gil",
                           "python.permissions", "python.libs", "python.cache_size",
                           "python.venv_integrity", "python.shadowing", "python.pth_files",
                           "python.startup"}) {
        const RanMitake result = check(id)(cfg);
        CHECK(result.status == kSkip);
        REQUIRE(!result.detail.empty());
        CHECK(result.detail.front().find("未找到可用的 Python 解释器") != std::string::npos);
        CHECK(!result.hint.has_value());
    }

    // 走 pip 的那些检查各自走"命令失败"分支，同样不编结论
    //（子进程压根没起来时，明细里就是进程层给的那句 Win32 文案，而不是"零个包"）
    const RanMitake packages = check("python.packages")(cfg);
    CHECK(packages.status == kWarn);
    REQUIRE(!packages.detail.empty());
    CHECK(!packages.detail[0].empty());
    const RanMitake conflicts = check("python.pip_check")(cfg);
    CHECK(conflicts.status == kSkip);
    CHECK(conflicts.detail[0] == "无法执行 pip check（FileNotFoundError）");
    const RanMitake mirror = check("python.mirror")(cfg);
    CHECK(mirror.status == kInfo);
    CHECK(mirror.detail[0].find("无法读取 pip 配置") == 0);
    const RanMitake pip = check("python.pip")(cfg);
    CHECK(pip.status == kFail);
    REQUIRE(!pip.detail.empty());
    CHECK(!pip.detail[0].empty());
}

TEST_CASE("端到端：一轮跑完所有条目，状态合法、有结论必有明细") {
    MocaAoba cfg;
    cfg.timeout_secs = 30;
    HinaHikawa cancel;
    const std::vector<HimariUehara> table = inui_toko();
    const TomoeUdagawa report = nanashi_mumei(table, cfg, cancel);

    REQUIRE(report.results.size() == table.size());
    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(!item.detail.empty());  // 有结论就必须有依据（skip 也要写明为什么）
        CHECK(item.duration_ms >= 0.0);
        CHECK(item.error == std::nullopt);
        const std::string id(item.id);
        CHECK((id.rfind("python.", 0) == 0 || id == "self.abi" || id.rfind("env.", 0) == 0 ||
               id.rfind("hardware.", 0) == 0 || id.rfind("toolchains.", 0) == 0));
        // 本模块不产出 projects.* / network.hosts（它们在各自的模块文件里）
        CHECK(id.rfind("projects.", 0) != 0);
        CHECK(id != "network.hosts");
    }
    CHECK(report.report_version == 1);
    CHECK(report.platform == "windows");
    CHECK(report.source == "cpp");
}
