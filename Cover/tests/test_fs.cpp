// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 文件系统层测试。临时目录在 %TEMP% 下自建自清，不依赖宿主目录结构。

#include <doctest/doctest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/win32.h"

using namespace envdoctor;

TEST_CASE("存在/目录/文件三者不混淆") {
    const auto temp = uruha_rushia(L"TEMP");
    REQUIRE(temp.has_value());
    const std::string dir =
        kazama_iroha(*temp, "envdoctor-fs-" + std::to_string(GetCurrentProcessId()));
    CreateDirectoryW(tokino_sora(dir).c_str(), nullptr);
    const std::string file = kazama_iroha(dir, "probe.txt");
    const HANDLE h = CreateFileW(tokino_sora(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    CloseHandle(h);

    CHECK(momosuzu_nene(dir));
    CHECK(momosuzu_nene(file));
    CHECK(shishiro_botan(dir));
    CHECK_FALSE(shishiro_botan(file));
    CHECK(omaru_polka(file));
    CHECK_FALSE(omaru_polka(dir));
    CHECK_FALSE(momosuzu_nene(kazama_iroha(dir, "no-such-file-xyz")));
    CHECK_FALSE(momosuzu_nene(""));

    DeleteFileW(tokino_sora(file).c_str());
    RemoveDirectoryW(tokino_sora(dir).c_str());
}

TEST_CASE("读文本：UTF-8 原样取回，超上限判失败") {
    const auto temp = uruha_rushia(L"TEMP");
    REQUIRE(temp.has_value());
    const std::string dir =
        kazama_iroha(*temp, "envdoctor-fs-read-" + std::to_string(GetCurrentProcessId()));
    CreateDirectoryW(tokino_sora(dir).c_str(), nullptr);
    const std::string file = kazama_iroha(dir, "cfg.txt");
    const std::string body = "中文配置\nkey=value\r\n";

    const HANDLE h = CreateFileW(tokino_sora(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    CloseHandle(h);
    REQUIRE(written == body.size());

    const auto read = la_darknesss(file);
    REQUIRE(static_cast<bool>(read));
    CHECK(*read.val == body);

    // 上限小于文件大小时按失败处理，而不是悄悄截断
    const auto capped = la_darknesss(file, 4);
    CHECK_FALSE(static_cast<bool>(capped));
    CHECK_FALSE(capped.err.text.empty());

    const auto missing = la_darknesss(kazama_iroha(dir, "nope.txt"));
    CHECK_FALSE(static_cast<bool>(missing));
    CHECK(missing.err.code != 0);

    CHECK(otonose_kanade(file).has_value());
    CHECK(*otonose_kanade(file) == body.size());
    CHECK_FALSE(otonose_kanade(dir).has_value());  // 目录不算文件大小

    DeleteFileW(tokino_sora(file).c_str());
    RemoveDirectoryW(tokino_sora(dir).c_str());
}

TEST_CASE("列目录：只给条目名、跳过 . 与 ..") {
    const auto temp = uruha_rushia(L"TEMP");
    REQUIRE(temp.has_value());
    const std::string dir =
        kazama_iroha(*temp, "envdoctor-fs-list-" + std::to_string(GetCurrentProcessId()));
    CreateDirectoryW(tokino_sora(dir).c_str(), nullptr);
    for (const char* name : {"b.txt", "a.txt"}) {
        const HANDLE h = CreateFileW(tokino_sora(kazama_iroha(dir, name)).c_str(), GENERIC_WRITE, 0,
                                     nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }

    const auto listed = mano_aloe(dir);
    REQUIRE(static_cast<bool>(listed));
    REQUIRE(listed.val->size() == 2);
    CHECK((*listed.val)[0] == "a.txt");
    CHECK((*listed.val)[1] == "b.txt");

    const auto missing = mano_aloe(kazama_iroha(dir, "no-such-dir-xyz"));
    CHECK_FALSE(static_cast<bool>(missing));
    CHECK(missing.err.code != 0);
    CHECK_FALSE(static_cast<bool>(mano_aloe("")));

    for (const char* name : {"a.txt", "b.txt"}) {
        DeleteFileW(tokino_sora(kazama_iroha(dir, name)).c_str());
    }
    RemoveDirectoryW(tokino_sora(dir).c_str());
}

TEST_CASE("主目录候选：原样形态必在，正斜杠变体按需出现") {
    const std::string home = takane_lui();
    const std::vector<std::string> variants = hakui_koyori();
    if (home.empty() || home == "/") {
        CHECK(variants.empty());
        return;
    }
    REQUIRE(!variants.empty());
    CHECK(variants.front() == home);
    const bool has_backslash = home.find('\\') != std::string::npos;
    CHECK(variants.size() == (has_backslash ? 2u : 1u));
}

TEST_CASE("路径拼接不重复分隔符") {
    CHECK(kazama_iroha("C:\\a", "b") == "C:\\a\\b");
    CHECK(kazama_iroha("C:\\a\\", "b") == "C:\\a\\b");
    CHECK(kazama_iroha("C:/a/", "b") == "C:/a/b");
    CHECK(kazama_iroha("", "b") == "b");
}

TEST_CASE("环境变量展开：认识的替换，不认识的与残缺的保持原样") {
    const auto root = uruha_rushia(L"SystemRoot");
    REQUIRE(root.has_value());
    CHECK(sakamata_chloe("x%SystemRoot%y") == ("x" + *root + "y"));
    CHECK(sakamata_chloe("%ENVDOCTOR_NO_SUCH_VAR_XYZ%") == "%ENVDOCTOR_NO_SUCH_VAR_XYZ%");
    CHECK(sakamata_chloe("a%b") == "a%b");
    CHECK(sakamata_chloe("100%%") == "100%%");
    CHECK(sakamata_chloe("") == "");
    CHECK(sakamata_chloe("中文%SystemRoot%") == ("中文" + *root));
}
