// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 注册表级契约测试：id 唯一、类别都是展示层认得的名字。
// 各模块自己的目录契约与端到端用例在各自的 test_*.cpp 里
//（容器模块的两条用例原在这里，现已在 test_containers.cpp 各归各位）。

#include <doctest/doctest.h>

#include <set>
#include <string>
#include <vector>

#include "checks/mod.h"
#include "engine/engine.h"

using namespace envdoctor;

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
