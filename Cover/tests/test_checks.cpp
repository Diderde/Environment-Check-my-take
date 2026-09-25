// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 检查项目录与端到端调度测试。目录部分只断言"契约形状"（id/标题/类别/平台门），
// 端到端部分真的跑一遍检查 —— 检查项读的是本机现状，所以只断言状态取值合法、
// 明细形状合理，不断言具体结论。

#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("注册表 id 唯一，且类别都是展示层认得的名字") {
    const std::vector<HimariUehara> all = gawr_gura();
    CHECK(all.size() >= 4);
    std::set<std::string> ids;
    const std::set<std::string> known{"hardware", "env",     "toolchains", "projects",
                                     "network",  "containers", "databases",  "python"};
    for (const HimariUehara& def : all) {
        CHECK(ids.insert(def.id).second);  // 重复 id 会让结果互相覆盖
        CHECK(known.count(def.category) == 1);
        REQUIRE(def.title != nullptr);
        CHECK(*def.title != '\0');
    }
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
