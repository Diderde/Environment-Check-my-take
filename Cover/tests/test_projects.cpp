// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 本地项目清点类检查的测试。
//
// 分两块：① 目录形状、判定与解析纯函数 —— 仓库事实、清单文本、分析输入全是构造出来的，
// 逐字段断言，不碰宿主环境；② 起一棵临时目录树真跑一遍 —— 只断言"状态取值合法、有结论
// 必有依据、报告里不出现路径/项目名/远端地址"。本机有几个仓库、装没装 git 这类结论
// **不断言**：换一台机器就会变红，而它想保护的分支由第一块覆盖。

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/process.h"
#include "base/status.h"
#include "checks/mod.h"
#include "checks/projects.h"
#include "engine/engine.h"

using namespace envdoctor;

TEST_CASE("项目模块目录：id/标题/类别与报告契约一致") {
    const std::vector<HimariUehara> all = ange_katrina();
    const std::vector<std::pair<std::string, std::string>> want{
        {"projects.inventory", "本地项目清点"},
        {"projects.health", "项目仓库健康度"},
        {"projects.deps", "跨项目依赖"},
    };
    REQUIRE(all.size() == want.size());
    std::set<std::string> ids;
    for (size_t i = 0; i < want.size(); ++i) {
        CHECK(std::string(all[i].id) == want[i].first);
        CHECK(std::string(all[i].title) == want[i].second);
        CHECK(std::string(all[i].category) == "projects");
        CHECK(all[i].platforms.empty());  // 扫描范围由调用方给，不挑平台
        CHECK(all[i].fn != nullptr);
        CHECK(ids.insert(all[i].id).second);
    }
}

TEST_CASE("没给扫描根：三项都记 skip，且都不去扫主目录") {
    MocaAoba cfg;  // scan_roots 空 = 没给 --scan-root
    const std::vector<RanMitake> got{yukishiro_mahiro(cfg), suzuhara_lulu(cfg), ex_albio(cfg)};
    for (const RanMitake& r : got) {
        CHECK(r.status == kSkip);
        REQUIRE(r.detail.size() == 1);
        CHECK(r.detail[0] == "未指定扫描根（--scan-root 可重复；默认不扫描本地项目）");
        CHECK(!r.hint.has_value());
    }

    // 只给了空白也算没给
    MocaAoba blank;
    blank.scan_roots = {"   ", ""};
    CHECK(mayuzumi_kai(blank).empty());
    CHECK(yukishiro_mahiro(blank).status == kSkip);
}

TEST_CASE("扫描根归一化：去空白、丢空项，重复给出的根保留") {
    MocaAoba cfg;
    cfg.scan_roots = {"  D:\\work  ", "", "E:\\code", "D:\\work"};
    const std::vector<std::string> roots = mayuzumi_kai(cfg);
    REQUIRE(roots.size() == 3);
    CHECK(roots[0] == "D:\\work");
    CHECK(roots[1] == "E:\\code");
    CHECK(roots[2] == "D:\\work");  // 重复的根不去重：编号按"根顺序 + 根内字典序"走
}

TEST_CASE("健康度判定：只有挡路或会丢东西的状态才 warn") {
    const auto make_repo = [](bool detached, int remotes, std::optional<int> dirty, bool shallow,
                              const std::string& operation, std::optional<double> lock_age,
                              const std::string& error) {
        sukoya_kana r;
        r.path = "C:\\tmp\\repo";
        r.detached = detached;
        r.remotes = remotes;
        r.dirty = dirty;
        r.shallow = shallow;
        r.operation = operation;
        r.lock_age = lock_age;
        r.error = error;
        return r;
    };

    // 一个仓库都没发现：不是问题，也不是"这台机器上没有项目"
    const RanMitake empty = hakase_fuyuki({}, false);
    CHECK(empty.status == kInfo);
    REQUIRE(empty.detail.size() == 1);
    CHECK(empty.detail[0] == "未在给定扫描根下发现 git 仓库（或扫描已达预算上限）");

    // 干净仓库：只记 info，不列编号
    const std::vector<sukoya_kana> clean{make_repo(false, 1, 0, false, "", std::nullopt, ""),
                                         make_repo(false, 2, 0, false, "", std::nullopt, "")};
    const RanMitake fine = hakase_fuyuki(clean, false);
    CHECK(fine.status == kInfo);
    REQUIRE(fine.detail.size() == 1);
    CHECK(fine.detail[0] == "仓库 2 个：未提交改动 0、无远端 0、浅克隆 0");
    CHECK(!fine.hint.has_value());

    // 有改动：列编号，但仍然是 info（未提交改动是正常工作状态）
    const std::vector<sukoya_kana> dirty{make_repo(false, 1, 3, false, "", std::nullopt, ""),
                                         make_repo(false, 1, 0, false, "", std::nullopt, "")};
    const RanMitake changed = hakase_fuyuki(dirty, false);
    CHECK(changed.status == kInfo);
    REQUIRE(changed.detail.size() == 2);
    CHECK(changed.detail[0] == "仓库 2 个：未提交改动 1、无远端 0、浅克隆 0");
    CHECK(changed.detail[1] == "  未提交改动: #1");

    // 浅克隆同样是正常工作状态：只记 info
    const std::vector<sukoya_kana> shallow{make_repo(false, 1, 0, true, "", std::nullopt, "")};
    CHECK(hakase_fuyuki(shallow, false).status == kInfo);
    CHECK(hakase_fuyuki(shallow, false).detail[1] == "  浅克隆: #1");

    // 未完成的 merge：warn + 建议
    const std::vector<sukoya_kana> merge{make_repo(false, 1, 0, false, "merge", std::nullopt, "")};
    const RanMitake broken = hakase_fuyuki(merge, false);
    CHECK(broken.status == kWarn);
    REQUIRE(broken.detail.size() == 2);
    CHECK(broken.detail[0] == "仓库 1 个：未提交改动 0、无远端 0、浅克隆 0");
    CHECK(broken.detail[1] == "  未完成的 git 操作: #1");
    REQUIRE(broken.hint.has_value());
    CHECK(broken.hint->find("index.lock") != std::string::npos);
    CHECK(broken.hint->find("git status") != std::string::npos);

    // 残留锁：超过 3 分钟才算"卡住"
    const std::vector<sukoya_kana> fresh_lock{make_repo(false, 1, 0, false, "", 10.0, "")};
    CHECK(hakase_fuyuki(fresh_lock, false).status == kInfo);
    const std::vector<sukoya_kana> stale_lock{make_repo(false, 1, 0, false, "", 200.0, "")};
    const RanMitake locked = hakase_fuyuki(stale_lock, false);
    CHECK(locked.status == kWarn);
    CHECK(locked.detail[1] == "  残留 index.lock: #1");

    // detached HEAD：提交会游离，warn
    const std::vector<sukoya_kana> detached{make_repo(true, 1, 0, false, "", std::nullopt, "")};
    const RanMitake free_head = hakase_fuyuki(detached, false);
    CHECK(free_head.status == kWarn);
    CHECK(free_head.detail[1] == "  detached HEAD: #1");

    // 取数失败：首行带计数，并列出编号
    const std::vector<sukoya_kana> errored{make_repo(false, 1, 0, false, "", std::nullopt, "boom")};
    const RanMitake failed = hakase_fuyuki(errored, false);
    CHECK(failed.status == kInfo);
    REQUIRE(failed.detail.size() == 2);
    CHECK(failed.detail[0] == "仓库 1 个：未提交改动 0、无远端 0、浅克隆 0、查询失败 1");
    CHECK(failed.detail[1] == "  查询失败: #1");
}

TEST_CASE("健康度判定：没取到'未提交改动'时明说未判断，不写 0") {
    sukoya_kana unknown;
    unknown.path = "C:\\tmp\\repo";
    unknown.remotes = 1;
    // dirty 留空、且没有失败说明 = 真正的"没取到"
    const RanMitake got = hakase_fuyuki({unknown}, false);
    CHECK(got.status == kInfo);
    REQUIRE(got.detail.size() == 1);
    CHECK(got.detail[0].find("未提交改动 未判断") != std::string::npos);
    CHECK(got.detail[0].find("未提交改动 0") == std::string::npos);
    // 没取到的类别不许列出编号
    CHECK(got.detail[0].find("  未提交改动:") == std::string::npos);
}

TEST_CASE("健康度判定：查询失败的仓库记进'查询失败'，不混进'未提交改动'") {
    const auto make_failed = [](const char* error) {
        sukoya_kana r;
        r.path = "C:\\tmp\\repo";
        r.remotes = 1;
        r.error = error;  // 查询失败 → 没有条数，但有人解释为什么
        return r;
    };

    // 旧实现口径：失败的仓库不进"未提交改动"的计数，只进"查询失败"
    const RanMitake all_failed = hakase_fuyuki({make_failed("boom")}, false);
    CHECK(all_failed.status == kInfo);
    REQUIRE(all_failed.detail.size() == 2);
    CHECK(all_failed.detail[0] == "仓库 1 个：未提交改动 0、无远端 0、浅克隆 0、查询失败 1");
    CHECK(all_failed.detail[0].find("未判断") == std::string::npos);
    CHECK(all_failed.detail[1] == "  查询失败: #1");

    // 一半查得出一半失败：数字只算查出来的那一半
    sukoya_kana counted;
    counted.path = "C:\\tmp\\repo2";
    counted.remotes = 1;
    counted.dirty = 2;
    const RanMitake mixed = hakase_fuyuki({counted, make_failed("boom")}, false);
    CHECK(mixed.detail[0] == "仓库 2 个：未提交改动 1、无远端 0、浅克隆 0、查询失败 1");
    CHECK(mixed.detail[1] == "  未提交改动: #1");
    CHECK(mixed.detail[2] == "  查询失败: #2");
}

TEST_CASE("健康度判定：编号样本最多 5 个，明细最多 7 行") {
    const auto many_no_remote = [](size_t n) {
        std::vector<sukoya_kana> repos;
        for (size_t i = 0; i < n; ++i) {
            sukoya_kana r;
            r.path = "C:\\tmp\\repo" + std::to_string(i);
            r.remotes = 0;  // 无远端是正常工作状态，用它堆编号
            repos.push_back(r);
        }
        return repos;
    };
    const RanMitake sampled = hakase_fuyuki(many_no_remote(7), false);
    REQUIRE(sampled.detail.size() == 2);
    CHECK(sampled.detail[1] == "  无远端: #1 #2 #3 #4 #5 …（共 7）");

    // 每一类各占一行 + 截断说明 → 超过 7 行时按上限截断
    std::vector<sukoya_kana> noisy;
    for (size_t i = 0; i < 3; ++i) {
        sukoya_kana r;
        r.path = "C:\\tmp\\repo" + std::to_string(i);
        r.remotes = 0;
        r.shallow = true;
        r.operation = "rebase";
        r.lock_age = 400.0;
        r.detached = true;
        r.error = "boom";
        noisy.push_back(r);
    }
    const RanMitake sliced = hakase_fuyuki(noisy, true);
    CHECK(sliced.status == kWarn);
    CHECK(sliced.detail.size() == 7);
}

TEST_CASE("requirements.txt：只收声明的依赖，URL / 选项行 / 环境标记都不当依赖") {
    bool bad = false;
    const std::string text =
        "# 注释\n"
        "\n"
        "-r other.txt\n"
        "requests==2.31.0   # 钉死\n"
        "flask>=2.0\n"
        "uvicorn[standard]==0.30.0\n"
        "pkg; python_version < \"3.11\"\n"
        "https://example.com/x.whl\n"
        "git+https://github.com/a/b.git\n";
    const std::vector<ManifestDep> deps = yorumi_rena("requirements.txt", text, &bad);
    CHECK(!bad);
    REQUIRE(deps.size() == 4);
    CHECK(std::get<0>(deps[0]) == "requests");
    CHECK(std::get<1>(deps[0]) == "==2.31.0");
    CHECK(std::get<2>(deps[0]) == "2.31.0");
    CHECK(std::get<0>(deps[1]) == "flask");
    CHECK(std::get<2>(deps[1]).empty());  // 范围约束不是精确钉死
    CHECK(std::get<0>(deps[2]) == "uvicorn");
    CHECK(std::get<1>(deps[2]) == "==0.30.0");  // extras 被剥掉，剩下的才是版本约束
    CHECK(std::get<2>(deps[2]) == "0.30.0");
    CHECK(std::get<0>(deps[3]) == "pkg");
    CHECK(std::get<2>(deps[3]).empty());
}

TEST_CASE("package.json：块顺序、精确版本、非字符串值与 http 前缀") {
    bool bad = false;
    const std::string text = R"({
  "name": "demo",
  "version": "1.0.0",
  "devDependencies": {
    "typescript": "5.4.5",
    "http-proxy": "1.18.1"
  },
  "dependencies": {
    "@types/node": "^20.11.0",
    "left-pad": "1.3.0",
    "bundle": {"nested": true},
    "count": 3
  },
  "scripts": {"build": "tsc"}
})";
    const std::vector<ManifestDep> deps = yorumi_rena("package.json", text, &bad);
    CHECK(!bad);
    // 先 dependencies 再 devDependencies（旧实现按这个键顺序取块，与文件里的先后无关）
    REQUIRE(deps.size() == 5);
    CHECK(std::get<0>(deps[0]) == "@types/node");
    CHECK(std::get<2>(deps[0]).empty());  // ^20.11.0 不是精确钉死
    CHECK(std::get<0>(deps[1]) == "left-pad");
    CHECK(std::get<2>(deps[1]) == "1.3.0");
    CHECK(std::get<0>(deps[2]) == "bundle");  // 非字符串值只给名字
    CHECK(std::get<1>(deps[2]).empty());
    CHECK(std::get<0>(deps[3]) == "count");
    CHECK(std::get<0>(deps[4]) == "typescript");
    CHECK(std::get<2>(deps[4]) == "5.4.5");
    for (const ManifestDep& d : deps) {
        CHECK(std::get<0>(d) != "http-proxy");  // http 前缀的包名照旧实现跳过
    }

    // 坏掉的 JSON：一份都不报，并标明"这份读不下来"
    bool broken = false;
    CHECK(yorumi_rena("package.json", R"({"dependencies": )", &broken).empty());
    CHECK(broken);

    // 合法但没有依赖块：空结果，不算读不下来
    bool none_bad = false;
    CHECK(yorumi_rena("package.json", R"({"name": "x"})", &none_bad).empty());
    CHECK(!none_bad);
}

TEST_CASE("go.mod：只在 require 块与 require 行里取，间接依赖照样算") {
    bool bad = false;
    const std::string text =
        "module example.com/demo\n"
        "\n"
        "go 1.21\n"
        "\n"
        "require (\n"
        "\tgithub.com/pkg/errors v0.9.1\n"
        "\tgolang.org/x/text v0.14.0 // indirect\n"
        ")\n"
        "\n"
        "require github.com/google/uuid v1.6.0\n"
        "\n"
        "replace example.com/old => example.com/new v1.0.0\n";
    const std::vector<ManifestDep> deps = yorumi_rena("go.mod", text, &bad);
    CHECK(!bad);
    REQUIRE(deps.size() == 3);
    CHECK(std::get<0>(deps[0]) == "github.com/pkg/errors");
    CHECK(std::get<2>(deps[0]) == "v0.9.1");
    CHECK(std::get<0>(deps[1]) == "golang.org/x/text");
    CHECK(std::get<2>(deps[1]) == "v0.14.0");
    CHECK(std::get<0>(deps[2]) == "github.com/google/uuid");
    CHECK(std::get<2>(deps[2]) == "v1.6.0");
}

TEST_CASE("pyproject.toml：project.dependencies 与 optional-dependencies") {
    bool bad = false;
    const std::string text =
        "[build-system]\n"
        "requires = [\"setuptools\"]\n"  // 不是 [project]：不取
        "\n"
        "[project]\n"
        "name = \"demo\"\n"
        "dependencies = [\n"
        "  \"requests==2.31.0\",\n"
        "  \"uvicorn[standard]==0.30.0\",   # extras\n"
        "  \"rich>=13\",\n"
        "]\n"
        "\n"
        "[project.optional-dependencies]\n"
        "test = [\"pytest==8.1.1\"]\n"
        "docs = ['sphinx==7.2.6']\n";
    const std::vector<ManifestDep> deps = yorumi_rena("pyproject.toml", text, &bad);
    CHECK(!bad);
    REQUIRE(deps.size() == 5);
    CHECK(std::get<0>(deps[0]) == "requests");
    CHECK(std::get<2>(deps[0]) == "2.31.0");
    CHECK(std::get<0>(deps[1]) == "uvicorn");
    CHECK(std::get<2>(deps[1]) == "0.30.0");
    CHECK(std::get<0>(deps[2]) == "rich");
    CHECK(std::get<2>(deps[2]).empty());
    CHECK(std::get<0>(deps[3]) == "pytest");
    CHECK(std::get<2>(deps[3]) == "8.1.1");
    CHECK(std::get<0>(deps[4]) == "sphinx");
    CHECK(std::get<2>(deps[4]) == "7.2.6");
}

TEST_CASE("Cargo.toml：三个依赖表与子表都算包名，裸版本号不判精确钉死") {
    bool bad = false;
    const std::string text =
        "[package]\n"
        "name = \"demo\"\n"
        "version = \"0.1.0\"\n"  // [package] 不是依赖表
        "\n"
        "[dependencies]\n"
        "serde = { version = \"1.0.200\", features = [\"derive\"] }\n"
        "regex = \"1.10.4\"\n"
        "\n"
        "[dev-dependencies]\n"
        "tempfile = \"3.10.1\"\n"
        "\n"
        "[build-dependencies]\n"
        "cc = \"1.0.90\"\n"
        "\n"
        "[dependencies.tokio]\n"
        "version = \"1.37.0\"\n"
        "\n"
        "[target.'cfg(unix)'.dependencies]\n"
        "nix = \"0.28.0\"\n";  // 目标专属表：旧实现读顶层，不取
    const std::vector<ManifestDep> deps = yorumi_rena("Cargo.toml", text, &bad);
    CHECK(!bad);
    REQUIRE(deps.size() == 5);
    CHECK(std::get<0>(deps[0]) == "serde");
    CHECK(std::get<1>(deps[0]).empty());  // 内联表在旧实现里不是字符串 → 只算包名
    CHECK(std::get<0>(deps[1]) == "regex");
    CHECK(std::get<1>(deps[1]) == "1.10.4");
    CHECK(std::get<0>(deps[2]) == "tempfile");
    CHECK(std::get<0>(deps[3]) == "cc");
    CHECK(std::get<0>(deps[4]) == "tokio");  // 子表的键名就是包名
    for (const ManifestDep& d : deps) {
        CHECK(std::get<2>(d).empty());  // Cargo 的裸版本号是 caret 语义，不算精确钉死
        // 内联表里的键（version / features）不是依赖
        CHECK(std::get<0>(d) != "version");
        CHECK(std::get<0>(d) != "features");
    }

    // 数组跨行是合法 TOML：照样读得出来，且数组值只算包名（旧实现同样只取名字）
    bool array_bad = false;
    const std::string array_text =
        "[dependencies]\n"
        "serde = \"1.0\"\n"
        "weird = [\n"
        "  \"a\",\n"
        "  \"b\",\n"
        "]\n";
    const std::vector<ManifestDep> array_deps = yorumi_rena("Cargo.toml", array_text, &array_bad);
    CHECK(!array_bad);
    REQUIRE(array_deps.size() == 2);
    CHECK(std::get<0>(array_deps[0]) == "serde");
    CHECK(std::get<0>(array_deps[1]) == "weird");
    CHECK(std::get<1>(array_deps[1]).empty());
}

TEST_CASE("TOML 里读不下来的形状：标出来，不硬猜") {
    bool bad = false;
    const std::string text =
        "[project]\n"
        "name = \"demo\"\n"
        "dependencies = \"requests\"\n";  // 不是数组：读不下来
    CHECK(yorumi_rena("pyproject.toml", text, &bad).empty());
    CHECK(bad);

    // 内联表跨行不是合法 TOML（旧实现整份解析失败）：不猜，且续行里的键不算依赖
    bool inline_bad = false;
    const std::string inline_text =
        "[dependencies]\n"
        "rand = { version = \"0.8.5\",\n"
        "         features = [\"small_rng\"] }\n";
    const std::vector<ManifestDep> inline_deps =
        yorumi_rena("Cargo.toml", inline_text, &inline_bad);
    CHECK(inline_bad);
    for (const ManifestDep& d : inline_deps) {
        CHECK(std::get<0>(d) != "features");
        CHECK(std::get<0>(d) != "version");
    }

    // 与依赖无关的多行字符串不该被算成"读不下来"
    bool fine_bad = false;
    const std::string other =
        "[project]\n"
        "description = \"\"\"\n"
        "多行\n"
        "说明\n"
        "\"\"\"\n"
        "dependencies = [\"requests==2.31.0\"]\n";
    CHECK(yorumi_rena("pyproject.toml", other, &fine_bad).size() == 1);
    CHECK(!fine_bad);
}

TEST_CASE("跨项目分析：只对精确钉死的版本判互斥") {
    const std::vector<ProjectDep> entries{
        {1, "requests", "2.31.0"}, {2, "requests", "2.32.0"}, {1, "flask", ""},
        {2, "flask", ""},          {3, "rich", "13.7.0"},
    };
    const RanMitake warn = kagami_hayato(entries, 3, 2, 0, 0);
    CHECK(warn.status == kWarn);
    REQUIRE(warn.detail.size() == 4);
    CHECK(warn.detail[0] == "已解析清单 2 份 / 依赖条目 5 条");
    CHECK(warn.detail[1] == "跨项目共用最多: flask（2 个项目）、requests（2 个项目）");
    CHECK(warn.detail[2] == "精确版本互斥 1 项（同一时间只可能装一个版本）:");
    CHECK(warn.detail[3] == "  requests: #1=2.31.0、#2=2.32.0");
    REQUIRE(warn.hint.has_value());
    CHECK(warn.hint->find("venv") != std::string::npos);

    // 钉的是同一个版本：不算互斥
    const RanMitake same =
        kagami_hayato({{1, "requests", "2.31.0"}, {2, "requests", "2.31.0"}}, 2, 2, 0, 0);
    CHECK(same.status == kInfo);
    CHECK(same.detail.back() == "未发现精确版本互斥（精确钉版本 2 条）");
    CHECK(!same.hint.has_value());

    // 一份清单都没解析出依赖
    const RanMitake none = kagami_hayato({}, 3, 0, 0, 0);
    CHECK(none.status == kInfo);
    REQUIRE(none.detail.size() == 1);
    CHECK(none.detail[0] ==
          "仓库 3 个，均无受支持的根清单文件"
          "（requirements.txt / pyproject.toml / package.json / go.mod / Cargo.toml）");

    // 读不到 / 读不下来时补说明：不许把"没读到"说成"没有清单文件"
    const RanMitake degraded = kagami_hayato({}, 1, 0, 2, 1);
    REQUIRE(degraded.detail.size() == 2);
    CHECK(degraded.detail[1] == "另有 2 份读取失败、1 份未能解析");

    // 有依赖时说明挂在首行括号里；只有读取失败时与旧文案逐字一致
    const RanMitake noted = kagami_hayato({{1, "requests", "2.31.0"}}, 1, 1, 0, 1);
    CHECK(noted.detail[0] == "已解析清单 1 份 / 依赖条目 1 条（1 份未能解析）");
    const RanMitake unread = kagami_hayato({{1, "requests", "2.31.0"}}, 1, 1, 2, 0);
    CHECK(unread.detail[0] == "已解析清单 1 份 / 依赖条目 1 条（2 份读取失败）");
}

TEST_CASE("远端只数条数：带凭据的地址一个字都不进报告") {
    const std::string config_text =
        "[core]\n"
        "  repositoryformatversion = 0\n"
        "[remote \"origin\"]\n"
        "  url = https://user:ghtoken@example.com/a/b.git\n"
        "[remote \"mirror\"]\n"
        "  url = git@10.0.0.7:internal/x.git\n"
        "[credential]\n"
        "  helper = store\n";
    CHECK(aiba_uiha(config_text) == 2);
    CHECK(aiba_uiha("") == 0);
    CHECK(aiba_uiha("[remote]\n") == 0);          // 没有引号：不是远端节
    CHECK(aiba_uiha("  [remote \"x\"]\n") == 0);  // 缩进的行首不算（旧正则要求行首）
}

TEST_CASE("worktree / 子模块：`.git` 是文件时按 gitdir 指过去") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_gitdir";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "wt", ec);

    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    const std::string wt = robocosan((root / "wt").wstring());
    const std::string real = robocosan((root / "real").wstring());

    write(root / "wt" / ".git", "gitdir: " + real + "\n");
    CHECK(amamiya_kokoro(wt) == real);

    write(root / "wt" / ".git", "gitdir: ../real\n");
    CHECK(amamiya_kokoro(wt) == kazama_iroha(wt, "../real"));

    // 普通仓库（`.git` 是目录）与"什么都没有"的仓库：都按 <repo>\.git 处理
    const std::string plain = robocosan((root / "plain").wstring());
    CHECK(amamiya_kokoro(plain) == kazama_iroha(plain, ".git"));

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("单仓库取数：git status 数未提交改动，重命名只算一条") {
    // 这一段要真跑 git 才会说话。没有 git 时只断言"取不到就不给数字"，
    // 不断言"这台机器上应该有几个改动"。
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_facts";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "broken" / ".git", ec);
    std::filesystem::create_directories(root / "repo" / ".git" / "objects", ec);
    std::filesystem::create_directories(root / "repo" / ".git" / "refs" / "heads", ec);
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    write(root / "repo" / ".git" / "HEAD", "ref: refs/heads/main\n");
    write(root / "repo" / "a.txt", "hello\n");

    // ① `.git` 建得不对：git 必定失败 → 没有条数，但必须有失败说明（装没装 git 都一样）
    const sukoya_kana broken =
        hayama_marin(robocosan((root / "broken").wstring()), std::chrono::seconds(6));
    CHECK(!broken.dirty.has_value());
    CHECK(!broken.error.empty());

    // ② 手搓出来的合法仓库（HEAD + objects + refs）+ 一个未跟踪文件：1 条改动
    const std::string repo = robocosan((root / "repo").wstring());
    const sukoya_kana facts = hayama_marin(repo, std::chrono::seconds(6));
    if (facts.error.empty()) {
        CHECK(facts.branch == "main");
        CHECK(!facts.detached);
        REQUIRE(facts.dirty.has_value());
        CHECK(*facts.dirty == 1);
    } else {
        CHECK(!facts.dirty.has_value());  // 取不到就不给数字
    }

    // ③ 重命名：`-z` 里新路径与原路径是两个字段，只该算一条改动
    if (facts.error.empty()) {
        const auto rename_root = root / "rename";
        std::filesystem::create_directories(rename_root, ec);
        const std::string rp = robocosan(rename_root.wstring());
        const auto run_git = [&rp](const std::vector<std::string>& args) {
            return nekomata_okayu("git", args, std::chrono::seconds(30), rp);
        };
        // 固定身份并关掉签名：不让宿主的 git 配置决定这个夹具能不能跑成
        REQUIRE(run_git({"init", "-q"}).success);
        write(rename_root / "old.txt", "hello\n");
        REQUIRE(run_git({"add", "old.txt"}).success);
        REQUIRE(run_git({"-c", "user.name=envdoctor", "-c", "user.email=envdoctor@example.invalid",
                         "-c", "commit.gpgsign=false", "commit", "-q", "-m", "init"})
                    .success);
        REQUIRE(run_git({"mv", "old.txt", "new.txt"}).success);

        const sukoya_kana renamed = hayama_marin(rp, std::chrono::seconds(6));
        REQUIRE(renamed.error.empty());
        REQUIRE(renamed.dirty.has_value());
        CHECK(*renamed.dirty == 1);  // 一次重命名 = 一条改动，不是两条
    }

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("三项并发取快照：谁先谁扫，另两项拿到的必须是同一份数据") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_race";
    std::filesystem::remove_all(root, ec);
    // 先把目录铺大一点：扫描要花上几毫秒，三项才真的会同时站在快照口上
    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 20; ++j) {
            std::filesystem::create_directories(
                root / ("bulk" + std::to_string(i)) / ("sub" + std::to_string(j)), ec);
        }
    }
    std::filesystem::create_directories(root / "alpha" / ".git", ec);
    std::filesystem::create_directories(root / "beta" / ".git", ec);
    std::filesystem::create_directories(root / "gamma" / ".git", ec);
    const std::string root_utf8 = robocosan(root.wstring());

    MocaAoba cfg;
    cfg.scan_roots = {root_utf8};
    ars_almal();

    // 三项各占一线程（引擎就是这么跑的），合并结果必须一致：都看到 3 个仓库
    RanMitake got[3];
    std::vector<std::thread> workers;
    workers.emplace_back([&] { got[0] = yukishiro_mahiro(cfg); });
    workers.emplace_back([&] { got[1] = suzuhara_lulu(cfg); });
    workers.emplace_back([&] { got[2] = ex_albio(cfg); });
    for (std::thread& t : workers) {
        t.join();
    }

    CHECK(got[0].detail[0] == "扫描根 1 个 / 发现仓库 3 个");
    CHECK(got[1].detail[0].rfind("仓库 3 个", 0) == 0);
    CHECK(got[2].detail[0] == "仓库 3 个，均无受支持的根清单文件"
                             "（requirements.txt / pyproject.toml / package.json / go.mod / "
                             "Cargo.toml）");

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("一轮内共享快照，取完即弃：下一轮重新扫描") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_round";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "alpha" / ".git", ec);
    const std::string root_utf8 = robocosan(root.wstring());

    MocaAoba cfg;
    cfg.scan_roots = {root_utf8};
    ars_almal();  // 从干净状态起跑

    // 第一项扫描并建立本轮快照
    const RanMitake first = yukishiro_mahiro(cfg);
    CHECK(first.status == kInfo);
    REQUIRE(first.detail.size() == 3);
    CHECK(first.detail[0] == "扫描根 1 个 / 发现仓库 1 个");

    // 磁盘上多一个仓库，但第二项复用本轮快照：本轮不该重新扫描
    std::filesystem::create_directories(root / "beta" / ".git", ec);
    const RanMitake second = suzuhara_lulu(cfg);
    CHECK(second.status == kInfo);
    CHECK(second.detail[0].rfind("仓库 1 个", 0) == 0);

    // 第三项取走最后一份：快照立刻作废
    CHECK(ex_albio(cfg).status == kInfo);

    // 下一轮：重新扫描，看到刚加的那个仓库
    const RanMitake next = yukishiro_mahiro(cfg);
    CHECK(next.detail[0] == "扫描根 1 个 / 发现仓库 2 个");

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("显式作废：ars_almal 之后必定重新扫描") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_reset";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "alpha" / ".git", ec);
    const std::string root_utf8 = robocosan(root.wstring());

    MocaAoba cfg;
    cfg.scan_roots = {root_utf8};
    ars_almal();
    CHECK(yukishiro_mahiro(cfg).detail[0] == "扫描根 1 个 / 发现仓库 1 个");

    std::filesystem::create_directories(root / "beta" / ".git", ec);
    CHECK(yukishiro_mahiro(cfg).detail[0] == "扫描根 1 个 / 发现仓库 1 个");  // 本轮内仍是旧数据
    ars_almal();
    CHECK(yukishiro_mahiro(cfg).detail[0] == "扫描根 1 个 / 发现仓库 2 个");

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("换了扫描根就重新扫描：快照按扫描根串起来") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_roots";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "one" / ".git", ec);
    std::filesystem::create_directories(root / "two" / ".git", ec);
    std::filesystem::create_directories(root / "two" / "nested" / ".git", ec);
    const std::string root_utf8 = robocosan(root.wstring());

    MocaAoba cfg;
    cfg.scan_roots = {kazama_iroha(root_utf8, "one")};
    ars_almal();
    CHECK(yukishiro_mahiro(cfg).detail[0] == "扫描根 1 个 / 发现仓库 1 个");

    cfg.scan_roots = {kazama_iroha(root_utf8, "two")};  // 换了根：不许复用上一个根的扫描
    CHECK(yukishiro_mahiro(cfg).detail[0] == "扫描根 1 个 / 发现仓库 2 个");

    // 深度上限内的嵌套仓库也算（根 two 底下两层各有一个）
    ars_almal();
    cfg.scan_roots = {kazama_iroha(root_utf8, "two")};
    CHECK(yukishiro_mahiro(cfg).detail[0] == "扫描根 1 个 / 发现仓库 2 个");

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("端到端：两个项目钉了不同版本 → warn，编号与隐私都对得上") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_deps";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "alpha" / ".git", ec);
    std::filesystem::create_directories(root / "beta" / ".git", ec);
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    write(root / "alpha" / "requirements.txt", "requests==2.31.0\n");
    write(root / "beta" / "requirements.txt", "requests==2.32.0\n");
    write(root / "alpha" / ".git" / "config",
          "[remote \"origin\"]\n  url = https://user:sekrit@example.com/a.git\n");

    MocaAoba cfg;
    cfg.scan_roots = {robocosan(root.wstring())};
    ars_almal();

    const RanMitake deps = ex_albio(cfg);
    CHECK(deps.status == kWarn);
    REQUIRE(deps.detail.size() == 4);
    CHECK(deps.detail[0] == "已解析清单 2 份 / 依赖条目 2 条");
    CHECK(deps.detail[1] == "跨项目共用最多: requests（2 个项目）");
    CHECK(deps.detail[2] == "精确版本互斥 1 项（同一时间只可能装一个版本）:");
    CHECK(deps.detail[3] == "  requests: #1=2.31.0、#2=2.32.0");

    // 隐私：编号之外的什么都不许出现（路径、项目名、远端地址与凭据）
    const auto check_private = [](const std::vector<std::string>& lines) {
        for (const std::string& line : lines) {
            CHECK(line.find('\\') == std::string::npos);
            CHECK(line.find("alpha") == std::string::npos);
            CHECK(line.find("beta") == std::string::npos);
            CHECK(line.find("example.com") == std::string::npos);
            CHECK(line.find("sekrit") == std::string::npos);
        }
    };
    check_private(deps.detail);
    if (deps.hint.has_value()) {
        check_private({*deps.hint});
    }

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("端到端：未完成的 merge 与残留锁让健康度记 warn，只给编号") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_health";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "alpha" / ".git", ec);
    std::filesystem::create_directories(root / "beta" / ".git", ec);
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    write(root / "alpha" / ".git" / "MERGE_HEAD", "0123456789012345678901234567890123456789\n");
    write(root / "beta" / ".git" / "HEAD", "ref: refs/heads/main\n");
    // 残留锁：把 mtime 拨到 10 分钟前（阈值是 3 分钟）
    write(root / "beta" / ".git" / "index.lock", "");
    std::filesystem::last_write_time(root / "beta" / ".git" / "index.lock",
                                     std::filesystem::file_time_type::clock::now() -
                                         std::chrono::minutes(10),
                                     ec);

    MocaAoba cfg;
    cfg.scan_roots = {robocosan(root.wstring())};
    ars_almal();

    const RanMitake health = suzuhara_lulu(cfg);
    CHECK(health.status == kWarn);
    REQUIRE(health.detail.size() >= 3);
    CHECK(health.detail[0].rfind("仓库 2 个", 0) == 0);
    CHECK(health.detail[1] == "  未完成的 git 操作: #1");
    CHECK(health.detail[2] == "  残留 index.lock: #2");
    // 两个 `.git` 都是手搓的：`git status` 必定失败 → 记"查询失败"，且不许把没查到的
    // 改动数写成 0 之外的任何数字（旧实现同样只把它们记进"查询失败"）
    CHECK(health.detail[0].find("查询失败 2") != std::string::npos);
    CHECK(health.detail.back() == "  查询失败: #1 #2");
    REQUIRE(health.hint.has_value());
    CHECK(health.hint->find("index.lock") != std::string::npos);
    for (const std::string& line : health.detail) {
        CHECK(line.find('\\') == std::string::npos);
        CHECK(line.find("alpha") == std::string::npos);
        CHECK(line.find("beta") == std::string::npos);
    }

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("端到端：worktree 形态（`.git` 是文件）也算一个仓库") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_wt";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "wt", ec);
    std::filesystem::create_directories(root / "real", ec);
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    write(root / "wt" / ".git", "gitdir: " + robocosan((root / "real").wstring()) + "\n");
    write(root / "real" / "HEAD", "ref: refs/heads/main\n");

    MocaAoba cfg;
    cfg.scan_roots = {robocosan(root.wstring())};
    ars_almal();
    const RanMitake inventory = yukishiro_mahiro(cfg);
    CHECK(inventory.detail[0] == "扫描根 1 个 / 发现仓库 1 个");
    CHECK(inventory.detail[1] == "有远端 0 个 / 无远端 1 个");

    ars_almal();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("端到端：跑一轮 projects 三项，状态合法且报告里不出现路径") {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "envdoctor_projects_engine";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "alpha" / ".git", ec);
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    write(root / "alpha" / "package.json", "{\"dependencies\": {\"left-pad\": \"1.3.0\"}}\n");

    MocaAoba cfg;
    cfg.timeout_secs = 20;
    cfg.categories = std::vector<std::string>{"projects"};
    cfg.scan_roots = {robocosan(root.wstring())};
    ars_almal();

    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(gawr_gura(), cfg, cancel);
    REQUIRE(report.results.size() == 3);

    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    std::set<std::string> ids;
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(item.category == "projects");
        CHECK(ids.insert(item.id).second);
        CHECK(!item.detail.empty());  // 有结论就必须有依据
        for (const std::string& line : item.detail) {
            CHECK(line.find('\n') == std::string::npos);
            CHECK(line.size() <= 200);
            CHECK(line.find('\\') == std::string::npos);
            CHECK(line.find("alpha") == std::string::npos);
        }
    }
    CHECK(ids.count("projects.inventory") == 1);
    CHECK(ids.count("projects.health") == 1);
    CHECK(ids.count("projects.deps") == 1);

    ars_almal();
    std::filesystem::remove_all(root, ec);
}
