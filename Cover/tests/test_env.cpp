// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 环境类检查的测试。分三层：
//   ① 目录形状（id/标题/类别/平台门）——纯契约，与宿主无关；
//   ② 纯判定函数的状态分支——注入取数结果，把"读不到"那几支真正跑一遍；
//   ③ 端到端跑一轮——检查项读的是本机现状，故只断言状态取值合法、有结论必有明细，
//      不断言具体结论（本机装没装什么不该由测试决定）。

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/status.h"
#include "checks/env.h"
#include "engine/engine.h"
#include "win/registry.h"

using namespace envdoctor;

TEST_CASE("环境模块目录：id/标题/类别与平台门与契约一致") {
    const std::vector<HimariUehara> all = spade_echo();
    REQUIRE(all.size() == 11);

    const char* const kIds[11] = {
        "env.path_validity",   "env.path_shadowing", "env.longpaths", "env.reboot_pending",
        "env.vcredist",        "env.defender_exclusions", "env.uac", "env.secureboot",
        "env.smb1",            "env.tls_legacy",     "env.dev_mode",
    };
    const char* const kTitles[11] = {
        "PATH 有效性", "PATH 遮蔽",     "长路径支持", "待重启状态",
        "VC++ 运行库", "Defender 路径排除项", "UAC 状态",  "安全启动",
        "SMBv1",       "旧版 TLS 配置", "开发者模式",
    };
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string id(all[i].id);
        CHECK(id == kIds[i]);
        CHECK(std::string(all[i].title) == kTitles[i]);
        CHECK(std::string(all[i].category) == "env");
        CHECK(id.rfind("env.", 0) == 0);
        CHECK(all[i].fn != nullptr);
    }
    // 只有 PATH 有效性不限平台；监视名单/注册表那些都是 Windows 形态。
    CHECK(all[0].platforms.empty());
    for (size_t i = 1; i < all.size(); ++i) {
        REQUIRE(all[i].platforms.size() == 1);
        CHECK(std::string(all[i].platforms[0]) == "windows");
    }
}

TEST_CASE("PATH 条目切分：去空白、剥引号、丢空项") {
    const std::vector<std::string> items =
        suzuya_aki(" \"C:\\Program Files\\x\" ;C:\\b;;\"C:\\c\";", ';');
    REQUIRE(items.size() == 3);
    CHECK(items[0] == "C:\\Program Files\\x");
    CHECK(items[1] == "C:\\b");
    CHECK(items[2] == "C:\\c");
}

TEST_CASE("PATH 有效性：失效目录与重复条目各占一行") {
    const auto all_exist = [](const std::string&) { return true; };
    const auto missing_one = [](const std::string& p) { return p != "C:\\missing"; };

    const RanMitake clean = tsukino_mito(std::string("C:\\a;C:\\b"), ';', all_exist);
    CHECK(clean.status == kOk);
    REQUIRE(clean.detail.size() == 1);
    CHECK(clean.detail[0] == "条目 2 个，共 9 字符");
    CHECK(!clean.hint.has_value());

    // 重复项里存在"仅尾斜杠不同"的形态；失效目录与重复项都要计入 warn。
    const RanMitake dirty =
        tsukino_mito(std::string("C:\\bin;C:\\missing;C:\\bin\\;D:\\x"), ';', missing_one);
    CHECK(dirty.status == kWarn);
    REQUIRE(dirty.detail.size() == 4);
    CHECK(dirty.detail[0] == "条目 4 个，共 30 字符");
    CHECK(dirty.detail[1] == "重复条目 1 个（去重可缩短约 8 字符）");
    CHECK(dirty.detail[2] == "失效目录 1 个:");
    CHECK(dirty.detail[3] == "  C:\\missing");
    REQUIRE(dirty.hint.has_value());
    CHECK(dirty.hint->find("清理系统/用户 PATH") != std::string::npos);

    // 超过 5 个失效目录只列前 5 个，余下给一行汇总。
    std::string many = "C:\\i1";
    for (int i = 2; i <= 7; ++i) {
        many += ";C:\\i" + std::to_string(i);
    }
    const RanMitake big = tsukino_mito(many, ';', [](const std::string&) { return false; });
    CHECK(big.status == kWarn);
    REQUIRE(big.detail.size() == 8);
    CHECK(big.detail[0] == "条目 7 个，共 41 字符");
    CHECK(big.detail[1] == "失效目录 7 个:");
    CHECK(big.detail[6] == "  C:\\i5");
    CHECK(big.detail[7] == "  …另有 2 个未列出");

    // 取不到 PATH：没有可判定的条目，不做结论（旧实现同样是 skip）。
    for (const std::optional<std::string>& raw :
         {std::optional<std::string>(std::nullopt), std::optional<std::string>("")}) {
        const RanMitake none = tsukino_mito(raw, ';', all_exist);
        CHECK(none.status == kSkip);
        REQUIRE(none.detail.size() == 1);
        CHECK(none.detail[0] == "PATH 为空");
        CHECK(!none.hint.has_value());
    }
}

TEST_CASE("PATH 遮蔽：只报被遮蔽的项，取不到 PATH 不做结论") {
    const auto two_dirs = [](const std::string& p) {
        return p == "C:\\a\\python.exe" || p == "C:\\c\\python.exe";
    };

    const RanMitake one = shibuya_hajime(std::string("C:\\a;C:\\b;C:\\c"), two_dirs);
    CHECK(one.status == kWarn);
    REQUIRE(one.detail.size() == 2);
    CHECK(one.detail[0] == "1 项被遮蔽:");
    CHECK(one.detail[1] == "  python.exe：生效 C:\\a，被遮蔽 C:\\c");
    REQUIRE(one.hint.has_value());
    CHECK(one.hint->find("PATH 上第一个出现的目录") != std::string::npos);

    // 同一目录在 PATH 里重复出现是常态，不算遮蔽（只有不同目录之间的同名冲突才算）。
    const RanMitake same = shibuya_hajime(std::string("C:\\a;C:\\a\\"), two_dirs);
    CHECK(same.status == kOk);
    REQUIRE(same.detail.size() == 1);
    CHECK(same.detail[0] == "监视名单内的可执行文件无遮蔽");
    CHECK(!same.hint.has_value());

    // 名单里 4 个名字 × 2 个多余目录 = 8 条遮蔽，只列前 6 条。
    const auto three_dirs = [](const std::string& p) {
        return p.rfind("C:\\d1\\", 0) == 0 || p.rfind("C:\\d2\\", 0) == 0 ||
               p.rfind("C:\\d3\\", 0) == 0;
    };
    const RanMitake many = shibuya_hajime(std::string("C:\\d1;C:\\d2;C:\\d3"), three_dirs);
    CHECK(many.status == kWarn);
    REQUIRE(many.detail.size() == 8);
    CHECK(many.detail[0] == "8 项被遮蔽:");
    CHECK(many.detail[7] == "  …另有 2 条未列出");

    // 取不到 / 空 PATH：旧实现会落到"无遮蔽"（用一次失败的读取给出结论），这里不判断。
    for (const std::optional<std::string>& raw :
         {std::optional<std::string>(std::nullopt), std::optional<std::string>(";;")}) {
        const RanMitake none = shibuya_hajime(raw, two_dirs);
        CHECK(none.status == kSkip);
        REQUIRE(none.detail.size() == 1);
        CHECK(none.detail[0] == "PATH 未设置或为空，本次不判断遮蔽");
    }
}

TEST_CASE("长路径：如实回报取值，读不到不下结论") {
    const TaeHanazono<uint32_t> on{1u, {}};
    const TaeHanazono<uint32_t> off{0u, {}};
    const TaeHanazono<uint32_t> odd{7u, {}};
    const TaeHanazono<uint32_t> refused{std::nullopt, {5, "拒绝访问"}};

    const RanMitake ok = higuchi_kaede(on);
    CHECK(ok.status == kOk);
    REQUIRE(ok.detail.size() == 1);
    CHECK(ok.detail[0] == "LongPathsEnabled = 1");
    CHECK(!ok.hint.has_value());

    const RanMitake warn = higuchi_kaede(off);
    CHECK(warn.status == kWarn);
    CHECK(warn.detail[0] == "LongPathsEnabled = 0");
    REQUIRE(warn.hint.has_value());
    CHECK(warn.hint->find("长路径") != std::string::npos);

    // 值既非 0 也非 1 时报告里要显示真实取值（旧实现这一支把文案写死成 = 0）。
    const RanMitake strange = higuchi_kaede(odd);
    CHECK(strange.status == kWarn);
    CHECK(strange.detail[0] == "LongPathsEnabled = 7");

    const RanMitake skip = higuchi_kaede(refused);
    CHECK(skip.status == kSkip);
    REQUIRE(skip.detail.size() == 1);
    CHECK(skip.detail[0] == "读取注册表失败（winerror=5）");
    CHECK(!skip.hint.has_value());
}

TEST_CASE("待重启：命中/不存在/读不到三分") {
    const TaeHanazono<bool> miss{false, {}};
    const TaeHanazono<bool> hit{true, {}};
    const TaeHanazono<bool> refused{std::nullopt, {5, "拒绝访问"}};

    const RanMitake clean = shizuka_rin({miss, miss, miss});
    CHECK(clean.status == kOk);
    REQUIRE(clean.detail.size() == 1);
    CHECK(clean.detail[0] == "三处待重启标记均不存在");
    CHECK(!clean.hint.has_value());

    const RanMitake found = shizuka_rin({hit, miss, miss});
    CHECK(found.status == kWarn);
    REQUIRE(found.detail.size() == 2);
    CHECK(found.detail[0] == "命中 1 项:");
    CHECK(found.detail[1] == "  待重命名的文件（安装器/驱动遗留）");
    REQUIRE(found.hint.has_value());
    CHECK(found.hint->find("重启") != std::string::npos);

    // 读不到 ≠ 不命中：旧实现会报"三处待重启标记均不存在"。
    const RanMitake unknown = shizuka_rin({refused, miss, miss});
    CHECK(unknown.status == kSkip);
    REQUIRE(unknown.detail.size() == 1);
    CHECK(unknown.detail[0] ==
          "待重命名的文件（安装器/驱动遗留）: 读取失败（winerror=5），本次不判断待重启状态");
    CHECK(!unknown.hint.has_value());

    // 命中与读不到并存：命中照样报 warn，读不到的那项如实列出。
    const RanMitake mixed = shizuka_rin({miss, hit, refused});
    CHECK(mixed.status == kWarn);
    REQUIRE(mixed.detail.size() == 3);
    CHECK(mixed.detail[1] == "  Windows 更新待重启");
    CHECK(mixed.detail[2] == "  组件服务(CBS) 待重启: 读取失败（winerror=5）");
}

TEST_CASE("VC++ 运行库版本号：优先现成字符串，分量不齐留空") {
    const std::array<std::optional<uint32_t>, 4> none{};
    CHECK(moira(std::string("v14.40.33810.00"), none) == "v14.40.33810.00");
    CHECK(moira(std::string("14.38.33130.00"), none) == "v14.38.33130.00");
    CHECK(moira(std::string("  v14.40.33810.0  "), none) == "v14.40.33810.0");
    CHECK(moira(std::nullopt, none).empty());
    CHECK(moira(std::string("   "), none).empty());

    // 注册表 Bld/Rbld 是整数值：33810/0 → "v14.40.33810.0"（不是补零的两位写法）。
    const std::array<std::optional<uint32_t>, 4> parts{14u, 40u, 33810u, 0u};
    CHECK(moira(std::nullopt, parts) == "v14.40.33810.0");

    const std::array<std::optional<uint32_t>, 4> partial{14u, std::nullopt, std::nullopt,
                                                         std::nullopt};
    CHECK(moira(std::nullopt, partial).empty());
}

TEST_CASE("VC++ 运行库结论：读不到注册表或 SystemRoot 时不下\"未安装\"的结论") {
    const TaeHanazono<uint32_t> installed{1u, {}};
    const TaeHanazono<uint32_t> zero{0u, {}};
    const TaeHanazono<uint32_t> absent{std::nullopt, {kErrorFileNotFound, "系统找不到指定的文件。"}};
    const TaeHanazono<uint32_t> refused{std::nullopt, {5, "拒绝访问"}};
    const std::vector<std::string> detail{"注册表记录: 已安装 v14.40.33810.00",
                                          "System32\\vcruntime140.dll: 不存在"};

    CHECK(yuki_chihiro(installed, false, detail).status == kOk);  // 注册表记录说装了
    CHECK(yuki_chihiro(absent, true, detail).status == kOk);      // DLL 在，同样算装了

    // 两处证据一致（记录说没装 + DLL 也不在）才判"缺运行库"。
    const RanMitake warn = yuki_chihiro(absent, false, detail);
    CHECK(warn.status == kWarn);
    REQUIRE(warn.hint.has_value());
    CHECK(warn.hint->find("VCRUNTIME140.dll") != std::string::npos);
    CHECK(yuki_chihiro(zero, false, detail).status == kWarn);

    // 取不到：注册表读不到、或 SystemRoot 读不到导致 DLL 探测没做成，都不下结论。
    const RanMitake skip = yuki_chihiro(refused, false, detail);
    CHECK(skip.status == kSkip);
    CHECK(skip.detail.back().find("本次不判断 VC++ 运行库安装状态") != std::string::npos);
    CHECK(yuki_chihiro(absent, std::nullopt, detail).status == kSkip);
    CHECK(yuki_chihiro(refused, std::nullopt, detail).status == kSkip);
    // 反过来：注册表读不到但 DLL 在，仍是明确的 ok（正向证据不受影响）。
    CHECK(yuki_chihiro(refused, true, detail).status == kOk);
}

TEST_CASE("旧版 TLS：显式启用报警，读不到不下结论") {
    const TaeHanazono<uint32_t> on{1u, {}};
    const TaeHanazono<uint32_t> off{0u, {}};
    const TaeHanazono<uint32_t> unset{std::nullopt, {kErrorFileNotFound, "系统找不到指定的文件。"}};
    const TaeHanazono<uint32_t> refused{std::nullopt, {5, "拒绝访问"}};

    const RanMitake enabled = elu({on, unset});
    CHECK(enabled.status == kWarn);
    REQUIRE(enabled.detail.size() == 2);
    CHECK(enabled.detail[0] == "TLS 1.0: 显式启用");
    CHECK(enabled.detail[1] == "TLS 1.1: 跟随系统默认");
    REQUIRE(enabled.hint.has_value());
    CHECK(enabled.hint->find("旧版 TLS 被显式打开") != std::string::npos);

    const RanMitake disabled = elu({off, unset});
    CHECK(disabled.status == kOk);
    REQUIRE(disabled.detail.size() == 2);
    CHECK(disabled.detail[0] == "TLS 1.0: 显式禁用");
    REQUIRE(disabled.hint.has_value());
    CHECK(*disabled.hint == "已显式禁用旧版 TLS：符合现代安全基线");

    // 键缺失 = 没显式配过（跟随系统默认），不是问题。
    const RanMitake untouched = elu({unset, unset});
    CHECK(untouched.status == kOk);
    CHECK(untouched.detail[1] == "TLS 1.1: 跟随系统默认");
    CHECK(!untouched.hint.has_value());

    // 读不到 ≠ 跟随系统默认：旧实现把两者写成同一句。
    const RanMitake unknown = elu({refused, unset});
    CHECK(unknown.status == kSkip);
    REQUIRE(unknown.detail.size() == 2);
    CHECK(unknown.detail[0] == "TLS 1.0: 读取失败（winerror=5），本次不判断旧版 TLS 配置");
    CHECK(!unknown.hint.has_value());

    // 显式启用优先：已确认的风险不会被另一项的读取失败掩盖。
    CHECK(elu({refused, on}).status == kWarn);
}

TEST_CASE("端到端：环境检查真的跑得出合法结果") {
    MocaAoba cfg;
    cfg.timeout_secs = 30;
    cfg.categories = std::vector<std::string>{"env"};
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(spade_echo(), cfg, cancel);

    REQUIRE(report.results.size() == 11);
    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(item.category == "env");
        CHECK(std::string(item.id).rfind("env.", 0) == 0);
        CHECK(item.duration_ms >= 0.0);
        CHECK(!item.detail.empty());  // 有结论就必须有依据（skip 也要写明为什么）
        CHECK(item.error == std::nullopt);
    }
    CHECK(report.report_version == 1);
    CHECK(report.platform == "windows");
    CHECK(report.source == "cpp");
}
