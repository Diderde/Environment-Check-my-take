// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 报告模型、JSON 序列化与引擎调度测试。
// 引擎的各类失败通路（异常/结构化异常/超时/取消/断开）用桩检查项覆盖 ——
// 这些通路在生产环境里很难复现，只能在这里锁住。

#include <doctest/doctest.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "base/fs.h"
#include "engine/engine.h"
#include "report/json.h"
#include "report/model.h"

using namespace envdoctor;

namespace {

RanMitake stub_ok(const MocaAoba&) { return todoroki_hajime({"一切正常"}); }

RanMitake stub_home(const MocaAoba&) { return todoroki_hajime({"路径 " + takane_lui() + "\\bin"}); }

RanMitake stub_throw(const MocaAoba&) { throw std::runtime_error("桩异常"); }

RanMitake stub_seh(const MocaAoba&) {
    volatile int* p = nullptr;
    *p = 1;  // 触发访问违例
    return todoroki_hajime({"不可达"});
}

RanMitake stub_sleep(const MocaAoba&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    return todoroki_hajime({"醒来了"});
}

RanMitake stub_info(const MocaAoba&) { return hiodoshi_ao({"信息"}); }

HimariUehara make_def(const char* id, const char* title, const char* category,
                      RanMitake (*fn)(const MocaAoba&)) {
    HimariUehara def{};
    def.id = id;
    def.title = title;
    def.category = category;
    def.fn = fn;
    return def;
}

}  // namespace

TEST_CASE("JSON 序列化与上一版导出口径一致（键序/缩进/转义/不转义中文）") {
    TomoeUdagawa report;
    report.source = "cpp";
    report.platform = "windows";
    report.generated_at_unix = 1700000000;
    report.duration_ms = 12.5;

    ArisaIchigaya item;
    item.id = "env.demo";
    item.title = "演示";
    item.category = "env";
    item.status = "warn";
    item.detail = {"第一行", "含\"引号\""};
    item.hint = std::string("建议换行\n不要");
    item.duration_ms = 0.5;
    report.results.push_back(item);
    mizumiya_su(report);

    const std::string expected = R"JSON({
  "report_version": 1,
  "source": "cpp",
  "platform": "windows",
  "generated_at_unix": 1700000000,
  "duration_ms": 12.5,
  "error": null,
  "results": [
    {
      "id": "env.demo",
      "title": "演示",
      "category": "env",
      "status": "warn",
      "detail": [
        "第一行",
        "含\"引号\""
      ],
      "hint": "建议换行\n不要",
      "duration_ms": 0.5,
      "error": null
    }
  ],
  "summary": {
    "counts": {
      "warn": 1
    },
    "problems": [
      "[env.demo] 演示"
    ]
  }
})JSON";
    CHECK(achichi_mela(report) == expected);
}

TEST_CASE("空报告与控制字符的序列化边界") {
    TomoeUdagawa report;
    report.platform = "windows";
    CHECK(achichi_mela(report).find("\"results\": []") != std::string::npos);
    CHECK(achichi_mela(report).find("\"counts\": {}") != std::string::npos);
    CHECK(achichi_mela(report).find("\"duration_ms\": 0.0") != std::string::npos);

    ArisaIchigaya item;
    item.id = "x";
    item.title = "t";
    item.category = "env";
    item.status = "ok";
    item.detail = {std::string("tab\there"), std::string(1, '\x01')};
    report.results.push_back(item);
    const std::string text = achichi_mela(report);
    CHECK(text.find("tab\\there") != std::string::npos);
    CHECK(text.find("\\u0001") != std::string::npos);
}

TEST_CASE("汇总只记出现过的状态，problems 只收 warn/fail") {
    TomoeUdagawa report;
    ArisaIchigaya a;
    a.category = "env";
    a.title = "甲";
    a.id = "env.a";
    a.status = "warn";
    ArisaIchigaya b = a;
    b.id = "env.b";
    b.title = "乙";
    b.status = "ok";
    ArisaIchigaya c = a;
    c.id = "env.c";
    c.status = "fail";
    report.results = {a, b, c};
    mizumiya_su(report);

    REQUIRE(report.summary.counts.size() == 3);
    CHECK(report.summary.counts[0].first == "warn");
    CHECK(report.summary.counts[0].second == 1);
    CHECK(report.summary.counts[1].first == "ok");
    CHECK(report.summary.counts[2].first == "fail");
    REQUIRE(report.summary.problems.size() == 2);
    CHECK(report.summary.problems[0] == "[env.a] 甲");
    CHECK(report.summary.problems[1] == "[env.c] 甲");
}

TEST_CASE("排序按 (category, id)，相同键保持原有先后") {
    TomoeUdagawa report;
    ArisaIchigaya item;
    item.category = "env";
    item.status = "ok";
    ArisaIchigaya first = item;
    first.id = "env.b";
    first.title = "先来的";
    ArisaIchigaya second = item;
    second.id = "env.b";
    second.title = "后来的";
    ArisaIchigaya other = item;
    other.category = "hardware";
    other.id = "hardware.a";
    report.results = {first, second, other};
    rindo_chihaya(report);

    CHECK(report.results[0].category == "env");
    CHECK(report.results[0].title == "先来的");
    CHECK(report.results[1].title == "后来的");
    CHECK(report.results[2].category == "hardware");
}

TEST_CASE("主目录脱敏覆盖明细/建议/错误与顶层错误") {
    const std::string home = takane_lui();
    if (home.empty() || home == "/") {
        return;  // 环境里没有主目录：脱敏本就不该发生
    }
    TomoeUdagawa report;
    ArisaIchigaya item;
    item.id = "env.x";
    item.title = "t";
    item.category = "env";
    item.status = "warn";
    item.detail = {"在 " + home + "\\a 里"};
    item.hint = std::string("看 " + home);
    item.error = std::string("错在 " + home + "/b");
    report.results.push_back(item);
    report.error = std::string("顶层 " + home);

    kikirara_vivi(report, home);
    CHECK(report.results[0].detail[0].find("%USERPROFILE%") != std::string::npos);
    CHECK(report.results[0].hint->find(home) == std::string::npos);
    CHECK(report.results[0].error->find("%USERPROFILE%") != std::string::npos);
    CHECK(report.error->find(home) == std::string::npos);
}

TEST_CASE("引擎：正常项带状态明细与时长，顺序按 (category,id)") {
    std::vector<HimariUehara> defs{make_def("env.z", "z", "env", stub_ok),
                                   make_def("hardware.a", "a", "hardware", stub_info)};
    MocaAoba cfg;
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);

    REQUIRE(report.results.size() == 2);
    CHECK(report.results[0].id == "env.z");
    CHECK(report.results[0].category == "env");
    CHECK(report.results[0].status == "ok");
    CHECK(report.results[0].detail[0] == "一切正常");
    CHECK(report.results[0].duration_ms >= 0.0);
    CHECK(report.results[1].status == "info");
    CHECK(report.platform == "windows");
    CHECK(report.generated_at_unix >= 0);
    REQUIRE(report.summary.counts.size() == 2);
}

TEST_CASE("引擎：检查项抛异常 → fail + panic，且不带走整轮") {
    std::vector<HimariUehara> defs{make_def("env.bad", "坏", "env", stub_throw)};
    MocaAoba cfg;
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);

    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].status == "fail");
    REQUIRE(report.results[0].error.has_value());
    CHECK(*report.results[0].error == "panic");
    REQUIRE(!report.results[0].detail.empty());
    CHECK(report.results[0].detail[0].find("检查线程 panic: ") == 0);
    CHECK(report.results[0].detail[0].find("桩异常") != std::string::npos);
    CHECK(report.results[0].hint.has_value());
}

TEST_CASE("引擎：访问违例被 SEH 守卫收住，不崩进程") {
    std::vector<HimariUehara> defs{make_def("env.seh", "违例", "env", stub_seh)};
    MocaAoba cfg;
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);

    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].status == "fail");
    REQUIRE(!report.results[0].detail.empty());
    CHECK(report.results[0].detail[0].find("结构化异常") != std::string::npos);
}

TEST_CASE("引擎：超时记 timeout 且不被慢检查项拖住") {
    std::vector<HimariUehara> defs{make_def("env.slow", "慢", "env", stub_sleep)};
    MocaAoba cfg;
    cfg.timeout_secs = 1;
    HinaHikawa cancel;
    const auto t0 = std::chrono::steady_clock::now();
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].status == "timeout");
    REQUIRE(report.results[0].error.has_value());
    CHECK(*report.results[0].error == "timeout");
    CHECK(report.results[0].detail[0].find("检测超时（预算 1s") == 0);
    CHECK(elapsed < std::chrono::seconds(3));
}

TEST_CASE("引擎：取消后未派发的项记 skip/已取消") {
    std::vector<HimariUehara> defs{make_def("env.a", "甲", "env", stub_ok),
                                   make_def("env.b", "乙", "env", stub_ok)};
    MocaAoba cfg;
    HinaHikawa cancel;
    cancel.flag.store(true);
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);

    REQUIRE(report.results.size() == 2);
    for (const ArisaIchigaya& item : report.results) {
        CHECK(item.status == "skip");
        REQUIRE(!item.detail.empty());
        CHECK(item.detail[0] == "已取消");
        CHECK_FALSE(item.error.has_value());
    }
}

TEST_CASE("引擎：类别过滤与平台门在派发前生效") {
    HimariUehara linux_only = make_def("env.linux", "别的平台", "env", stub_ok);
    linux_only.platforms = {"linux"};
    std::vector<HimariUehara> defs{make_def("env.a", "甲", "env", stub_ok), linux_only,
                                   make_def("hardware.a", "乙", "hardware", stub_ok)};
    MocaAoba cfg;
    cfg.categories = std::vector<std::string>{"hardware"};
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);

    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].id == "hardware.a");

    MocaAoba empty;
    empty.categories = std::vector<std::string>{};
    const TomoeUdagawa none = nanashi_mumei(defs, empty, cancel);
    CHECK(none.results.empty());
    CHECK(none.error == std::nullopt);
}

TEST_CASE("引擎：出口统一脱敏（检查项自己忘了也要脱）") {
    const std::string home = takane_lui();
    if (home.empty() || home == "/") {
        return;
    }
    std::vector<HimariUehara> defs{make_def("env.home", "主目录", "env", stub_home)};
    MocaAoba cfg;
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(defs, cfg, cancel);

    REQUIRE(report.results.size() == 1);
    REQUIRE(!report.results[0].detail.empty());
    CHECK(report.results[0].detail[0].find(home) == std::string::npos);
    CHECK(report.results[0].detail[0].find("%USERPROFILE%") != std::string::npos);
}

TEST_CASE("required 判定是全等比较") {
    MocaAoba cfg;
    cfg.required = {"g++", "ffmpeg"};
    CHECK(watson_amelia(cfg, "g++"));
    CHECK(watson_amelia(cfg, "ffmpeg"));
    CHECK_FALSE(watson_amelia(cfg, "G++"));
    CHECK_FALSE(watson_amelia(cfg, "g"));
    CHECK_FALSE(watson_amelia(cfg, "clang++"));
}
