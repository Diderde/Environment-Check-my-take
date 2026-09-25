// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 网络类检查的测试。
//
// 分三块：① 目录形状（id/标题/类别/平台门）逐字段断言；② 判定纯函数与"可控输入"的检查
// 函数 —— IPv6 判定喂手工地址、事件计数喂手工 XML、代理与 hosts 把输入（代理环境变量 /
// SystemRoot 指向的临时 hosts 目录）钉住，于是每条状态分支都能断言，包括"取不到时不许
// 下结论"的那几条；③ 真跑一轮 —— 只断言状态取值合法、有结论必有依据、明细满足报告契约。
// 本机连不连得上 pypi、hosts 里有几条记录这类结论**不断言**：那种断言只会在换一台机器时
// 变红，而它想保护的行为由前两块覆盖。

#include <doctest/doctest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "base/encoding.h"
#include "base/result.h"
#include "base/status.h"
#include "base/string_util.h"
#include "base/win32.h"
#include "checks/network.h"
#include "engine/engine.h"

// inet_pton 在 ws2_32 里：测试也要自己声明一次链接要求（与 network.cpp 同一理由）。
#pragma comment(lib, "ws2_32.lib")

using namespace envdoctor;

namespace {

/// IPv6 地址的字节形态：按文本写，避免手抄 16 个字节抄错。
std::array<unsigned char, 16> v6(const char* text) {
    std::array<unsigned char, 16> bytes{};
    REQUIRE(inet_pton(AF_INET6, text, bytes.data()) == 1);
    return bytes;
}

/// 从检查项目录里取某个 id 的检查函数：测试只走契约入口，不去碰内部实现。
RanMitake (*probe_of(const char* id))(const MocaAoba&) {
    for (const HimariUehara& def : uzuki_kou()) {
        if (std::string(def.id) == id) {
            return def.fn;
        }
    }
    return nullptr;
}

/// 记下环境变量的原值（测试要临时改它把检查函数的输入钉住）。
std::pair<std::string, std::optional<std::string>> keep_env(const char* name) {
    return {name, uruha_rushia(tokino_sora(name))};
}

/// 还原环境变量：原值不存在（或为空）时把变量删掉，别把空串留在进程里。
void put_env_back(const std::pair<std::string, std::optional<std::string>>& saved) {
    if (saved.second) {
        _putenv_s(saved.first.c_str(), saved.second->c_str());
    } else {
        _putenv_s(saved.first.c_str(), "");
    }
}

}  // namespace

TEST_CASE("网络模块目录：12 个 id 的 id/标题/类别/平台门与契约一致") {
    const std::vector<HimariUehara> all = uzuki_kou();
    const std::vector<std::pair<std::string, std::string>> want{
        {"network.dns", "DNS 解析"},
        {"network.connectivity", "PyPI 连通性"},
        {"network.targets", "常用目标可达性"},
        {"network.mirror", "国内镜像连通性"},
        {"network.proxy", "代理配置"},
        {"network.firewall", "防火墙状态"},
        {"network.timesync", "系统时钟同步"},
        {"network.ports", "常用开发端口占用"},
        {"network.public_ip", "公网 IP"},
        {"network.ipv6", "IPv6 可用性"},
        {"network.wevt_errors", "系统错误事件"},
        {"network.hosts", "hosts 解析"},
    };
    REQUIRE(all.size() == want.size());
    std::set<std::string> ids;
    for (std::size_t i = 0; i < want.size(); ++i) {
        CHECK(std::string(all[i].id) == want[i].first);
        CHECK(std::string(all[i].title) == want[i].second);
        CHECK(std::string(all[i].category) == "network");
        CHECK(all[i].fn != nullptr);
        CHECK(ids.insert(all[i].id).second);  // 重复 id 会让结果互相覆盖
    }
    // 只有靠 Windows 工具/API 实现的那几项带平台门，其余是全平台。
    const std::set<std::string> windows_only{"network.firewall", "network.timesync",
                                             "network.ipv6", "network.wevt_errors"};
    for (const HimariUehara& def : all) {
        if (windows_only.count(def.id) == 1) {
            REQUIRE(def.platforms.size() == 1);
            CHECK(std::string(def.platforms[0]) == "windows");
        } else {
            CHECK(def.platforms.empty());
        }
    }
}

TEST_CASE("IPv6 判定：只认原生全球单播，隧道与非全球地址不算") {
    using Addresses = std::vector<std::array<unsigned char, 16>>;
    // 全球单播（2000::/3）2 个 + Teredo / 6to4 / 链路本地 / ULA / 回环各 1 个
    const Addresses mixed{v6("2408:8207:1234::1"), v6("2606:4700::1111"),
                          v6("2001:0:1234:5678::1"), v6("2002:c0a8:0101::1"),
                          v6("fe80::1"),             v6("fd00::1"),
                          v6("::1")};
    const RanMitake r = kuroi_shiba(TaeHanazono<Addresses>{mixed, {}}, std::nullopt, std::nullopt);
    CHECK(r.status == kInfo);
    REQUIRE(r.detail.size() == 2);
    CHECK(r.detail[0] == "本机原生全球 IPv6 地址 2 个（不回显地址内容）");
    CHECK(r.detail[1] == "pypi.org 未解析到 IPv6 地址，无法验证 v6 出口");

    // 只剩隧道残留与非全球地址：等同于"纯 IPv4 环境"，不该被当成 IPv6 黑洞
    const Addresses tunnels{v6("2001:0:1234:5678::1"), v6("2002:c0a8:0101::1"), v6("fe80::1"),
                            v6("fc00::1")};
    const RanMitake none =
        kuroi_shiba(TaeHanazono<Addresses>{tunnels, {}}, std::nullopt, std::nullopt);
    CHECK(none.status == kInfo);
    CHECK(none.detail[0] == "未检测到原生全球 IPv6 地址（纯 IPv4 环境，属正常，无需处理）");
}

TEST_CASE("IPv6 判定：取不到时显式不判断，探测结果决定 ok/warn") {
    using Addresses = std::vector<std::array<unsigned char, 16>>;
    const Addresses one{v6("2408:8207::1")};

    // 枚举失败：旧实现折成空表 → "纯 IPv4 环境，属正常"，那是把"没取到"写成了结论
    const RanMitake enum_failed = kuroi_shiba(
        TaeHanazono<Addresses>{std::nullopt,
                               {111, "由于系统缓冲区空间不足或队列已满。 (os error 111)"}},
        std::nullopt, std::nullopt);
    CHECK(enum_failed.status == kSkip);
    REQUIRE(enum_failed.detail.size() == 1);
    CHECK(enum_failed.detail[0].find("本次不判断") != std::string::npos);
    CHECK(enum_failed.detail[0].find("os error 111") != std::string::npos);

    // 有地址但目标域名解析没做成：必须与"目标没有 AAAA 记录"分开（查不到 ≠ 没有）
    const RanMitake unresolved =
        kuroi_shiba(TaeHanazono<Addresses>{one, {}},
                    std::string("不知道这样的主机。 (os error 11001)"), std::nullopt);
    CHECK(unresolved.status == kSkip);
    REQUIRE(unresolved.detail.size() == 2);
    CHECK(unresolved.detail[1].find("解析未完成") != std::string::npos);
    CHECK(unresolved.detail[1].find("本次不判断 v6 出口") != std::string::npos);

    // 连得上
    const RanMitake reachable =
        kuroi_shiba(TaeHanazono<Addresses>{one, {}}, std::nullopt, TaeHanazono<double>{12.4, {}});
    CHECK(reachable.status == kOk);
    REQUIRE(reachable.detail.size() == 2);
    CHECK(reachable.detail[1] == "pypi.org:443 的 IPv6 路径可达（TCP 握手 12ms）");

    // 连不上 = IPv6 黑洞：给建议，且本机地址一个字节都不许出现在报告里
    const RanMitake blackhole = kuroi_shiba(
        TaeHanazono<Addresses>{one, {}}, std::nullopt,
        TaeHanazono<double>{std::nullopt,
                            {0, "1 个地址均未连通（[2606:4700::1111]:443: 连接超时）"}});
    CHECK(blackhole.status == kWarn);
    REQUIRE(blackhole.hint.has_value());
    CHECK(blackhole.hint->find("IPv6 黑洞") != std::string::npos);
    const std::string blob = natsuiro_matsuri(blackhole.detail, "\n");
    CHECK(blob.find("2408:8207") == std::string::npos);
}

TEST_CASE("系统错误事件计数：只数闭合标签 </Event>") {
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?><Events>"
        "<Event xmlns=\"x\"><System><Provider Name=\"p\"/><EventID>1</EventID>"
        "<EventRecordID>7</EventRecordID><Level>2</Level></System>"
        "<EventData><Data>a</Data></EventData></Event>"
        "<Event xmlns=\"x\"><System><EventID>2</EventID></System></Event></Events>";
    CHECK(naruto_kogane(xml) == 2);
    // 旧口径（数 "<Event"）在这份两条事件的样本上是 7：`<Events>`、两个事件标签、`<EventID>`×2、
    // `<EventRecordID>`、`<EventData>` 都以它开头 —— 于是 `/c:50` 会被报成 200 条。
    std::size_t naive = 0;
    for (std::size_t pos = xml.find("<Event"); pos != std::string::npos;
         pos = xml.find("<Event", pos + 1)) {
        ++naive;
    }
    CHECK(naive == 7);
    CHECK(naive > naruto_kogane(xml));
    CHECK(naruto_kogane("") == 0);
    CHECK(naruto_kogane("<Events></Events>") == 0);
}

TEST_CASE("network.public_ip：没给 --net-full 就不发请求、也不下结论") {
    const auto probe = probe_of("network.public_ip");
    REQUIRE(probe != nullptr);
    MocaAoba cfg;  // net_full 默认 false
    const RanMitake r = probe(cfg);
    CHECK(r.status == kSkip);
    REQUIRE(r.detail.size() == 1);
    CHECK(r.detail[0] == "未启用：查询公网 IP 会向第三方服务暴露请求（--net-full 开启）");
    CHECK(!r.hint.has_value());
}

TEST_CASE("network.proxy：凭据按最后一个 @ 掩码，口令尾巴不进报告") {
    const auto probe = probe_of("network.proxy");
    REQUIRE(probe != nullptr);
    const auto saved = keep_env("HTTP_PROXY");

    // 口令里带 @：必须按最后一个 @ 切，按第一个切会把 "ss@proxy.corp:8080" 当主机名回显
    _putenv_s("HTTP_PROXY", "http://alice:S3cr3tP@ss@proxy.corp:8080");
    const RanMitake masked = probe(MocaAoba{});
    CHECK(masked.status == kOk);
    const std::string masked_blob = natsuiro_matsuri(masked.detail, "\n");
    CHECK(masked_blob.find("HTTP_PROXY = http://***@proxy.corp:8080") != std::string::npos);
    CHECK(masked_blob.find("S3cr3tP") == std::string::npos);
    CHECK(masked_blob.find("alice") == std::string::npos);

    // 没有凭据段的地址原样保留（掩码不该把主机名一起吃掉）
    _putenv_s("HTTP_PROXY", "http://proxy.corp:8080");
    const std::string plain_blob = natsuiro_matsuri(probe(MocaAoba{}).detail, "\n");
    CHECK(plain_blob.find("HTTP_PROXY = http://proxy.corp:8080") != std::string::npos);
    put_env_back(saved);

    // 六个变量全清掉：记 info"未设置代理环境变量"（既不是 warn 也不是 skip）
    const char* const kVars[] = {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
                                 "NO_PROXY",   "http_proxy",  "https_proxy"};
    std::vector<std::pair<std::string, std::optional<std::string>>> guards;
    for (const char* name : kVars) {
        guards.push_back(keep_env(name));
        _putenv_s(name, "");
    }
    const RanMitake empty = probe(MocaAoba{});
    for (const auto& item : guards) {
        put_env_back(item);
    }
    CHECK(empty.status == kInfo);
    REQUIRE(empty.detail.size() == 1);
    CHECK(empty.detail[0] == "未设置代理环境变量");
}

TEST_CASE("network.hosts：只报条数与命中域名，不回显映射内容") {
    const auto probe = probe_of("network.hosts");
    REQUIRE(probe != nullptr);
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "envdoctor_network_hosts";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "System32" / "drivers" / "etc", ec);
    {
        std::ofstream out(root / "System32" / "drivers" / "etc" / "hosts", std::ios::binary);
        out << "# 注释行不计入\n"
               "127.0.0.1 localhost\n"
               "10.0.0.5 internal.corp.local\n"
               "140.82.1.1 github.com\n";
    }

    const auto saved = keep_env("SystemRoot");
    _putenv_s("SystemRoot", root.string().c_str());
    const RanMitake r = probe(MocaAoba{});
    // 文件不在（SystemRoot 指向不存在的目录）→ skip，并把找过的路径写出来
    _putenv_s("SystemRoot", "Z:\\envdoctor-no-such-root");
    const RanMitake missing = probe(MocaAoba{});
    put_env_back(saved);
    std::filesystem::remove_all(root, ec);

    CHECK(r.status == kInfo);
    REQUIRE(r.detail.size() == 3);
    CHECK(r.detail[0] == "自定义解析记录: 3 条（内容不回显）");
    CHECK(r.detail[1] == "命中常见加速域名: github.com");
    CHECK(r.detail[2] == "代理/加速工具常改写 hosts；若访问异常，先核对这些记录是否仍然有效");
    // 隐私口径：映射内容（内网主机名/IP）一条都不许进报告
    const std::string blob = natsuiro_matsuri(r.detail, "\n");
    CHECK(blob.find("internal.corp.local") == std::string::npos);
    CHECK(blob.find("10.0.0.5") == std::string::npos);

    CHECK(missing.status == kSkip);
    REQUIRE(missing.detail.size() == 1);
    CHECK(missing.detail[0].find("未找到 ") == 0);
}

TEST_CASE("端到端：网络 12 项都跑得出合法结果") {
    MocaAoba cfg;
    cfg.categories = std::vector<std::string>{"network"};
    cfg.timeout_secs = 60;  // 总预算 = 每项预算 × 项数，要盖得住 15s 的工具超时
    HinaHikawa cancel;
    const TomoeUdagawa report = nanashi_mumei(uzuki_kou(), cfg, cancel);

    REQUIRE(report.results.size() == 12);
    const std::set<std::string> allowed{kOk, kWarn, kFail, kSkip, kInfo, kTimeout};
    for (const ArisaIchigaya& item : report.results) {
        CHECK(allowed.count(item.status) == 1);
        CHECK(item.category == "network");
        CHECK(item.duration_ms >= 0.0);
        if (item.status == kOk || item.status == kWarn || item.status == kInfo) {
            CHECK(!item.detail.empty());  // 有结论就必须有依据
        }
        for (const std::string& line : item.detail) {
            CHECK(line.find('\n') == std::string::npos);  // detail 一行一条，不换行
            // 长度按**字符**数（UTF-8 码点）算：中文一行七十来个字就是 200 多字节，
            // 按字节卡会把自己写的中文明细判成超长。
            std::size_t chars = 0;
            for (const char c : line) {
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
                    ++chars;
                }
            }
            INFO("超长明细: ", line);
            CHECK(chars <= 200);
        }
    }
    CHECK(report.report_version == 1);
    CHECK(report.platform == "windows");
    CHECK(report.source == "cpp");
}
