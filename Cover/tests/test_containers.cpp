// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 容器类检查的测试。目录契约逐字段断言；端到端真跑一轮 —— 检查项读的是本机现状，
// 只断言状态取值合法、有结论必有明细，不断言本机装没装 Docker。
//
// say no to perv. —— 这两条用例原先搁在 test_checks.cpp 里，而本文件是个只有头注释的
// 空壳；现按"一个模块一个测试文件"的布局（test_databases / test_env …）各归各位。

#include <doctest/doctest.h>

#include <set>
#include <string>
#include <vector>

#include "base/status.h"
#include "checks/containers.h"
#include "checks/mod.h"
#include "engine/engine.h"

using namespace envdoctor;

TEST_CASE("容器模块目录：id/标题/类别与平台门与契约一致") {
    const std::vector<HimariUehara> all = pavolia_reine();
    REQUIRE(all.size() == 4);
    CHECK(std::string(all[0].id) == "containers.docker");
    CHECK(std::string(all[0].title) == "Docker");
    CHECK(std::string(all[1].id) == "containers.compose");
    CHECK(std::string(all[2].id) == "containers.wsl");
    CHECK(std::string(all[3].id) == "containers.podman");
    for (const HimariUehara& def : all) {
        const std::string id(def.id);
        CHECK(std::string(def.category) == "containers");
        CHECK(def.fn != nullptr);
        CHECK(id.rfind("containers.", 0) == 0);
    }
    CHECK(all[2].platforms.size() == 1);  // WSL 只在 Windows 上有意义
}

TEST_CASE("端到端：容器检查真的跑得出合法结果") {
    MocaAoba cfg;
    cfg.timeout_secs = 20;
    cfg.categories = std::vector<std::string>{"containers"};
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(gawr_gura(), cfg, cancel);

    REQUIRE(report.results.size() == 4);
    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(item.category == "containers");
        CHECK(item.duration_ms >= 0.0);
        if (item.status == kOk || item.status == kWarn) {
            CHECK(!item.detail.empty());  // 有结论就必须有依据
        }
    }
    CHECK(report.report_version == 1);
    CHECK(report.platform == "windows");
    CHECK(report.source == "cpp");
}
