// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 工具链类检查的测试。
//
// 取向：目录形状与每条判定分支都钉死（喂手工构造的探针结果，完全不碰本机环境），
// 而"这台机器到底装了什么"只在最后一条端到端用例里碰 —— 那里也只断言状态取值合法、
// 有结论必有明细，不断言具体结论。

#include <doctest/doctest.h>

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/process.h"
#include "base/status.h"
#include "checks/mod.h"
#include "checks/toolchains.h"
#include "engine/engine.h"
#include "win/tools.h"

using namespace envdoctor;

namespace {

/// 手工构造探针结果：测试里不启动任何进程。
RimiUshigome fake_probe(bool success, const std::string& out, const std::string& err = {},
                        bool not_found = false, bool timed_out = false) {
    RimiUshigome r;
    r.success = success;
    r.out = out;
    r.err = err;
    r.not_found = not_found;
    r.timed_out = timed_out;
    return r;
}

/// "程序没找到"（`not_found` 通路，上层据此报未安装）。
const RimiUshigome kMissing = fake_probe(false, "", "系统找不到指定的文件。", true, false);

/// "两路都没取到、退出码非 0"：既不是未安装也不是超时 —— 本次探测什么也没得到。
const RimiUshigome kNoResult = fake_probe(false, "", "");

MocaAoba requiring(const std::string& id) {
    MocaAoba cfg;
    cfg.required = {id};
    return cfg;
}

/// 期望的目录：白名单工具（复合检查用代表工具）/ id / 标题 / 是否只在 Windows 上有意义。
/// id 与旧实现逐字一致：表驱动检查直接用工具标识当 id，只有包管理器那一项带类别前缀。
/// 表驱动工具的"展示名"不在这里单列 —— 它由判定文案反查（见下一条用例），
/// 这样注册表标题、工具表名字、判定文案三处一旦分叉就会红。
const std::vector<std::tuple<YukinaMinato, const char*, const char*, bool>> kExpected = {
    {YukinaMinato::Git, "git", "Git", false},
    {YukinaMinato::Node, "node", "Node.js", false},
    {YukinaMinato::Npm, "npm", "npm", false},
    {YukinaMinato::Java, "java", "Java", false},
    {YukinaMinato::Javac, "javac", "JDK (javac)", false},
    {YukinaMinato::Mvn, "mvn", "Maven", false},
    {YukinaMinato::Gradle, "gradle", "Gradle", false},
    {YukinaMinato::Go, "go", "Go", false},
    {YukinaMinato::Rustc, "rustc", "Rust (rustc)", false},
    {YukinaMinato::Cargo, "cargo", "Cargo", false},
    {YukinaMinato::Gcc, "gcc", "GCC", false},
    {YukinaMinato::Gxx, "g++", "G++", false},
    {YukinaMinato::Clang, "clang", "Clang", false},
    {YukinaMinato::Clangxx, "clang++", "Clang++", false},
    {YukinaMinato::Cmake, "cmake", "CMake", false},
    {YukinaMinato::Ninja, "ninja", "Ninja", false},
    {YukinaMinato::Make, "make", "Make", false},
    {YukinaMinato::DotNet, "dotnet", ".NET", false},
    {YukinaMinato::Python, "python", "Python（系统级）", false},
    {YukinaMinato::Lua, "lua", "Lua", false},
    {YukinaMinato::Luajit, "luajit", "LuaJIT", false},
    {YukinaMinato::Luarocks, "luarocks", "LuaRocks", false},
    {YukinaMinato::Nvcc, "nvcc", "CUDA (nvcc)", false},
    {YukinaMinato::Vswhere, "vswhere", "VS Build Tools (vswhere)", true},
    {YukinaMinato::Kubectl, "kubectl", "kubectl", false},
    {YukinaMinato::Ffmpeg, "ffmpeg", "FFmpeg", false},
    {YukinaMinato::VswhereVc, "msvc", "MSVC C++ 工具集", true},
    {YukinaMinato::Conda, "toolchains.pkg_mgr", "包管理器", false},
};

/// 三个复合检查没有独立的表驱动展示名：它们的名字写在各自的判定函数里。
bool is_composite(YukinaMinato tool) {
    return tool == YukinaMinato::Javac || tool == YukinaMinato::VswhereVc ||
           tool == YukinaMinato::Conda;
}

}  // namespace

TEST_CASE("工具链目录：28 个 id / 标题 / 类别 / 平台门逐字一致") {
    const std::vector<HimariUehara> all = ienaga_mugi();
    REQUIRE(all.size() == kExpected.size());
    CHECK(all.size() == 28);

    std::set<std::string> ids;
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& [tool, id, title, windows_only] = kExpected[i];
        (void)tool;
        CHECK(std::string(all[i].id) == id);
        CHECK(std::string(all[i].title) == title);
        CHECK(std::string(all[i].category) == "toolchains");
        CHECK(all[i].fn != nullptr);
        CHECK(ids.insert(all[i].id).second);          // 重复 id 会让结果互相覆盖
        CHECK(*all[i].id != '\0');
        // 只有包管理器那一项带类别前缀，其余 id 就是工具标识本身（与旧实现一致）
        if (std::string(all[i].id) != "toolchains.pkg_mgr") {
            CHECK(std::string(all[i].id).find('.') == std::string::npos);
        }
        if (windows_only) {
            REQUIRE(all[i].platforms.size() == 1);
            CHECK(std::string(all[i].platforms[0]) == "windows");
        } else {
            CHECK(all[i].platforms.empty());
        }
    }
    // 只该有 vswhere / msvc 两项带平台门
    size_t gated = 0;
    for (const HimariUehara& def : all) {
        if (!def.platforms.empty()) {
            ++gated;
        }
    }
    CHECK(gated == 2);
}

TEST_CASE("id 单一口径：注册表 ↔ 工具标识 ↔ 判定文案里的展示名") {
    const std::vector<HimariUehara> all = ienaga_mugi();
    for (size_t i = 0; i < kExpected.size(); ++i) {
        const auto& [tool, id, title, windows_only] = kExpected[i];
        (void)windows_only;
        // 注册表字面量必须与判定侧拼出来的一致，否则 --require 会静默失效
        CHECK(std::string(all[i].id) == id);
        CHECK(yashiro_kizuku(tool) == id);

        if (is_composite(tool)) {
            continue;
        }
        // 表驱动检查的展示名取自模块内的工具表：用"未安装"两态把注册表标题反查一遍
        const RanMitake hit = hassaku_yuzu(tool, id, requiring(id), kMissing);
        CHECK(hit.status == kFail);
        REQUIRE(hit.detail.size() == 1);
        CHECK(hit.detail[0] == std::string(title) + " 未安装（已在 --require 中声明为必备）");
        REQUIRE(hit.hint.has_value());
        CHECK(*hit.hint == "请安装并确保其位于 PATH 中");

        const RanMitake plain = hassaku_yuzu(tool, id, MocaAoba{}, kMissing);
        CHECK(plain.status == kInfo);
        REQUIRE(plain.detail.size() == 1);
        CHECK(plain.detail[0] == std::string(title) + " 未安装");
        CHECK_FALSE(plain.hint.has_value());
    }
}

TEST_CASE("版本行：默认取首行，Gradle 单独挑 `Gradle x.y` 行") {
    CHECK(umiyashano_kami(YukinaMinato::Clang, "clang version 18.1.8\n") == "clang version 18.1.8");
    // 非 Gradle 的工具不会被 `Gradle x.y` 行带走
    CHECK(umiyashano_kami(YukinaMinato::Mvn, "Apache Maven 3.9.6\nGradle 8.5\n") ==
          "Apache Maven 3.9.6");
    // gradle --version 首行是分隔线，版本在 `Gradle x.y` 行上
    const std::string banner =
        "\n------------------------------------------------------------\n"
        "Gradle 8.5\n"
        "------------------------------------------------------------\n\n"
        "Kotlin:       1.9.20\n";
    CHECK(umiyashano_kami(YukinaMinato::Gradle, banner) == "Gradle 8.5");
    // 没有版本行时回退首行，不会拿到空串以外的意外值
    CHECK(umiyashano_kami(YukinaMinato::Gradle, "some odd output") == "some odd output");
    CHECK(umiyashano_kami(YukinaMinato::Gradle, "") == "");
    CHECK(umiyashano_kami(YukinaMinato::Git, "") == "");
    // 子进程输出是 CRLF：行尾的 \r 不能留在版本号里
    CHECK(umiyashano_kami(YukinaMinato::Node, "v20.11.1\r\n") == "v20.11.1");
    CHECK(umiyashano_kami(YukinaMinato::Gradle, "----\r\nGradle 8.5\r\n") == "Gradle 8.5");
    // 末行没有换行符也要能取到
    CHECK(umiyashano_kami(YukinaMinato::Ninja, "1.12.1") == "1.12.1");
}

TEST_CASE("表驱动判定：五条状态分支与文案") {
    // 未安装 + 未声明必备 → info
    const RanMitake missing = hassaku_yuzu(YukinaMinato::Git, "git", MocaAoba{}, kMissing);
    CHECK(missing.status == kInfo);
    CHECK(missing.detail[0] == "Git 未安装");
    CHECK_FALSE(missing.hint.has_value());

    // 未安装 + 声明必备 → fail + 安装提示
    const RanMitake required =
        hassaku_yuzu(YukinaMinato::Git, "git", requiring("git"), kMissing);
    CHECK(required.status == kFail);
    CHECK(required.detail[0] == "Git 未安装（已在 --require 中声明为必备）");
    REQUIRE(required.hint.has_value());
    CHECK(*required.hint == "请安装并确保其位于 PATH 中");

    // 超时 → timeout（文案里的秒数来自统一常量）
    const RanMitake timeout = hassaku_yuzu(
        YukinaMinato::Git, "git", MocaAoba{},
        fake_probe(false, "", "来不及了", false, true));
    CHECK(timeout.status == kTimeout);
    CHECK(timeout.detail[0] == "Git 检测超时（10s）");
    CHECK_FALSE(timeout.hint.has_value());

    // 成功且有输出 → ok，明细就是版本行
    const RanMitake ok = hassaku_yuzu(YukinaMinato::Git, "git", MocaAoba{},
                                      fake_probe(true, "git version 2.45.2\n"));
    CHECK(ok.status == kOk);
    REQUIRE(ok.detail.size() == 1);
    CHECK(ok.detail[0] == "git version 2.45.2");
    CHECK_FALSE(ok.hint.has_value());

    // 退出码非 0 但有输出 → info：原样报出来，不据此断言能正常用
    const RanMitake nonzero = hassaku_yuzu(YukinaMinato::Git, "git", MocaAoba{},
                                           fake_probe(false, "", "git version 2.45.2"));
    CHECK(nonzero.status == kInfo);
    CHECK(nonzero.detail[0] == "git version 2.45.2");

    // 跑通了但认不出哪一行是版本 → info（装着是确定的）
    const RanMitake unparsed = hassaku_yuzu(YukinaMinato::Git, "git", MocaAoba{},
                                            fake_probe(true, "\n\n"));
    CHECK(unparsed.status == kInfo);
    CHECK(unparsed.detail[0] == "Git 已安装但无法解析版本输出");

    // 装了 Java 的工具走 stderr 兜底：stdout 空就用 stderr
    const RanMitake stderr_only = hassaku_yuzu(YukinaMinato::Lua, "lua", MocaAoba{},
                                               fake_probe(true, "", "Lua 5.4.6  Copyright (C) 1994"));
    CHECK(stderr_only.status == kOk);
    CHECK(stderr_only.detail[0] == "Lua 5.4.6  Copyright (C) 1994");
}

TEST_CASE("表驱动判定：什么都没取到时不判『已安装』（取不到不当结论）") {
    // 既没找到可执行文件、又没超时、退出码非 0、两路输出全空
    const RanMitake skip =
        hassaku_yuzu(YukinaMinato::Git, "git", MocaAoba{}, kNoResult);
    CHECK(skip.status == kSkip);
    REQUIRE(skip.detail.size() == 1);
    CHECK(skip.detail[0].find("本次不判断") != std::string::npos);
    CHECK(skip.detail[0].find("已安装但无法解析版本输出") == std::string::npos);
    CHECK_FALSE(skip.hint.has_value());

    // 声明了必备也一样：没有证据就不能判 fail
    const RanMitake skip_required =
        hassaku_yuzu(YukinaMinato::Git, "git", requiring("git"), kNoResult);
    CHECK(skip_required.status == kSkip);
}

TEST_CASE("JDK/JRE 交叉判定：四个象限 + 缺工具时的建议") {
    const RimiUshigome javac_ok = fake_probe(true, "", "javac 21.0.5\n");
    const RimiUshigome java_ok = fake_probe(true, "", "openjdk version \"21.0.5\"\n");

    // javac + java 双全 → ok（版本行取自 stderr）
    const RanMitake both = azuchi_momo(java_ok, javac_ok, false);
    CHECK(both.status == kOk);
    REQUIRE(both.detail.size() == 2);
    CHECK(both.detail[0] == "javac 21.0.5");
    CHECK(both.detail[1] == "PATH 上有 java");
    CHECK_FALSE(both.hint.has_value());

    // 只有 JRE → info + 点破（要点破，但不算病）
    const RanMitake jre_only = azuchi_momo(java_ok, kMissing, false);
    CHECK(jre_only.status == kInfo);
    REQUIRE(jre_only.detail.size() == 2);
    CHECK(jre_only.detail[0] == "PATH 上有 java");
    CHECK(jre_only.detail[1] == "但没有 javac —— 很可能只装了 JRE：能运行，不能编译");
    REQUIRE(jre_only.hint.has_value());
    CHECK(*jre_only.hint == "需要编译 Java 代码时安装 JDK，并确保 javac 在 PATH 上");

    // 双缺 → info，不给提示（没装 Java 本来就正常）
    const RanMitake none = azuchi_momo(kMissing, kMissing, false);
    CHECK(none.status == kInfo);
    REQUIRE(none.detail.size() == 1);
    CHECK(none.detail[0] == "javac 未安装（未装 Java 时属正常）");
    CHECK_FALSE(none.hint.has_value());

    // 超时优先于一切判定
    const RanMitake timeout = azuchi_momo(java_ok, fake_probe(false, "", "", false, true), false);
    CHECK(timeout.status == kTimeout);
    CHECK(timeout.detail[0] == "javac 检测超时（10s）");
    CHECK_FALSE(timeout.hint.has_value());

    // --require javac：缺失升格为 fail（JRE-only 与双缺都算）
    const RanMitake req_jre = azuchi_momo(java_ok, kMissing, true);
    CHECK(req_jre.status == kFail);
    CHECK(req_jre.detail[0] == "javac 未安装（已在 --require 中声明为必备）");
    REQUIRE(req_jre.hint.has_value());
    CHECK(req_jre.hint->find("JDK") != std::string::npos);
    const RanMitake req_none = azuchi_momo(kMissing, kMissing, true);
    CHECK(req_none.status == kFail);
    // 已安装时 required 不改变 ok 判定
    const RanMitake req_ok = azuchi_momo(java_ok, javac_ok, true);
    CHECK(req_ok.status == kOk);
    CHECK_FALSE(req_ok.hint.has_value());

    // 有输出、只是认不出：仍是 info（沿用旧口径）
    const RanMitake unparsed = azuchi_momo(java_ok, fake_probe(false, "javac ???\n"), false);
    CHECK(unparsed.status == kInfo);
    CHECK(unparsed.detail[0] == "javac 已安装但无法解析版本输出（PATH 上有 java）");
}

TEST_CASE("JDK/JRE 交叉判定：java 探针没取到时不断言『PATH 上有 java』") {
    // java 探针超时（旧口径会把它当"PATH 上有 java"，进而断言只装了 JRE）
    const RimiUshigome java_timeout = fake_probe(false, "", "", false, true);
    const RanMitake skip = azuchi_momo(java_timeout, kMissing, false);
    CHECK(skip.status == kSkip);
    REQUIRE(skip.detail.size() == 2);
    CHECK(skip.detail[0] == "javac 未安装（进程未找到）");
    CHECK(skip.detail[1].find("本次不判断") != std::string::npos);
    for (const std::string& line : skip.detail) {
        CHECK(line.find("PATH 上有 java") == std::string::npos);
    }
    CHECK_FALSE(skip.hint.has_value());

    // 但"javac 缺失"本身是硬证据：声明过必备就照旧 fail
    const RanMitake required = azuchi_momo(java_timeout, kMissing, true);
    CHECK(required.status == kFail);
    CHECK(required.detail[0] == "javac 未安装（已在 --require 中声明为必备）");

    // javac 正常、java 没取到：javac 的版本照报，只是不判断 java 那一半
    const RanMitake half =
        azuchi_momo(java_timeout, fake_probe(true, "javac 21.0.5\n"), false);
    CHECK(half.status == kOk);
    REQUIRE(half.detail.size() == 2);
    CHECK(half.detail[0] == "javac 21.0.5");
    CHECK(half.detail[1] != "PATH 上有 java");
    CHECK(half.detail[1].find("未判断") != std::string::npos);

    // java 探针"跑了但两路都没取到"（不是超时）同样算没取到
    const RanMitake quiet = azuchi_momo(kNoResult, kMissing, false);
    CHECK(quiet.status == kSkip);

    // javac 自己什么都没取到 → 不判断是否已安装
    const RimiUshigome java_ok = fake_probe(true, "", "openjdk version \"21.0.5\"\n");
    const RanMitake javac_silent = azuchi_momo(java_ok, kNoResult, false);
    CHECK(javac_silent.status == kSkip);
    REQUIRE(javac_silent.detail.size() == 1);
    CHECK(javac_silent.detail[0].find("本次不判断") != std::string::npos);
}

TEST_CASE("MSVC 工具集判定：四种安装形态 + 查询失败不判结论") {
    // 整个 VS 系未装 → info，无提示
    const RanMitake absent = harusaki_air(kMissing, false);
    CHECK(absent.status == kInfo);
    CHECK(absent.detail[0] == "未检测到 Visual Studio Installer（vswhere）—— 未安装 MSVC 时属正常");
    CHECK_FALSE(absent.hint.has_value());

    // --require msvc：vswhere 缺失升格为 fail
    const RanMitake absent_required = harusaki_air(kMissing, true);
    CHECK(absent_required.status == kFail);
    CHECK(absent_required.detail[0] ==
          "未检测到 Visual Studio Installer（vswhere）——已在 --require 中声明 MSVC 为必备");
    REQUIRE(absent_required.hint.has_value());
    CHECK(*absent_required.hint == "安装 Visual Studio Build Tools 并勾选“使用 C++ 的桌面开发”");

    // 超时
    const RanMitake timeout = harusaki_air(fake_probe(false, "", "", false, true), false);
    CHECK(timeout.status == kTimeout);
    CHECK(timeout.detail[0] == "vswhere 检测超时（10s）");
    CHECK_FALSE(timeout.hint.has_value());

    // VS + VC.Tools 都装了 → ok，附 cl.exe 的 PATH 说明
    const RanMitake ok = harusaki_air(fake_probe(true, "17.9.6\n"), false);
    CHECK(ok.status == kOk);
    REQUIRE(ok.detail.size() == 2);
    CHECK(ok.detail[0] == "VS 17.9.6 已带 C++ 工具集（VC.Tools）");
    CHECK(ok.detail[1].find("cl.exe") != std::string::npos);
    CHECK_FALSE(ok.hint.has_value());

    // 装了 VS 但没装 C++ 组件（-requires 空输出、退出码 0）→ info + 勾选提示
    const RanMitake no_vc = harusaki_air(fake_probe(true, "\n"), false);
    CHECK(no_vc.status == kInfo);
    REQUIRE(no_vc.detail.size() == 1);
    CHECK(no_vc.detail[0] ==
          "已安装 Visual Studio / Build Tools，但未包含 C++ 工具集"
          "（Microsoft.VisualStudio.Component.VC.Tools）");
    REQUIRE(no_vc.hint.has_value());
    CHECK(*no_vc.hint == "需要本机编译 C/C++ 时，在 Visual Studio Installer 里勾选“使用 C++ 的桌面开发”");

    // --require msvc：缺组件也升格为 fail；已装好不受影响
    const RanMitake no_vc_required = harusaki_air(fake_probe(true, "\n"), true);
    CHECK(no_vc_required.status == kFail);
    REQUIRE(no_vc_required.hint.has_value());
    const RanMitake ok_required = harusaki_air(fake_probe(true, "17.9.6\n"), true);
    CHECK(ok_required.status == kOk);
    CHECK_FALSE(ok_required.hint.has_value());
}

TEST_CASE("MSVC 判定：vswhere 退出码非 0 时不再编『已带 C++ 工具集』") {
    // 非空输出 + 退出码非 0：旧口径会写成 "VS <错误文本> 已带 C++ 工具集（VC.Tools）"
    const RanMitake failed_with_text =
        harusaki_air(fake_probe(false, "Error 0x80131500: something went wrong\n"), false);
    CHECK(failed_with_text.status == kSkip);
    REQUIRE(failed_with_text.detail.size() == 2);
    CHECK(failed_with_text.detail[0].find("退出码非 0") != std::string::npos);
    CHECK(failed_with_text.detail[1].find("Error 0x80131500") != std::string::npos);
    for (const std::string& line : failed_with_text.detail) {
        CHECK(line.find("已带 C++ 工具集") == std::string::npos);
    }
    CHECK_FALSE(failed_with_text.hint.has_value());

    // 空输出 + 退出码非 0：旧口径会写成 "未包含 C++ 工具集"
    const RanMitake failed_empty = harusaki_air(kNoResult, false);
    CHECK(failed_empty.status == kSkip);
    REQUIRE(failed_empty.detail.size() == 1);
    CHECK(failed_empty.detail[0].find("本次不判断") != std::string::npos);

    // --require 也一样：没证据就不判 fail
    const RanMitake failed_required = harusaki_air(kNoResult, true);
    CHECK(failed_required.status == kSkip);
}

TEST_CASE("包管理器汇总：未装不算病，装了才算 ok") {
    const std::vector<std::pair<std::string, RimiUshigome>> none = {
        {"Conda", kMissing}, {"Poetry", kMissing}, {"Pipenv", kMissing}};
    const RanMitake info = kanda_shoichi(none, false);
    CHECK(info.status == kInfo);
    REQUIRE(info.detail.size() == 3);
    CHECK(info.detail[0] == "Conda: 未安装");
    CHECK(info.detail[1] == "Poetry: 未安装");
    CHECK(info.detail[2] == "Pipenv: 未安装");
    REQUIRE(info.hint.has_value());
    CHECK(*info.hint == "未检测到 Conda/Poetry/Pipenv；仅用 pip + venv 时无需安装");

    // 有一个可用 → ok，无提示
    const std::vector<std::pair<std::string, RimiUshigome>> some = {
        {"Conda", fake_probe(true, "conda 24.1.0\n")}, {"Poetry", kMissing}};
    const RanMitake ok = kanda_shoichi(some, false);
    CHECK(ok.status == kOk);
    REQUIRE(ok.detail.size() == 2);
    CHECK(ok.detail[0] == "Conda: conda 24.1.0");
    CHECK(ok.detail[1] == "Poetry: 未安装");
    CHECK_FALSE(ok.hint.has_value());

    // --require toolchains.pkg_mgr：一个都没有才升格为 fail
    const RanMitake required = kanda_shoichi(none, true);
    CHECK(required.status == kFail);
    REQUIRE(required.hint.has_value());
    CHECK(*required.hint == "Conda/Poetry/Pipenv 均未安装（已在 --require 中声明为必备）");
    const RanMitake required_but_found = kanda_shoichi(some, true);
    CHECK(required_but_found.status == kOk);

    // 装了、只是版本解析不出：进程退出码 0，"在不在"已有答案 → 算装上了
    const std::vector<std::pair<std::string, RimiUshigome>> unparsed = {
        {"Conda", fake_probe(true, "")}, {"Poetry", kMissing}};
    const RanMitake present = kanda_shoichi(unparsed, false);
    CHECK(present.status == kOk);
    CHECK(present.detail[0] == "Conda: 已安装但无法解析版本");
}

TEST_CASE("包管理器汇总：探测没取到时不判『未安装』") {
    const std::vector<std::pair<std::string, RimiUshigome>> timed_out = {
        {"Conda", kMissing}, {"Poetry", fake_probe(false, "", "", false, true)},
        {"Pipenv", kMissing}};
    const RanMitake skip = kanda_shoichi(timed_out, false);
    CHECK(skip.status == kSkip);
    REQUIRE(skip.detail.size() == 4);
    CHECK(skip.detail[1] == "Poetry: 检测超时");
    CHECK(skip.detail[3].find("本次不判断") != std::string::npos);
    CHECK_FALSE(skip.hint.has_value());

    // --require 也一样：有一个没探到就不能说"均未安装"
    const RanMitake skip_required = kanda_shoichi(timed_out, true);
    CHECK(skip_required.status == kSkip);

    // 探针没跑起来（退出码非 0 且无输出）
    const std::vector<std::pair<std::string, RimiUshigome>> silent = {
        {"Conda", kNoResult}, {"Poetry", kMissing}, {"Pipenv", kMissing}};
    const RanMitake skip_silent = kanda_shoichi(silent, false);
    CHECK(skip_silent.status == kSkip);
    CHECK(skip_silent.detail[0] == "Conda: 探测未取得结果");
    // 空的探针列表不该被当成"都装了"（防御：present 计数从 0 起）
    const RanMitake empty = kanda_shoichi({}, false);
    CHECK(empty.status == kInfo);
}

TEST_CASE("端到端：工具链检查在本机真的跑得出合法结果") {
    MocaAoba cfg;
    cfg.timeout_secs = 30;
    cfg.categories = std::vector<std::string>{"toolchains"};
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(gawr_gura(), cfg, cancel);

    // 类别 toolchains 下不止本模块：Python 层还有 4 项（toolchains.git_identity /
    // toolchains.ssh_keys / toolchains.java_home / toolchains.git_config），由 `checks/python.cpp`
    // 注册。所以这里只要求"至少包含本模块的 28 个 id"，不把这一类别的总数写死 ——
    // 两侧谁多一项都会让写死的数字再红一次。
    CHECK(report.results.size() >= kExpected.size());
    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};

    // 结果按 id 取回来：本模块的条目逐条查，其余模块注册的条目只做最弱的合法性校验。
    std::map<std::string, const ArisaIchigaya*> by_id;
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(item.category == "toolchains");
        CHECK(by_id.emplace(item.id, &item).second);  // 重复 id 会让结果互相覆盖
        CHECK(item.duration_ms >= 0.0);
    }

    for (const auto& [tool, id, title, windows_only] : kExpected) {
        (void)tool;
        (void)title;
        (void)windows_only;
        const auto hit = by_id.find(id);
        REQUIRE(hit != by_id.end());
        const ArisaIchigaya& item = *hit->second;
        // 有结论必有明细：这台机器装没装不断言，只要求"说了话就得有依据"
        CHECK(!item.detail.empty());
        CHECK_FALSE(item.error.has_value());
        // required 为空时不该出现 fail（未安装只报 info）
        CHECK(item.status != kFail);
    }
}
