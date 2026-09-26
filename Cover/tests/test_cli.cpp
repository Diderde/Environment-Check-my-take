// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 命令行层测试。
//
// 这里**不跑真实诊断**：断言全部建立在手工构造的报告与桩参数上，验的是"同一份输入必得
// 同一份输出"的那部分——参数解析、类别过滤、--require 语义、退出码判定，以及人可读正文与
// 导出文件的逐字格式。宿主相关的部分（本机装没装某工具、控制台是哪个代码页）一律不断言
// 具体结论，只断言"有结论必有明细"这类结构性事实。

#include <doctest/doctest.h>

#include <io.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/status.h"
#include "cli/cli.h"
#include "engine/engine.h"
#include "report/json.h"
#include "report/model.h"

using namespace envdoctor;

TEST_CASE("参数解析：默认值、run 省略与全部开关") {
    const Rosalyn bare = rosalyn({});
    CHECK(bare.error.empty());
    CHECK(bare.mode == "run");
    REQUIRE(bare.cfg.timeout_secs.has_value());
    CHECK(*bare.cfg.timeout_secs == 25);           // 默认 25 秒
    CHECK_FALSE(bare.cfg.categories.has_value());  // 未给 -c = 全跑
    CHECK(bare.cfg.required.empty());
    CHECK_FALSE(bare.cfg.net_full);
    CHECK(bare.raw_scan_roots.empty());
    CHECK_FALSE(bare.json_path.has_value());
    CHECK_FALSE(bare.txt_path.has_value());
    CHECK_FALSE(bare.no_color);
    CHECK(bare.unknown_categories.empty());

    // `run` 是默认子命令：省略它时 run 的选项照样认（旧实现要求选项必须跟在 run 之后）
    const Rosalyn implied = rosalyn({"-c", "env"});
    CHECK(implied.error.empty());
    REQUIRE(implied.cfg.categories.has_value());
    CHECK(implied.cfg.categories->size() == 1);
    CHECK((*implied.cfg.categories)[0] == "env");

    // 短选项的紧跟写法 `-cenv` 与 `--category=env` 等价
    const Rosalyn attached = rosalyn({"-cnetwork"});
    CHECK(attached.error.empty());
    REQUIRE(attached.cfg.categories.has_value());
    CHECK((*attached.cfg.categories)[0] == "network");

    const Rosalyn full = rosalyn({"run", "-c", "env", "--category=network", "--require",
                                  "git,make", "--require", "node", "--timeout", "7", "--json",
                                  "a.json", "--txt", "b.txt", "--net-full", "--scan-root",
                                  "C:\\Windows", "--scan-root", "C:\\Windows"});
    CHECK(full.error.empty());
    REQUIRE(full.cfg.categories.has_value());
    CHECK(full.cfg.categories->size() == 2);
    CHECK((*full.cfg.categories)[0] == "env");
    CHECK((*full.cfg.categories)[1] == "network");
    REQUIRE(full.cfg.required.size() == 3);
    CHECK(full.cfg.required[0] == "git");
    CHECK(full.cfg.required[1] == "make");
    CHECK(full.cfg.required[2] == "node");
    REQUIRE(full.cfg.timeout_secs.has_value());
    CHECK(*full.cfg.timeout_secs == 7);
    CHECK(full.cfg.net_full);
    REQUIRE(full.json_path.has_value());
    CHECK(*full.json_path == "a.json");
    REQUIRE(full.txt_path.has_value());
    CHECK(*full.txt_path == "b.txt");
    // 重复的扫描根原样保留：目录判定与去重推迟到运行前（落地时才需要真文件系统）
    REQUIRE(full.raw_scan_roots.size() == 2);
    CHECK(full.raw_scan_roots[0] == "C:\\Windows");
    CHECK(full.raw_scan_roots[1] == "C:\\Windows");
}

TEST_CASE("参数解析：用法错误、告警分流与已废弃选项") {
    // --timeout：0/负数/非整数/缺值都是用法错误（旧实现靠 click 的 IntRange(min=1)）
    for (const char* bad : {"0", "-1", "abc", "2.5", ""}) {
        const Rosalyn parsed = rosalyn({"run", "--timeout", bad});
        CHECK_FALSE(parsed.error.empty());
    }
    CHECK(rosalyn({"run", "--timeout", "0"}).error.find("--timeout") != std::string::npos);
    CHECK_FALSE(rosalyn({"run", "--timeout"}).error.empty());
    CHECK_FALSE(rosalyn({"run", "--category"}).error.empty());
    CHECK_FALSE(rosalyn({"run", "--json"}).error.empty());

    // 未知选项、多余位置参数、未知子命令：都按用法错误处理
    CHECK_FALSE(rosalyn({"--nope"}).error.empty());
    CHECK_FALSE(rosalyn({"run", "extra"}).error.empty());
    CHECK_FALSE(rosalyn({"bogus"}).error.empty());
    CHECK_FALSE(rosalyn({"run", "--version"}).error.empty());  // 根开关只在子命令之前

    // 展开开关是显示层的：解析阶段不校验类别名，也不影响过滤与退出码
    const Rosalyn expand_one = rosalyn({"run", "-e", "hardware"});
    CHECK(expand_one.error.empty());
    REQUIRE(expand_one.expand_categories.size() == 1);
    CHECK(expand_one.expand_categories[0] == "hardware");
    CHECK_FALSE(expand_one.expand_all);

    const Rosalyn expand_many = rosalyn({"run", "-e", "env", "--expand=network"});
    CHECK(expand_many.error.empty());
    REQUIRE(expand_many.expand_categories.size() == 2);
    CHECK(expand_many.expand_categories[0] == "env");
    CHECK(expand_many.expand_categories[1] == "network");

    CHECK(rosalyn({"run", "-E"}).expand_all);
    CHECK(rosalyn({"run", "--expand-all"}).expand_all);
    CHECK(rosalyn({"run", "-E"}).error.empty());  // 不再按"未实现"退回用法错误

    // `-e` 给未知类别：不报错、不过滤（过滤只看 `-c`），只是匹配不到而已
    const Rosalyn expand_unknown = rosalyn({"run", "-e", "nosuchcat"});
    CHECK(expand_unknown.error.empty());
    CHECK_FALSE(expand_unknown.cfg.categories.has_value());
    CHECK(expand_unknown.unknown_categories.empty());  // 未知类别告警只属于 `-c`

    // 类别名全部无效应按参数错误退出，且**不能**折成"不过滤"（那会让退出码反映无关检查）
    const Rosalyn all_bad = rosalyn({"run", "-c", "nosuchcat"});
    CHECK(all_bad.error.find("未匹配到任何已知类别") != std::string::npos);
    CHECK_FALSE(all_bad.cfg.categories.has_value());

    // 用法错误 = 显式状态（`error` 非空），且这条路径上不留半个已解析的配置：
    // 旧实现在这里直接 exit 2，配置根本没机会被用到；本实现显式清空，
    // 漏看 `error` 的调用方也不会拿着"半个过滤器"静默跑错范围。
    const Rosalyn rejected = rosalyn({"run", "-c", "nosuchcat", "--require", "git", "--json",
                                      "a.json", "--net-full", "-e", "env", "-E"});
    CHECK_FALSE(rejected.error.empty());
    CHECK(rejected.mode == "run");
    CHECK_FALSE(rejected.cfg.categories.has_value());
    CHECK(rejected.cfg.required.empty());
    CHECK_FALSE(rejected.cfg.net_full);
    CHECK_FALSE(rejected.json_path.has_value());
    CHECK(rejected.expand_categories.empty());
    CHECK_FALSE(rejected.expand_all);
    CHECK_FALSE(rejected.obsolete_core);

    // 部分未知：保留有效的，未知的单独留档用于告警
    const Rosalyn mixed = rosalyn({"run", "-c", "databases", "-c", "nosuchcat"});
    CHECK(mixed.error.empty());
    REQUIRE(mixed.cfg.categories.has_value());
    CHECK(mixed.cfg.categories->size() == 1);
    CHECK((*mixed.cfg.categories)[0] == "databases");
    REQUIRE(mixed.unknown_categories.size() == 1);
    CHECK(mixed.unknown_categories[0] == "nosuchcat");

    // --core 在单进程实现里已废弃：告警后忽略，而不是把老脚本一脚踢死
    const Rosalyn legacy_core = rosalyn({"run", "--core", "envdoctor_core.dll"});
    CHECK(legacy_core.error.empty());
    CHECK(legacy_core.obsolete_core);
    CHECK(legacy_core.mode == "run");
}

TEST_CASE("参数解析：根开关与子命令的分派") {
    CHECK(rosalyn({}).mode == "run");
    CHECK(rosalyn({"run"}).mode == "run");
    CHECK(rosalyn({"--version"}).mode == "version");
    CHECK(rosalyn({"--list-checks"}).mode == "list");
    CHECK(rosalyn({"--help"}).mode == "help");
    // 根开关优先于子命令（旧实现里 --version 在回调里先于子命令派发）
    CHECK(rosalyn({"--version", "run"}).mode == "version");
    // 帮助最优先：旧实现由 click 的 eager 选项处理，`--version --help` 也是先出帮助
    CHECK(rosalyn({"--version", "--help"}).mode == "help");
    // tui/gui 认识但没实现：解析不报错，由入口以非 0 收场
    CHECK(rosalyn({"tui"}).mode == "tui");
    CHECK(rosalyn({"gui"}).mode == "gui");
    CHECK(rosalyn({"tui"}).error.empty());
}

TEST_CASE("--version 与 --list-checks 的文本格式") {
    CHECK(kagami_kira() == "envdoctor 0.2.0 / core 0.2.0");

    // 格式取自旧实现的 `f"{category:12} {id:30} {title}"`：类别先补到 12 列，
    // **再跟一个分隔空格**（id 从第 14 列开始），id 补到 30 列后再跟一个分隔空格。
    HimariUehara def{};
    def.id = "databases.ports";
    def.title = "数据库端口";
    def.category = "databases";
    // "databases" 9 字符 → 补 3 空格到 12 列 + 1 个分隔空格 = 4 个空格
    CHECK(arurandeisu(def) == "databases" + std::string(4, ' ') + "databases.ports" +
                                   std::string(16, ' ') + "数据库端口");

    // 超长不截断（补空格量取 0）
    HimariUehara wide{};
    wide.id = "toolchains.some.very.long.check.id";
    wide.title = "标题";
    wide.category = "toolchains";
    // "toolchains" 10 字符 → 补 2 空格到 12 列 + 1 个分隔空格 = 3 个空格；
    // id 34 字符已超 30 列，不补、不截断，后面只跟 1 个分隔空格
    CHECK(arurandeisu(wide) == "toolchains" + std::string(3, ' ') +
                                   "toolchains.some.very.long.check.id" + " 标题");

    // 清单按 (category, id) 升序且 id 唯一（--list-checks 的展示顺序）
    const std::vector<HimariUehara> all = tsukishita_kaoru();
    std::set<std::string> seen;
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string category = all[i].category != nullptr ? all[i].category : "";
        const std::string id = all[i].id != nullptr ? all[i].id : "";
        CHECK(seen.insert(id).second);
        CHECK_FALSE(arurandeisu(all[i]).empty());
        if (i > 0) {
            const std::string previous_category =
                all[i - 1].category != nullptr ? all[i - 1].category : "";
            const std::string previous_id = all[i - 1].id != nullptr ? all[i - 1].id : "";
            CHECK((previous_category < category ||
                   (previous_category == category && previous_id <= id)));
        }
    }
}

TEST_CASE("类别过滤：只留选中类别、汇总重算、整轮耗时不动") {
    TomoeUdagawa report;
    report.platform = "windows";
    report.duration_ms = 999.0;
    const auto add = [&report](const char* id, const char* title, const char* category,
                               const char* status) {
        ArisaIchigaya item;
        item.id = id;
        item.title = title;
        item.category = category;
        item.status = status;
        report.results.push_back(item);
    };
    add("databases.ports", "数据库端口", "databases", kInfo);
    add("env.uac", "UAC", "env", kOk);
    add("env.smb1", "SMB1", "env", kFail);
    add("network.dns", "DNS", "network", kWarn);
    mizumiya_su(report);

    artia(report, {"env"});
    REQUIRE(report.results.size() == 2);
    CHECK(report.results[0].id == "env.uac");
    CHECK(report.results[1].id == "env.smb1");
    REQUIRE(report.summary.counts.size() == 2);
    CHECK(report.summary.counts[0].first == "ok");
    CHECK(report.summary.counts[0].second == 1);
    CHECK(report.summary.counts[1].first == "fail");
    REQUIRE(report.summary.problems.size() == 1);
    CHECK(report.summary.problems[0] == "[env.smb1] SMB1");
    // 耗时是整轮的墙钟，不随过滤重算（旧实现同样原样保留）
    CHECK(report.duration_ms == doctest::Approx(999.0));

    // 空表 = 不筛
    artia(report, {});
    CHECK(report.results.size() == 2);
}

TEST_CASE("人可读输出：分类折叠、统计与诊断结论逐字一致") {
    TomoeUdagawa report;
    report.platform = "windows";
    report.duration_ms = 1234.5;
    const auto add = [&report](const char* id, const char* title, const char* category,
                               const char* status, std::vector<std::string> detail,
                               std::optional<std::string> hint, double duration) {
        ArisaIchigaya item;
        item.id = id;
        item.title = title;
        item.category = category;
        item.status = status;
        item.detail = std::move(detail);
        item.hint = std::move(hint);
        item.duration_ms = duration;
        report.results.push_back(std::move(item));
    };
    // 顺序 = 引擎排序后的形态（按 category, id），结论清单的顺序跟着它走
    add("custom.thing", "自定义项", "custom", kWarn, {"不进折叠视图"}, "hint-here", 1.0);
    add("databases.ports", "数据库端口", "databases", kInfo, {"3306 未监听"}, std::nullopt, 400.1);
    add("env.uac", "UAC", "env", kOk, {}, std::nullopt, 0.2);
    add("hardware.disk", "磁盘空间", "hardware", kWarn, {"C: 剩余 5.0 GiB"}, "清理磁盘", 0.0);
    add("hardware.memory", "内存", "hardware", kOk, {"占用 41%"}, std::nullopt, 3.7);
    add("network.dns", "DNS", "network", kFail, {"解析失败"}, "检查网络", 5000.0);
    add("python.venv", "虚拟环境", "python", kTimeout, {"检测超时（>25s）"}, std::nullopt, 25000.0);
    add("toolchains.git", "Git", "toolchains", kSkip, {"未安装"}, std::nullopt, 1.0);
    mizumiya_su(report);

    const std::string expected =
        "▸ hardware  ⚠️1 ✅1\n"
        "▸ env  ✅1\n"
        "▸ toolchains  ⏭️1\n"
        "▸ network  ❌1\n"
        "▸ databases  ℹ️1\n"
        "▸ python  ⏱️1\n"
        "\n"
        "════ 诊断结论 ════\n"
        "统计: ⚠️2  ℹ️1  ✅2  ❌1  ⏱️1  ⏭️1\n"
        "发现 3 个需要关注的问题:\n"
        "  1. ⚠️ [custom.thing] 自定义项\n"
        "     ↳ hint-here\n"
        "  2. ⚠️ [hardware.disk] 磁盘空间\n"
        "     ↳ 清理磁盘\n"
        "  3. ❌ [network.dns] DNS\n"
        "     ↳ 检查网络";
    CHECK(kanade_izuru(report, {}, false, true, false) == expected);

    // 编码装不下装饰字符时整套切 ASCII 代用（旧实现按 stdout 编码探测后取另一套表）
    const std::string ascii =
        "> hardware  [!]1 [OK]1\n"
        "> env  [OK]1\n"
        "> toolchains  [-]1\n"
        "> network  [X]1\n"
        "> databases  [i]1\n"
        "> python  [T]1\n"
        "\n"
        "==== 诊断结论 ====\n"
        "统计: [!]2  [i]1  [OK]2  [X]1  [T]1  [-]1\n"
        "发现 3 个需要关注的问题:\n"
        "  1. [!] [custom.thing] 自定义项\n"
        "     -> hint-here\n"
        "  2. [!] [hardware.disk] 磁盘空间\n"
        "     -> 清理磁盘\n"
        "  3. [X] [network.dns] DNS\n"
        "     -> 检查网络";
    CHECK(kanade_izuru(report, {}, false, false, false) == ascii);

    // 着色只落在该着色的片段上：类别标题加粗、图标按状态着色
    const std::string colored = kanade_izuru(report, {}, false, true, true);
    CHECK(colored.find("\033[1m▸ hardware\033[0m") != std::string::npos);
    CHECK(colored.find("\033[33m⚠️\033[0m1") != std::string::npos);
    CHECK(colored.find("\033[32m✅\033[0m1") != std::string::npos);

    // 顶层 error：结论改成引擎异常，且不再报"发现 N 个问题"
    report.error = std::string("引擎内部 panic: boom");
    const std::string errored = kanade_izuru(report, {}, false, true, false);
    CHECK(errored.find("❌ 诊断引擎异常: 引擎内部 panic: boom") != std::string::npos);
    CHECK(errored.find("个需要关注的问题") == std::string::npos);

    // 全是 warn 以下的干净报告
    TomoeUdagawa clean;
    ArisaIchigaya only_ok;
    only_ok.id = "env.uac";
    only_ok.title = "UAC";
    only_ok.category = "env";
    only_ok.status = kOk;
    clean.results.push_back(only_ok);
    mizumiya_su(clean);
    CHECK(kanade_izuru(clean, {}, false, true, false)
              .find("✅ 未发现需要处理的问题") != std::string::npos);
}

TEST_CASE("未知类别不进人可读输出，但进统计、结论与导出") {
    TomoeUdagawa report;
    report.platform = "windows";
    ArisaIchigaya custom;
    custom.id = "custom.x";
    custom.title = "自定义项";
    custom.category = "custom";
    custom.status = kWarn;
    custom.detail = {"不进折叠视图"};
    report.results.push_back(custom);
    ArisaIchigaya env_item;
    env_item.id = "env.uac";
    env_item.title = "UAC";
    env_item.category = "env";
    env_item.status = kOk;
    report.results.push_back(env_item);
    mizumiya_su(report);

    const std::string body = kanade_izuru(report, {}, false, true, false);
    CHECK(body.find("▸ env") != std::string::npos);
    CHECK(body.find("▸ custom") == std::string::npos);  // 类别折叠只走固定清单
    // 折叠头每个类别只有一个（旧实现每个类别先打一行 `▸ {类别}  {计数}`），
    // 表外类别一个都不给。这里必须用字符串字面量比较：`▸` 是 3 字节 UTF-8，
    // 按 char 比会退化成一个字节，误命中 `⚠️` 里的变体选择符字节。
    CHECK(body.find("▸") == body.rfind("▸"));
    CHECK(body.find("发现 1 个需要关注的问题:") != std::string::npos);  // 但结论里照样算问题
    CHECK(body.find("[custom.x] 自定义项") != std::string::npos);

    // 导出是全量：表外类别只是"不显示"，不是"不存在"
    CHECK(achichi_mela(report).find("\"category\": \"custom\"") != std::string::npos);
    CHECK(yakushiji_suzaku(report).find("(`custom.x`)") != std::string::npos);
}

TEST_CASE("展开视图：-e 命中类别逐条展开，未命中保持折叠") {
    TomoeUdagawa report;
    report.platform = "windows";
    const auto add = [&report](const char* id, const char* title, const char* category,
                               const char* status, std::vector<std::string> detail,
                               std::optional<std::string> hint, double duration) {
        ArisaIchigaya item;
        item.id = id;
        item.title = title;
        item.category = category;
        item.status = status;
        item.detail = std::move(detail);
        item.hint = std::move(hint);
        item.duration_ms = duration;
        report.results.push_back(std::move(item));
    };
    add("env.uac", "UAC", "env", kOk, {}, std::nullopt, 0.2);
    add("env.smb1", "SMB1", "env", kWarn, {"SMB1 = 1", "建议关闭"}, "关闭 SMB1", 12.6);
    add("hardware.disk", "磁盘空间", "hardware", kOk, {"C: 剩余 100 GiB"}, std::nullopt, 3.0);
    mizumiya_su(report);

    // -e env：只有 env 展开；展开行只是多打的明细，不参与统计与结论
    const std::string expanded_env =
        "▸ hardware  ✅1\n"
        "▸ env  ✅1 ⚠️1\n"
        "  ✅ [env.uac] UAC  (0ms)\n"
        "  ⚠️ [env.smb1] SMB1  (13ms)\n"
        "      · SMB1 = 1\n"
        "      · 建议关闭\n"
        "      ↳ 建议: 关闭 SMB1\n"
        "\n"
        "════ 诊断结论 ════\n"
        "统计: ✅2  ⚠️1\n"
        "发现 1 个需要关注的问题:\n"
        "  1. ⚠️ [env.smb1] SMB1\n"
        "     ↳ 关闭 SMB1";
    CHECK(kanade_izuru(report, {"env"}, false, true, false) == expanded_env);

    // -E：全部类别展开（顺序仍是固定类别清单，条目按结果顺序）
    const std::string expanded_all =
        "▸ hardware  ✅1\n"
        "  ✅ [hardware.disk] 磁盘空间  (3ms)\n"
        "      · C: 剩余 100 GiB\n"
        "▸ env  ✅1 ⚠️1\n"
        "  ✅ [env.uac] UAC  (0ms)\n"
        "  ⚠️ [env.smb1] SMB1  (13ms)\n"
        "      · SMB1 = 1\n"
        "      · 建议关闭\n"
        "      ↳ 建议: 关闭 SMB1\n"
        "\n"
        "════ 诊断结论 ════\n"
        "统计: ✅2  ⚠️1\n"
        "发现 1 个需要关注的问题:\n"
        "  1. ⚠️ [env.smb1] SMB1\n"
        "     ↳ 关闭 SMB1";
    CHECK(kanade_izuru(report, {}, true, true, false) == expanded_all);

    // `-e` 给了不存在的类别：不展开、不报错；结果与完全不展开逐字相同
    CHECK(kanade_izuru(report, {"nosuchcat"}, false, true, false) ==
          kanade_izuru(report, {}, false, true, false));

    // 明细与建议符号随编码能力退化（`·`→`-`、`↳`→`->`），与折叠视图同源
    const std::string ascii = kanade_izuru(report, {"env"}, false, false, false);
    CHECK(ascii.find("      - SMB1 = 1") != std::string::npos);
    CHECK(ascii.find("      -> 建议: 关闭 SMB1") != std::string::npos);

    // 着色：id 用暗色、图标按状态、建议行整体黄色
    const std::string colored = kanade_izuru(report, {"env"}, false, true, true);
    CHECK(colored.find("\033[33m⚠️\033[0m [env.smb1]") != std::string::npos);
    CHECK(colored.find("\033[90menv.smb1\033[0m") != std::string::npos);
    CHECK(colored.find("\033[33m      ↳ 建议: 关闭 SMB1\033[0m") != std::string::npos);
}

TEST_CASE("展开行组与展开判定") {
    ArisaIchigaya item;
    item.id = "env.smb1";
    item.title = "SMB1";
    item.category = "env";
    item.status = kWarn;
    item.detail = {"SMB1 = 1", "建议关闭"};
    item.hint = std::string("关闭 SMB1");
    item.duration_ms = 12.6;

    const std::vector<std::string> lines = debidebi_debiru(item, true, false);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "  ⚠️ [env.smb1] SMB1  (13ms)");
    CHECK(lines[1] == "      · SMB1 = 1");
    CHECK(lines[2] == "      · 建议关闭");
    CHECK(lines[3] == "      ↳ 建议: 关闭 SMB1");

    // 没有明细也没有建议时只留图标行；ASCII 代用同样生效
    item.detail.clear();
    item.hint.reset();
    const std::vector<std::string> bare = debidebi_debiru(item, false, false);
    REQUIRE(bare.size() == 1);
    CHECK(bare[0] == "  [!] [env.smb1] SMB1  (13ms)");

    // 展开判定：`-E` 一律展开；`-e` 全等比较、大小写敏感、不给就不展开
    CHECK(rindou_mikoto({}, true, "env"));
    CHECK(rindou_mikoto({"env"}, false, "env"));
    CHECK_FALSE(rindou_mikoto({"env"}, false, "hardware"));
    CHECK_FALSE(rindou_mikoto({}, false, "env"));
    CHECK_FALSE(rindou_mikoto({"ENV"}, false, "env"));
}

TEST_CASE("展开只改控制台正文：统计、结论与导出不随之变化") {
    TomoeUdagawa report;
    report.platform = "windows";
    ArisaIchigaya ok_item;
    ok_item.id = "env.uac";
    ok_item.title = "UAC";
    ok_item.category = "env";
    ok_item.status = kOk;
    ok_item.detail = {"已启用"};
    report.results.push_back(ok_item);
    ArisaIchigaya warn_item;
    warn_item.id = "hardware.disk";
    warn_item.title = "磁盘空间";
    warn_item.category = "hardware";
    warn_item.status = kWarn;
    warn_item.detail = {"C: 剩余 5 GiB"};
    warn_item.hint = std::string("清理磁盘");
    report.results.push_back(warn_item);
    mizumiya_su(report);

    const std::string folded = kanade_izuru(report, {}, false, true, false);
    const std::string expanded_env = kanade_izuru(report, {"env"}, false, true, false);
    const std::string expanded_all = kanade_izuru(report, {}, true, true, false);
    CHECK(folded != expanded_env);
    CHECK(expanded_env != expanded_all);

    // 三种视图的"统计 + 结论"必须一字不差：展开只是多打了明细行
    const auto tail_of = [](const std::string& text) { return text.substr(text.find("统计: ")); };
    CHECK(tail_of(folded) == tail_of(expanded_env));
    CHECK(tail_of(expanded_env) == tail_of(expanded_all));

    // 导出两个函数都不接收展开参数：展开前后逐字节相同，且不含控制台的缩进明细行
    const std::string json = achichi_mela(report);
    const std::string markdown = yakushiji_suzaku(report);
    CHECK(achichi_mela(report) == json);
    CHECK(yakushiji_suzaku(report) == markdown);
    CHECK(markdown.find("      · ") == std::string::npos);
    CHECK(json.find("[进度]") == std::string::npos);
}

TEST_CASE("进度行：文本格式与终端门控") {
    // 格式：`\r[进度] {done}/{total}` + 两个空格 + 当前项 + 三个空格。
    // 空格数用 std::string(n, ' ') 写出来而不是手敲：手敲的空格数在评审里数不清，
    // 而"两个分隔 + 三个收尾"是有依据的（旧实现 `f"\r[进度] {done}/{total_n}  {current}   "`，
    // 收尾空格负责盖掉上一行更长的残影）。
    CHECK(machita_chima(1, 10, "env.uac") ==
          std::string("\r[进度] 1/10  ") + "env.uac" + std::string(3, ' '));
    CHECK(machita_chima(0, 0, "") == std::string("\r[进度] 0/0") + std::string(5, ' '));
    CHECK(machita_chima(2, 3, "env.uac").rfind("\r", 0) == 0);  // 以 CR 开头才谈得上原地刷新

    // 门控：`enabled=false` 一个字节都不写（重定向后的输出必须与没有进度时逐字节一致），
    // `enabled=true` 原样落盘（不补换行）。stdout 临时切到 tmpfile 来验，切之前先 flush：
    // 之前只有断言"捕获内容以 `\r[` 开头"，结果被上一条测试留在 stdout 缓冲区里的字节
    // 顶到了前面 —— 那种前缀断言测的是"没人往 stdout 写别的"，不是进度本身的行为。
    std::fflush(stdout);
    std::FILE* capture = nullptr;
    const bool capture_ok = tmpfile_s(&capture) == 0 && capture != nullptr;
    const int stdout_fd = _fileno(stdout);
    const int saved = capture_ok ? _dup(stdout_fd) : -1;
    const bool swapped =
        saved >= 0 && capture_ok && _dup2(_fileno(capture), stdout_fd) == 0;
    if (swapped) {
        belmond_banderas("NOPE", false);
        belmond_banderas(machita_chima(2, 3, "env.uac"), true);
        std::fflush(stdout);
        (void)_dup2(saved, stdout_fd);  // 先把 stdout 换回去，后面的断言才敢失败
    }
    if (saved >= 0) {
        _close(saved);
    }
    CHECK(swapped);  // 捕获装置本身失败要看得见，而不是静默跳过整段
    if (swapped) {
        std::fseek(capture, 0, SEEK_END);
        const long size = std::ftell(capture);
        std::fseek(capture, 0, SEEK_SET);
        std::string got;
        if (size > 0) {
            got.resize(static_cast<size_t>(size));
            (void)std::fread(got.data(), 1, got.size(), capture);
        }
        // 与 `belmond_banderas` 同源：进度文本在写出去时会按控制台编码降级（重定向下走 ANSI 代码页），
        // 所以期望值也要过一遍同一个函数，否则断言的是编码而不是行为。
        const std::string expected = hoshimachi_suisei(machita_chima(2, 3, "env.uac"));
        CHECK(got.find("NOPE") == std::string::npos);  // enabled=false → 什么都没写
        CHECK(got.size() >= expected.size());
        if (got.size() >= expected.size()) {
            // enabled=true 的进度行是最后写出的内容（进度是覆盖式输出，末尾必须是它）
            CHECK(got.compare(got.size() - expected.size(), expected.size(), expected) == 0);
        }
        CHECK(got.find('\r') != std::string::npos);  // CR 在捕获里确实出现过
    }
    if (capture != nullptr) {
        std::fclose(capture);
    }
}

TEST_CASE("导出：--json 与 --txt 的文本与落盘形态") {
    TomoeUdagawa report;
    report.platform = "windows";
    report.duration_ms = 1235.0;
    ArisaIchigaya ok_item;
    ok_item.id = "env.uac";
    ok_item.title = "UAC";
    ok_item.category = "env";
    ok_item.status = kOk;
    ok_item.detail = {"已启用"};
    ok_item.hint = std::string("无需处理");
    report.results.push_back(ok_item);
    ArisaIchigaya warn_item;
    warn_item.id = "custom.x";
    warn_item.title = "自定义项";
    warn_item.category = "custom";
    warn_item.status = kWarn;
    warn_item.detail = {"不进折叠视图"};
    report.results.push_back(warn_item);
    mizumiya_su(report);

    const std::string markdown =
        "# 环境诊断报告\n"
        "\n"
        "- 平台: windows\n"
        "- 耗时: 1235ms\n"
        "\n"
        "## [ok] UAC (`env.uac`)\n"
        "- 已启用\n"
        "- 建议: 无需处理\n"
        "\n"
        "## [warn] 自定义项 (`custom.x`)\n"
        "- 不进折叠视图\n";
    CHECK(yakushiji_suzaku(report) == markdown);

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "envdoctor_cli_export";
    std::error_code code;
    std::filesystem::remove_all(dir, code);
    std::filesystem::create_directories(dir, code);
    const std::string json_path = robocosan((dir / "report.json").wstring());
    const std::string txt_path = robocosan((dir / "report.txt").wstring());

    std::string error;
    CHECK(astel_leda(json_path, achichi_mela(report), error));
    CHECK(error.empty());
    CHECK(astel_leda(txt_path, markdown, error));
    CHECK(error.empty());

    const auto read_bytes = [](const std::string& path) {
        std::ifstream file(tokino_sora(path).c_str(), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };
    // 落盘口径：`\n` 翻成 `\r\n`（旧实现走 Python 文本模式写入，Windows 上就是这个形态）
    const auto crlf = [](const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '\n') {
                out += "\r\n";
            } else {
                out.push_back(c);
            }
        }
        return out;
    };
    const std::string json_bytes = read_bytes(json_path);
    const std::string txt_bytes = read_bytes(txt_path);
    CHECK(json_bytes == crlf(achichi_mela(report)));
    CHECK(txt_bytes == crlf(markdown));
    CHECK(json_bytes.rfind("\xEF\xBB\xBF", 0) != 0);            // 不带 BOM
    CHECK(json_bytes.back() == '}');                            // JSON 末尾不追加换行
    CHECK(txt_bytes.substr(txt_bytes.size() - 2) == "\r\n");     // 文本末行之后是块尾空行
    CHECK(json_bytes.find("\\u") == std::string::npos);          // 中文不转义
    CHECK(json_bytes.find("\"title\": \"自定义项\"") != std::string::npos);
    std::filesystem::remove_all(dir, code);

    // 写不进去（父目录不存在）时必须报错，而不是"看起来导出了"
    std::string failed;
    CHECK_FALSE(astel_leda(robocosan((dir / "no_such_dir" / "x.json").wstring()), "x", failed));
    CHECK_FALSE(failed.empty());
    CHECK(minase_rio("报告写入失败: " + failed, false) == 2);
}

TEST_CASE("退出码判定：顶层 error 与 fail 才算失败") {
    TomoeUdagawa report;
    const auto push = [&report](const char* status) {
        ArisaIchigaya item;
        item.id = "x.demo";
        item.title = "演示";
        item.category = "env";
        item.status = status;
        report.results.push_back(item);
    };
    push(kOk);
    push(kWarn);
    push(kInfo);
    push(kSkip);
    push(kTimeout);
    CHECK(yukoku_roberu(report) == 0);  // warn/info/skip/timeout 都不算失败

    push(kFail);
    CHECK(yukoku_roberu(report) == 1);

    TomoeUdagawa errored;
    errored.error = std::string("引擎内部 panic: boom");
    CHECK(yukoku_roberu(errored) == 1);  // 顶层 error 即使一条结果都没有也是 1
}

TEST_CASE("--require 的语义：逗号拆分、去空白与未知名字") {
    const std::vector<std::string> known = {"cargo", "git", "make", "node"};
    CHECK(aragami_oga({"git", "make"}, known).empty());

    const std::vector<std::string> unknown = aragami_oga({"git", "nosuch", "git"}, known);
    REQUIRE(unknown.size() == 1);
    CHECK(unknown[0] == "nosuch");

    // 全等比较：大小写敏感、不做前缀匹配（引擎侧就是这么比的）
    CHECK(aragami_oga({"Git"}, known).size() == 1);
    CHECK(aragami_oga({"gi"}, known).size() == 1);

    // 逗号拆分、去空白、丢空段；名字原样转发给引擎（不认识的也不丢，只是无效）
    const Rosalyn opts =
        rosalyn({"run", "--require", " git , ,make ", "--require", "node", "--require", "nosuch"});
    CHECK(opts.error.empty());
    REQUIRE(opts.cfg.required.size() == 4);
    CHECK(opts.cfg.required[0] == "git");
    CHECK(opts.cfg.required[1] == "make");
    CHECK(opts.cfg.required[2] == "node");
    CHECK(opts.cfg.required[3] == "nosuch");
}

TEST_CASE("--scan-root：非目录告警、转绝对路径与按序去重") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "envdoctor_cli_scan";
    std::error_code code;
    std::filesystem::remove_all(dir, code);
    std::filesystem::create_directories(dir, code);
    const std::string dir_text = robocosan(dir.wstring());

    std::vector<std::string> warnings;
    const std::string missing = dir_text + "\\no_such_sub";
    const std::vector<std::string> roots = kageyama_shien({dir_text, dir_text, missing}, warnings);
    REQUIRE(roots.size() == 1);  // 重复的扫描根只留一份
    CHECK(roots[0].size() > 2);
    CHECK(roots[0][1] == ':');        // 已落成绝对路径
    CHECK(shishiro_botan(roots[0]));  // 落地后仍指向同一个目录
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "警告: --scan-root 不是已存在的目录，已忽略: " + missing);
    std::filesystem::remove_all(dir, code);

    // `~` 展开：家目录存在时应当落地成功（不存在则不猜）
    if (shishiro_botan(takane_lui())) {
        std::vector<std::string> tilde_warnings;
        const std::vector<std::string> tilde_roots = kageyama_shien({"~"}, tilde_warnings);
        CHECK(tilde_warnings.empty());
        REQUIRE(tilde_roots.size() == 1);
        CHECK(shishiro_botan(tilde_roots[0]));
    }

    // 一个 root 都没给：projects.* 记 skip 的依据就是空表
    std::vector<std::string> none_warnings;
    CHECK(kageyama_shien({}, none_warnings).empty());
    CHECK(none_warnings.empty());
}

TEST_CASE("入口分派：退出码与未实现子命令") {
    const auto run_main = [](std::vector<std::string> args) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("envdoctor"));
        for (std::string& arg : args) {
            argv.push_back(arg.data());
        }
        return doris(static_cast<int>(argv.size()), argv.data());
    };
    CHECK(run_main({"--version"}) == 0);
    CHECK(run_main({"--help"}) == 0);
    CHECK(run_main({"--list-checks"}) == 0);
#ifdef ENVDECTOR_WITH_UI
    // say no to perv. —— 早先这里无条件断言 tui/gui 退出 2，但那只对 UI-OFF 构建成立；
    // 前端编入时 doris 会进真实界面循环，默认构建下测试套件在第一条上就挂死（实测复现）。
    // 解析层认得这两个子命令已由"参数解析"用例覆盖；退出码 2 的分支只在下面的
    // UI-OFF 分支里存在，据此验证。
#else
    // 未实现/不可用一律非 0，绝不假装跑完再返回 0
    CHECK(run_main({"tui"}) == 2);
    CHECK(run_main({"gui"}) == 2);
#endif
    CHECK(run_main({"--nope"}) == 2);
    CHECK(run_main({"run", "--timeout", "0"}) == 2);
    CHECK(run_main({"-c", "nosuchcat"}) == 2);
    CHECK(run_main({"run", "extra"}) == 2);
}

TEST_CASE("Ctrl+C 处理器：装卸不改变令牌状态") {
    HinaHikawa cancel;
    hizaki_gamma(&cancel);
    CHECK_FALSE(cancel.flag.load());  // 没有真实信号时令牌不能被谁顺手置位
    hizaki_gamma(nullptr);
    CHECK_FALSE(cancel.flag.load());
}
