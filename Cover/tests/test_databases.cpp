// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 数据库类检查的测试。
//
// 分两块：① 目录形状与判定纯函数 —— 逐字段断言，不碰宿主环境；② 真跑一轮 —— 只断言
// "状态取值合法、有结论必有依据"。本机装没装 MySQL、哪几个端口被占用这类结论**不断言**：
// 那种断言只会在换一台机器时变红，而它想保护的分支（占用 / 没人听 / 探测没做成）由第一块覆盖。

#include <doctest/doctest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "base/status.h"
#include "checks/databases.h"
#include "checks/mod.h"
#include "engine/engine.h"

// 测试自己也要起一个监听套接字来当"有人在监听"的靶子，理由同模块文件：链接要求留在
// 唯一用到它的翻译单元里。
#pragma comment(lib, "ws2_32.lib")

using namespace envdoctor;

TEST_CASE("数据库模块目录：id/标题/类别与报告契约一致") {
    const std::vector<HimariUehara> all = yumeoi_kakeru();
    REQUIRE(all.size() == 1);
    CHECK(std::string(all[0].id) == "databases.ports");
    CHECK(std::string(all[0].title) == "本机数据库服务");
    CHECK(std::string(all[0].category) == "databases");
    CHECK(all[0].platforms.empty());  // 端口探测不挑平台
    CHECK(all[0].fn != nullptr);
}

TEST_CASE("端口判定：占用才报一行，一个都没占用只报未检测到") {
    using Probe = std::tuple<unsigned, std::string, std::optional<bool>>;

    const std::vector<Probe> quiet{{3306, "MySQL", false}, {5432, "PostgreSQL", false}};
    const RanMitake none = aizono_manami(quiet);
    CHECK(none.status == kInfo);
    REQUIRE(none.detail.size() == 1);
    CHECK(none.detail[0] == "未检测到本机监听的常见数据库端口");
    CHECK(!none.hint.has_value());

    const std::vector<Probe> one{{3306, "MySQL", true}, {5432, "PostgreSQL", false}};
    const RanMitake hit = aizono_manami(one);
    CHECK(hit.status == kInfo);
    REQUIRE(hit.detail.size() == 1);
    CHECK(hit.detail[0] == "端口 3306：占用（可能是 MySQL）");

    // 多个占用：一行一个，顺序就是探测顺序
    const std::vector<Probe> many{{6379, "Redis", true}, {27017, "MongoDB", true}};
    const RanMitake lines = aizono_manami(many);
    CHECK(lines.status == kInfo);
    REQUIRE(lines.detail.size() == 2);
    CHECK(lines.detail[0] == "端口 6379：占用（可能是 Redis）");
    CHECK(lines.detail[1] == "端口 27017：占用（可能是 MongoDB）");

    for (const std::string& line : lines.detail) {
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.size() <= 200);
    }
}

TEST_CASE("探测没做成 ≠ 没装数据库：显式不判断，不写'未检测到'") {
    using Probe = std::tuple<unsigned, std::string, std::optional<bool>>;

    const std::vector<Probe> broken{{3306, "MySQL", std::nullopt}, {5432, "PostgreSQL", false}};
    const RanMitake skip = aizono_manami(broken);
    CHECK(skip.status == kSkip);
    REQUIRE(skip.detail.size() == 1);
    CHECK(skip.detail[0].find("3306") != std::string::npos);
    CHECK(skip.detail[0].find("本次不判断") != std::string::npos);
    // 关键：不能顺势给出一句"未检测到……" —— 那是把没测成当成机器上没有
    CHECK(skip.detail[0].find("未检测到") == std::string::npos);

    // 已经探到的占用仍要报出来（有结论的部分照报，只是不替没测成的部分下结论）
    const std::vector<Probe> mixed{{3306, "MySQL", true}, {5432, "PostgreSQL", std::nullopt}};
    const RanMitake partial = aizono_manami(mixed);
    CHECK(partial.status == kInfo);  // 探到的占用照样报，只是不替没测成的端口下结论
    REQUIRE(partial.detail.size() == 2);
    CHECK(partial.detail[0] == "端口 3306：占用（可能是 MySQL）");
    CHECK(partial.detail[1].find("5432") != std::string::npos);
    CHECK(partial.detail[1].find("未检测到") == std::string::npos);
}

TEST_CASE("端口探测：连得上的报占用，没人听的报未占用") {
    // 自己起一个监听套接字再让探测去打它 —— 这样断言不依赖宿主机上装了什么。
    WSADATA data{};
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(listener != INVALID_SOCKET);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;  // 让系统挑一个空闲端口
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(listener, 1) == 0);
    int addr_len = static_cast<int>(sizeof(addr));
    REQUIRE(getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0);
    const unsigned port = ntohs(addr.sin_port);

    const std::optional<bool> hit = saegusa_akina(port);
    REQUIRE(hit.has_value());
    CHECK(*hit);

    // 关掉之后同一个端口就不该再报占用：反过来证明"占用"是真探出来的
    closesocket(listener);
    const std::optional<bool> gone = saegusa_akina(port);
    CHECK(!gone.value_or(false));
    WSACleanup();
}

TEST_CASE("端到端：数据库检查真的跑得出合法结果") {
    MocaAoba cfg;
    cfg.timeout_secs = 20;
    cfg.categories = std::vector<std::string>{"databases"};
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(gawr_gura(), cfg, cancel);

    REQUIRE(report.results.size() == 1);
    const ArisaIchigaya& item = report.results[0];
    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    CHECK(allowed.count(item.status) == 1);
    CHECK(item.id == "databases.ports");
    CHECK(item.category == "databases");
    // 这一项要么给出端口结论（info），要么明说没测成（skip）—— 不会变成 ok/warn/fail
    CHECK((item.status == kInfo || item.status == kSkip));
    CHECK(!item.detail.empty());
    for (const std::string& line : item.detail) {
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.size() <= 200);
    }
}
