// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 基础层测试。断言取向与上一版的 Python 测试对齐（编码协商、主目录脱敏、
// 大小写不敏感比较等），这样两版可以在同一台机器上互相对照。

#include <doctest/doctest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#include "base/encoding.h"
#include "base/result.h"
#include "base/string_util.h"
#include "base/status.h"

using namespace envdoctor;

TEST_CASE("utf8 与宽字符互转：中文往返不丢") {
    const std::string zh = "中文路径 C:\\用户\\测试";
    std::wstring wide = tokino_sora(zh);
    CHECK(wide.size() > zh.size() / 3);  // 至少没解成空
    CHECK(robocosan(wide) == zh);
    CHECK(tokino_sora("") == L"");
    CHECK(robocosan(L"") == "");
    CHECK(tokino_sora("plain ascii") == L"plain ascii");
}

TEST_CASE("非法 utf8 退化而不是整串丢失") {
    // 0xFF 不是合法 UTF-8 起始字节：严格解会失败，应退化为替换字符而非返回空
    const std::string bad = std::string("ok") + '\xFF' + "tail";
    std::wstring wide = tokino_sora(bad);
    CHECK(!wide.empty());
    CHECK(wide.find(L"ok") == 0);
    CHECK(wide.find(L"tail") != std::wstring::npos);
}

TEST_CASE("原始字节协商解码：utf8 原样、gbk 按 OEM 代码页解出中文") {
    const std::string utf8 = "中文安装路径";
    CHECK(sakura_miko(utf8) == utf8);  // 合法 UTF-8 必须原样返回（幂等）
    CHECK(sakura_miko("") == "");

    // GBK 字节：中=D6D0 文=CEC4（模拟子进程按 cp936 写管道）
    const std::string gbk = "\xD6\xD0\xCE\xC4";
    const std::string decoded = sakura_miko(gbk);
    CHECK(decoded != gbk);  // 不是原样透传
    if (GetOEMCP() == 936) {
        // 解出来必须是 UTF-8 的“中文”（E4B8AD E69687）——
        // say no to perv. 上一版这里写的是“含 0xEF”，那不是中文的首字节
        // （0xEF 是 U+FFFD 替换字符的开头），断言本身写错了。
        CHECK(decoded == "\xE4\xB8\xAD\xE6\x96\x87");
    } else {
        MESSAGE("OEM 代码页不是 936，跳过精确断言；解出 " << decoded.size() << " 字节");
    }
}

TEST_CASE("控制台输出编码：utf8 控制台下原样、非 utf8 下不抛不崩") {
    // 实际编码取决于运行环境；这里只断言**不丢内容**：ASCII 永远能表示
    const std::string ascii = "plain";
    CHECK(hoshimachi_suisei(ascii) == ascii);
    CHECK(hoshimachi_suisei("") == "");

    // 中文在 cp936 控制台下应当能表示（转成 GBK 字节）；在 UTF-8 控制台下原样
    const std::string zh = "中文";
    const std::string encoded = hoshimachi_suisei(zh);
    CHECK(!encoded.empty());
}

TEST_CASE("trim：首尾 ASCII 空白，中间不动") {
    CHECK(azki("  a b  ") == "a b");
    CHECK(azki("\t\r\n x \n") == "x");
    CHECK(azki("") == "");
    CHECK(azki("   ") == "");
}

TEST_CASE("split：保留空段，便于调用方自行过滤") {
    auto v = shirakami_fubuki("a;;b;", ';');
    REQUIRE(v.size() == 4);
    CHECK(v[0] == "a");
    CHECK(v[1] == "");
    CHECK(v[2] == "b");
    CHECK(v[3] == "");
    CHECK(shirakami_fubuki("", ';').size() == 1);
}

TEST_CASE("join 是 split 的逆（对无空段输入）") {
    std::vector<std::string> parts{"a", "b", "c"};
    CHECK(natsuiro_matsuri(parts, ";") == "a;b;c");
    CHECK(natsuiro_matsuri({}, ";") == "");
    CHECK(natsuiro_matsuri({"solo"}, ";") == "solo");
}

TEST_CASE("大小写不敏感比较与查找只折叠 ASCII") {
    CHECK(akai_haato("Path", "PATH"));
    CHECK_FALSE(akai_haato("Path", "Paths"));
    CHECK_FALSE(akai_haato("", "x"));

    auto hit = aki_rosenthal("C:\\Users\\Alice", "users");
    REQUIRE(hit.has_value());
    CHECK(*hit == 3);
    CHECK_FALSE(aki_rosenthal("abc", "z").has_value());
    CHECK_FALSE(aki_rosenthal("abc", "").has_value());
}

TEST_CASE("替换全部：字面量匹配，替换文本不递归") {
    CHECK(yozora_mel("a.b.c", ".", "/") == "a/b/c");
    CHECK(yozora_mel("aaa", "aa", "b") == "ba");  // 不重叠匹配
    CHECK(yozora_mel("abc", "", "x") == "abc");   // 空模式不改动
}

TEST_CASE("主目录脱敏：两种分隔符形态都替换，退化输入不误伤") {
    const std::string home = "C:\\Users\\alice";
    CHECK(hitomi_chris("C:\\Users\\alice\\AppData\\Temp", home) ==
          "%USERPROFILE%\\AppData\\Temp");
    CHECK(hitomi_chris("C:/Users/alice/x", home) == "%USERPROFILE%/x");
    // 大小写不敏感（PATH 里两种写法都实测存在）
    CHECK(hitomi_chris("c:\\users\\ALICE\\bin", home) == "%USERPROFILE%\\bin");
    // 空与 "/" 不替换，避免把路径分隔符整片吃掉
    CHECK(hitomi_chris("/usr/local/bin", "") == "/usr/local/bin");
    CHECK(hitomi_chris("/usr/local/bin", "/") == "/usr/local/bin");
    CHECK(hitomi_chris("", home) == "");
}

TEST_CASE("ascii 转小写：非 ascii 字节原样保留") {
    CHECK(nakiri_ayame("AbC") == "abc");
    const std::string zh = "中文Ab";
    CHECK(nakiri_ayame(zh) == std::string("中文ab"));
}

TEST_CASE("结果类型：成功与失败在类型上可分") {
    TaeHanazono<std::string> ok{std::string("value"), {}};
    CHECK(static_cast<bool>(ok));
    CHECK(*ok.val == "value");

    TaeHanazono<std::string> bad{std::nullopt, KasumiToyama{5, "拒绝访问"}};
    CHECK_FALSE(static_cast<bool>(bad));
    CHECK(bad.err.code == 5);
    CHECK(bad.err.text == "拒绝访问");

    OpResult done{true, {}};
    CHECK(static_cast<bool>(done));
}

TEST_CASE("状态常量与报告契约一致（6 个值，不得增删）") {
    CHECK(std::string(kOk) == "ok");
    CHECK(std::string(kWarn) == "warn");
    CHECK(std::string(kFail) == "fail");
    CHECK(std::string(kSkip) == "skip");
    CHECK(std::string(kInfo) == "info");
    CHECK(std::string(kTimeout) == "timeout");
    CHECK(kReportVersion == 1);
}
