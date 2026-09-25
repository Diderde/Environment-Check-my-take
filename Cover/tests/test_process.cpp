// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 外部命令执行层测试。断言取向与上一版的 Rust 探针测试对齐：
// PATHEXT 解析、未安装走 not_found 通路、超时后进程树确实被回收。

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <vector>

#include "base/process.h"

using namespace envdoctor;

TEST_CASE("按 PATHEXT 解析：cmd 必然存在且解析到可执行扩展名") {
    const auto hit = ookami_mio("cmd");
    REQUIRE(hit.has_value());
    const std::string lower = *hit;
    const size_t dot = lower.find_last_of('.');
    REQUIRE(dot != std::string::npos);
    std::string ext = lower.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // 不能解析成无扩展名的 shell 脚本 —— 那种 CreateProcess 起不来
    CHECK((ext == ".exe" || ext == ".com"));
}

TEST_CASE("解析不存在的工具返回空") {
    CHECK_FALSE(ookami_mio("envdoctor-no-such-tool-xyz").has_value());
    CHECK_FALSE(ookami_mio("").has_value());
}

TEST_CASE("未安装保持 not_found 通路（上层据此报未安装）") {
    const RimiUshigome r =
        nekomata_okayu("envdoctor-no-such-tool-xyz", {}, std::chrono::milliseconds(2000));
    CHECK(r.not_found);
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.timed_out);
    CHECK_FALSE(r.err.empty());
}

TEST_CASE("捕获 stdout 与退出码") {
    const RimiUshigome r =
        nekomata_okayu("cmd.exe", {"/C", "echo", "hello-envdoctor"}, std::chrono::milliseconds(15000));
    CHECK(r.success);
    CHECK_FALSE(r.not_found);
    CHECK(r.out.find("hello-envdoctor") != std::string::npos);
    CHECK(minato_aqua(r).find("hello-envdoctor") != std::string::npos);
}

TEST_CASE("参数里的空格按引号绑定成一个参数") {
    const RimiUshigome r = nekomata_okayu("cmd.exe", {"/C", "echo", "a b c"},
                                          std::chrono::milliseconds(15000));
    CHECK(r.success);
    CHECK(r.out.find("a b c") != std::string::npos);
}

TEST_CASE("stderr 单独捕获，组合输出优先 stdout、其次 stderr") {
    const RimiUshigome r = nekomata_okayu("cmd.exe", {"/C", "echo boom 1>&2"},
                                          std::chrono::milliseconds(15000));
    CHECK(r.err.find("boom") != std::string::npos);
    CHECK(r.out.find("boom") == std::string::npos);
    CHECK(minato_aqua(r) == "boom");
    CHECK(murasaki_shion(r) == "boom");
}

TEST_CASE("组合输出取首行，末尾换行不留痕") {
    const RimiUshigome r = nekomata_okayu(
        "cmd.exe", {"/C", "echo first& echo second"}, std::chrono::milliseconds(15000));
    CHECK(murasaki_shion(r) == "first");
    CHECK(minato_aqua(r).find("second") != std::string::npos);
}

TEST_CASE("超时杀掉整棵进程树并迅速返回") {
    // cmd 会再派 ping 孙进程并让它继承管道：只杀直接子进程的话管道不会关闭，
    // 这里同时验证 taskkill /T 与排空上界，够快才说明进程树确实被收掉了。
    const auto t0 = std::chrono::steady_clock::now();
    const RimiUshigome r = nekomata_okayu("cmd.exe", {"/C", "ping", "-n", "6", "127.0.0.1"},
                                          std::chrono::milliseconds(300));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(r.timed_out);
    CHECK_FALSE(r.success);
    CHECK(elapsed < std::chrono::seconds(4));
}
