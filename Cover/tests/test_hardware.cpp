// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 硬件类检查的测试。
//
// 分两块：① 目录形状与判定纯函数 —— 逐字段断言，不碰宿主环境；② 真跑一轮 ——
// 只断言"状态取值合法、有结论必有依据、取不到不写成没配置、探针不留文件"。
// 本机装没装 powershell、有几块盘、电量多少这类结论**不断言**：那种断言只会在
// 换一台机器时变红，而它想保护的行为（分支选择）由第一块覆盖。

#include <doctest/doctest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/fs.h"
#include "base/status.h"
#include "base/win32.h"
#include "checks/hardware.h"
#include "engine/engine.h"

using namespace envdoctor;

TEST_CASE("硬件模块目录：id/标题/类别/平台门与报告契约一致") {
    const std::vector<HimariUehara> all = regis_altare();
    const std::vector<std::pair<std::string, std::string>> want{
        {"hardware.memory", "内存"},
        {"hardware.uptime", "开机时长"},
        {"hardware.battery", "电池"},
        {"hardware.disk", "系统盘空间"},
        {"hardware.gpu", "显卡"},
        {"hardware.cpu_features", "CPU 指令集"},
        {"hardware.pagefile", "页面文件"},
        {"hardware.smart", "磁盘健康 (SMART)"},
        {"hardware.power_plan", "电源计划"},
        {"hardware.temp", "临时目录"},
        {"hardware.disk_io", "磁盘写入"},
    };
    REQUIRE(all.size() == want.size());
    std::set<std::string> ids;
    for (size_t i = 0; i < want.size(); ++i) {
        CHECK(std::string(all[i].id) == want[i].first);
        CHECK(std::string(all[i].title) == want[i].second);
        CHECK(std::string(all[i].category) == "hardware");
        REQUIRE(all[i].platforms.size() == 1);  // 这几项只有 Windows 实现
        CHECK(std::string(all[i].platforms[0]) == "windows");
        CHECK(all[i].fn != nullptr);
        CHECK(ids.insert(all[i].id).second);  // 重复 id 会让结果互相覆盖
    }
}

TEST_CASE("单位与小数位：GiB 口径固定 1024³，文本是定点小数") {
    CHECK(mononobe_alice(0) == 0.0);
    CHECK(mononobe_alice(1024ULL * 1024 * 1024) == doctest::Approx(1.0));
    CHECK(mononobe_alice(16ULL * 1024 * 1024 * 1024) == doctest::Approx(16.0));
    CHECK(kenmochi_toya(16.0, 2) == "16.00");
    CHECK(kenmochi_toya(16.0, 0) == "16");
    CHECK(kenmochi_toya(1.234, 1) == "1.2");
    CHECK(kenmochi_toya(1.29, 1) == "1.3");
    CHECK(kenmochi_toya(12.8, 0) == "13");
}

TEST_CASE("CPU 特性位判定：只有缺 AVX2 才是可行动的问题") {
    const RanMitake ok = suzuka_utako(true, false);
    CHECK(ok.status == kOk);
    REQUIRE(ok.detail.size() == 2);
    CHECK(ok.detail[0] == "AVX2: 支持");
    CHECK(ok.detail[1] == "AVX-512F: 未报告支持（较老系统可能不识别该特性位）");
    CHECK(!ok.hint.has_value());

    const RanMitake avx512 = suzuka_utako(true, true);
    CHECK(avx512.status == kOk);
    REQUIRE(avx512.detail.size() == 2);
    CHECK(avx512.detail[1] == "AVX-512F: 支持");

    // 没探测到的特性位不能凭空补一行
    const RanMitake unknown512 = suzuka_utako(true, std::nullopt);
    CHECK(unknown512.status == kOk);
    CHECK(unknown512.detail.size() == 1);

    const RanMitake no_avx2 = suzuka_utako(false, std::nullopt);
    CHECK(no_avx2.status == kWarn);
    REQUIRE(no_avx2.detail.size() == 1);
    CHECK(no_avx2.detail[0] == "AVX2: 不支持");
    REQUIRE(no_avx2.hint.has_value());
    CHECK(no_avx2.hint->find("AVX2") != std::string::npos);
}

TEST_CASE("页面文件判定：空配置才 warn，系统托管与手工配置都 ok") {
    const RanMitake none = akabane_youko({});
    CHECK(none.status == kWarn);
    REQUIRE(none.detail.size() == 1);
    CHECK(none.detail[0] == "未配置任何页面文件（或已全部禁用）");
    REQUIRE(none.hint.has_value());
    CHECK(none.hint->find("页面文件") != std::string::npos);

    const RanMitake managed = akabane_youko({"?:\\pagefile.sys"});
    CHECK(managed.status == kOk);
    REQUIRE(managed.detail.size() == 2);
    CHECK(managed.detail[0] == "页面文件: 系统托管");
    CHECK(managed.detail[1] == "  ?:\\pagefile.sys");
    CHECK(!managed.hint.has_value());

    // 卷影路径形态同样算系统托管
    CHECK(akabane_youko({"\\??\\C:\\pagefile.sys"}).detail[0] == "页面文件: 系统托管");

    const RanMitake manual = akabane_youko({"C:\\pagefile.sys", "D:\\pagefile.sys"});
    CHECK(manual.status == kOk);
    REQUIRE(manual.detail.size() == 3);
    CHECK(manual.detail[0] == "页面文件: 手工配置");
    CHECK(manual.detail[1] == "  C:\\pagefile.sys");
    CHECK(manual.detail[2] == "  D:\\pagefile.sys");
}

TEST_CASE("磁盘状态判定：驱动不上报与解析失败都只作 info") {
    const RanMitake mixed = honma_himawari({"Samsung SSD|OK", "WDC HDD|Pred Fail"});
    CHECK(mixed.status == kWarn);
    REQUIRE(mixed.detail.size() == 2);
    CHECK(mixed.detail[0] == "Samsung SSD → OK");
    CHECK(mixed.detail[1] == "WDC HDD → Pred Fail");
    REQUIRE(mixed.hint.has_value());
    // 判据是驱动上报的状态，建议文案要如实说清数据源，并点名是哪块盘
    CHECK(mixed.hint->find("SMART") != std::string::npos);
    CHECK(mixed.hint->find("Pred Fail") != std::string::npos);

    const RanMitake ok = honma_himawari({"Samsung SSD|OK"});
    CHECK(ok.status == kOk);
    CHECK(ok.detail.size() == 1);
    CHECK(!ok.hint.has_value());

    // 大小写不敏感
    CHECK(honma_himawari({"NVMe|ok"}).status == kOk);
    CHECK(honma_himawari({"NVMe|OK"}).status == kOk);
    CHECK(honma_himawari({"NVMe|Degraded"}).status == kWarn);

    // 空行忽略；只有空行等于没有数据
    const RanMitake blanks = honma_himawari({"", "   ", "Samsung SSD|OK"});
    CHECK(blanks.status == kOk);
    CHECK(blanks.detail.size() == 1);
    const RanMitake empty = honma_himawari({});
    CHECK(empty.status == kSkip);
    REQUIRE(empty.detail.size() == 1);
    CHECK(empty.detail[0] == "未获取到磁盘信息");
}

TEST_CASE("磁盘状态判定（回归）：未上报不是'盘坏了'") {
    const RanMitake unknown = honma_himawari({"USB Enclosure|Unknown"});
    CHECK(unknown.status == kInfo);
    REQUIRE(unknown.detail.size() == 1);
    CHECK(unknown.detail[0] == "USB Enclosure → 状态未上报");
    REQUIRE(unknown.hint.has_value());
    CHECK(unknown.hint->find("未上报") != std::string::npos);

    // 没有 `|` 的行是探针输出形态异常，同样只能归入"未上报"
    const RanMitake malformed = honma_himawari({"some unexpected line"});
    CHECK(malformed.status == kInfo);
    REQUIRE(malformed.detail.size() == 1);
    CHECK(malformed.detail[0] == "some unexpected line → 状态未上报");

    // 未上报与明确异常混在一起时，明确异常优先
    const RanMitake both = honma_himawari({"A|Unknown", "B|Degraded"});
    CHECK(both.status == kWarn);
    CHECK(both.detail.size() == 2);
}

TEST_CASE("临时目录判定：探针失败 fail 优先于空间不足") {
    const RanMitake ok = shiina_yuika(50.0, true, {"临时目录: C:\\Temp"});
    CHECK(ok.status == kOk);
    REQUIRE(ok.detail.size() == 2);
    CHECK(ok.detail[0] == "临时目录: C:\\Temp");
    CHECK(ok.detail[1] == "读写探针: 通过");
    CHECK(!ok.hint.has_value());

    const RanMitake low = shiina_yuika(0.5, true, {"临时目录: C:\\Temp"});
    CHECK(low.status == kWarn);
    REQUIRE(low.detail.size() == 3);
    CHECK(low.detail[1] == "读写探针: 通过");
    CHECK(low.detail[2] == "可用空间不足 2GB，构建与解包可能中途失败");

    // 写不进去时只报探针失败：空间再大也不改判定，也不补"空间不足"那行
    const RanMitake broken = shiina_yuika(50.0, false, {"临时目录: C:\\Temp"});
    CHECK(broken.status == kFail);
    REQUIRE(broken.detail.size() == 2);
    CHECK(broken.detail[1] == "读写探针: 失败");

    // 2GB 是"不足"的下界：等于阈值不算不足
    CHECK(shiina_yuika(2.0, true, {}).status == kOk);
    CHECK(shiina_yuika(1.999, true, {}).status == kWarn);
    CHECK(shiina_yuika(0.0, true, {}).status == kWarn);
}

TEST_CASE("写入耗时判定：正常只报数，异常慢才 warn") {
    const RanMitake fast = setsuna(12.8);
    CHECK(fast.status == kInfo);
    REQUIRE(fast.detail.size() == 1);
    CHECK(fast.detail[0] == "1MB 写入 + fsync: 13ms");
    CHECK(!fast.hint.has_value());

    // 5400 转机械盘合法地会超 300ms：那是 info 不是 warn
    CHECK(setsuna(400.0).status == kInfo);
    CHECK(setsuna(1000.0).status == kInfo);  // 阈值是"大于 1000ms"

    const RanMitake slow = setsuna(1500.0);
    CHECK(slow.status == kWarn);
    REQUIRE(slow.detail.size() == 1);
    CHECK(slow.detail[0] == "1MB 写入 + fsync: 1500ms");
    REQUIRE(slow.hint.has_value());
    CHECK(slow.hint->find("杀软") != std::string::npos);
}

TEST_CASE("端到端：跑得出合法结果，取不到不当结论，探针不留文件") {
    MocaAoba cfg;
    cfg.timeout_secs = 25;
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(regis_altare(), cfg, cancel);
    REQUIRE(report.results.size() == 11);
    CHECK(report.report_version == 1);
    CHECK(report.platform == "windows");
    CHECK(report.source == "cpp");

    const auto find = [&report](const char* id) -> const ArisaIchigaya* {
        for (const ArisaIchigaya& item : report.results) {
            if (item.id == id) {
                return &item;
            }
        }
        return nullptr;
    };

    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(item.category == "hardware");
        CHECK(item.duration_ms >= 0.0);
        if (item.status == kOk || item.status == kWarn || item.status == kInfo ||
            item.status == kTimeout) {
            CHECK(!item.detail.empty());  // 有结论就必须有依据
        }
        if (item.status == kTimeout) {
            CHECK(item.detail.at(0).find("超时") != std::string::npos);
        }
        // "取不到"（超时 / 跳过）不许写成"没配置 / 没装"：这两句会把人送去改一台正常的机器
        if (item.status == kTimeout || item.status == kSkip) {
            for (const std::string& line : item.detail) {
                CHECK(line.find("未配置") == std::string::npos);
                CHECK(line.find("未安装") == std::string::npos);
            }
        }
    }

    // 页面文件：读不到只能"本次不判断"，绝不能出现"未配置任何页面文件"
    const ArisaIchigaya* pagefile = find("hardware.pagefile");
    REQUIRE(pagefile != nullptr);
    REQUIRE(!pagefile->detail.empty());
    if (pagefile->status == kSkip) {
        const std::string& first = pagefile->detail.at(0);
        const bool read_failed_only =
            first.find("不判断页面文件配置") != std::string::npos ||
            first.rfind("读取注册表失败", 0) == 0;
        CHECK(read_failed_only);
    } else {
        const bool conclusive = pagefile->status == kOk || pagefile->status == kWarn;
        CHECK(conclusive);
    }

    // 磁盘健康：info 只可能来自"驱动未上报"，不可能来自"磁盘有毛病"
    const ArisaIchigaya* smart = find("hardware.smart");
    REQUIRE(smart != nullptr);
    if (smart->status == kInfo) {
        REQUIRE(smart->hint.has_value());
        CHECK(smart->hint->find("未上报") != std::string::npos);
        CHECK(smart->hint->find("更换") == std::string::npos);
    }
    if (smart->status == kSkip) {
        REQUIRE(!smart->detail.empty());
        const std::string& first = smart->detail.at(0);
        const bool no_data = first == "未获取到磁盘信息" || first == "未找到 powershell";
        CHECK(no_data);
    }

    // 宿主相关分支只断言文案形态
    const ArisaIchigaya* memory = find("hardware.memory");
    REQUIRE(memory != nullptr);
    if (memory->status == kOk || memory->status == kWarn) {
        REQUIRE(!memory->detail.empty());
        CHECK(memory->detail.at(0).rfind("总内存 ", 0) == 0);
        CHECK(memory->detail.at(0).find("（物理内存占用 ") != std::string::npos);
    }

    const ArisaIchigaya* uptime = find("hardware.uptime");
    REQUIRE(uptime != nullptr);
    REQUIRE(!uptime->detail.empty());
    CHECK(uptime->detail.at(0).rfind("系统已运行 ", 0) == 0);
    CHECK(uptime->detail.at(0).find(" 分钟") != std::string::npos);

    const ArisaIchigaya* cpu = find("hardware.cpu_features");
    REQUIRE(cpu != nullptr);
    REQUIRE(!cpu->detail.empty());
    const bool avx2_line =
        cpu->detail.at(0) == "AVX2: 支持" || cpu->detail.at(0) == "AVX2: 不支持";
    CHECK(avx2_line);

    const ArisaIchigaya* disk = find("hardware.disk");
    REQUIRE(disk != nullptr);
    if (disk->status == kOk || disk->status == kWarn) {
        REQUIRE(!disk->detail.empty());
        CHECK(disk->detail.at(0).find(" 总 ") != std::string::npos);
        CHECK(disk->detail.at(0).find("已用 ") != std::string::npos);
        CHECK(disk->detail.at(0).find("可用 ") != std::string::npos);
    }

    const ArisaIchigaya* temp = find("hardware.temp");
    REQUIRE(temp != nullptr);
    REQUIRE(!temp->detail.empty());
    CHECK(temp->detail.at(0).rfind("临时目录: ", 0) == 0);

    // 两个写探针都是"创建即删"：跑完不该在临时目录里留下文件
    const auto temp_env = uruha_rushia(L"TEMP");
    const auto tmp_env = uruha_rushia(L"TMP");
    std::string dir = "C:\\Temp";
    if (temp_env) {
        dir = *temp_env;
    } else if (tmp_env) {
        dir = *tmp_env;
    }
    CHECK(!momosuzu_nene(kazama_iroha(dir, ".envdoctor_probe")));
    CHECK(!momosuzu_nene(kazama_iroha(dir, ".envdoctor_io_probe")));
}
