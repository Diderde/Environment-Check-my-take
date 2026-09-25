// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 网络类检查：DNS 解析 / PyPI 连通性 / 常用目标可达性 / 国内镜像 / 代理配置 / 防火墙状态 /
// 系统时钟同步 / 常用开发端口占用 / 公网 IP（隐私门控）/ IPv6 可用性 / 系统错误事件 /
// hosts 解析。
//
// 三件事在这里被刻意分开：
//   · **探测**只负责拿到结果（套接字、白名单工具、Win32 API、文件），失败一律走结果类型的
//     错误字段，不返回"看起来像结论"的空值；
//   · **判定**里能脱离宿主环境的那两块（IPv6 结论映射、事件条数）是纯函数，在 network.h
//     导出，逐条状态分支都能离线断言；
//   · **共享的套接字原语**（会话初始化 / 地址解析 / 单次连接 / 预算内连接）与定点小数文本
//     只在本文件内使用，做成文件级 lambda 对象，不进头文件。
//
// 逐字段等价是硬约束：文案、阈值、超时秒数、命令参数与被排除的特殊情况全部照抄旧实现。
// 唯一允许的改动是把"取不到当成结论"的地方改判为显式不判断 —— 这类点都在代码里用注释
// 写明了原来错在哪。

#include "checks/network.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// windows.h 默认把 min/max 定义成宏，而本文件的"每地址预算取小"用的是 std::min：
// 不关掉宏会在 `std::min(` 处报 C2589，且报错点离真正原因很远。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/process.h"
#include "base/status.h"
#include "base/string_util.h"
#include "base/win32.h"
#include "win/tools.h"

// ws2_32 / iphlpapi 由本文件自己声明链接：基础层刻意不碰网络库（它是"不依赖任何 Windows
// UI/网络库"的一层），把这份链接要求留在唯一用到它们的模块里，别的地方不必跟着多一份。
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace envdoctor {

RanMitake kuroi_shiba(const TaeHanazono<std::vector<std::array<unsigned char, 16>>>& native_addrs,
                      const std::optional<std::string>& unresolved_reason,
                      const std::optional<TaeHanazono<double>>& connect) {
    if (!native_addrs) {
        // say no to perv. —— 旧实现把"枚举 API 失败"折成空表，再一路落进"未检测到原生全球
        // IPv6 地址（纯 IPv4 环境，属正常）"：一次调用失败被写成了环境结论。枚举没做成时，
        // 地址数量、隧道残留、黑洞全都无从谈起，这里显式不判断。
        return isaki_riona({"无法枚举本机 IPv6 地址（" + native_addrs.err.text +
                            "），本次不判断 IPv6 可用性"});
    }
    std::size_t count = 0;
    for (const std::array<unsigned char, 16>& bytes : *native_addrs.val) {
        // 只认原生全球单播：`2000::/3`（RFC 4291 全球单播），并排除两种隧道形态 ——
        // `2001:0000::/32`（Teredo）与 `2002::/16`（6to4）。隧道地址连不通外网目标是常态，
        // 把它们算进来会把"纯 IPv4 + 隧道残留"误报成 IPv6 黑洞。
        if ((bytes[0] & 0xe0) != 0x20) {
            continue;
        }
        if (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00) {
            continue;
        }
        if (bytes[0] == 0x20 && bytes[1] == 0x02) {
            continue;
        }
        ++count;
    }
    if (count == 0) {
        return hiodoshi_ao({"未检测到原生全球 IPv6 地址（纯 IPv4 环境，属正常，无需处理）"});
    }
    // 地址本身属于可定位信息（等同公网 IP），只报数量、不回显内容。
    std::vector<std::string> detail{"本机原生全球 IPv6 地址 " + std::to_string(count) +
                                    " 个（不回显地址内容）"};
    if (unresolved_reason) {
        // say no to perv. —— 旧实现把"域名解析没做成"与"目标没有 AAAA 记录"折进同一支，
        // 于是 DNS 查询失败被写成"pypi.org 未解析到 IPv6 地址"这个结论。查不到 ≠ 没有；
        // 这里显式不判断，并把解析失败的原因写出来。
        detail.push_back("pypi.org 的 IPv6 解析未完成（" + *unresolved_reason +
                         "），本次不判断 v6 出口");
        return isaki_riona(detail);
    }
    if (!connect) {
        detail.push_back("pypi.org 未解析到 IPv6 地址，无法验证 v6 出口");
        return hiodoshi_ao(detail);
    }
    if (connect->val) {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "%.0f", *connect->val);
        detail.push_back(std::string("pypi.org:443 的 IPv6 路径可达（TCP 握手 ") + buf + "ms）");
        return todoroki_hajime(detail);
    }
    detail.push_back("pypi.org:443 的 IPv6 路径不可达：" + connect->err.text);
    return juufuutei_raden(
        kWarn, detail,
        "IPv6 黑洞：系统会优先尝试 IPv6、失败后才回落 IPv4，表现为 pip/git 偶发卡顿与超时；"
        "可在网卡属性里取消勾选“Internet 协议版本 6 (TCP/IPv6)”，或让路由器下发可用的 IPv6 前缀");
}

std::size_t naruto_kogane(const std::string& xml) {
    constexpr const char* kClosingTag = "</Event>";
    constexpr std::size_t kClosingTagLen = 8;
    std::size_t count = 0;
    for (std::size_t pos = xml.find(kClosingTag); pos != std::string::npos;
         pos = xml.find(kClosingTag, pos + kClosingTagLen)) {
        ++count;
    }
    return count;
}

namespace {

/// 套接字层只初始化一次；失败就意味着这一项没有答案。
///
/// `WSAStartup` 是进程级的，重复调用只增加引用计数并返回同一结果，缓存起来即可
/// （本文件从不调用 `WSACleanup`）。
const auto winsock_ready = [] {
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
};

/// 定点小数文本（位数由调用方给定）。显示口径与旧实现的 `{ms:.0}` / `{ms:.1}` 一致：
/// 四舍五入到指定位数、不做科学计数法、不带千分位。
const auto fixed_text = [](double value, int decimals) {
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return std::string(buf);
};

/// 名字解析 → "文本形态 + 套接字地址"。
///
/// 文本给文案用、地址给连接用，合成一份可以避免"解析一次、连接时再解析一次"。
/// `family` 传 `AF_UNSPEC` 时与旧实现的 `to_socket_addrs()` 同口径（IPv4/IPv6 都拿回来，
/// 调用方按需过滤）；解析失败时 `err.code` / `err.text` 是系统错误码与可读文本。
const auto resolve_addresses = [](const std::string& host, unsigned short port, int family)
        -> TaeHanazono<std::vector<std::pair<std::string, sockaddr_storage>>> {
    using Address = std::pair<std::string, sockaddr_storage>;
    if (!winsock_ready()) {
        return {std::nullopt, {0, "Winsock 初始化失败"}};
    }
    ADDRINFOA hints{};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const std::string service = std::to_string(port);
    ADDRINFOA* list = nullptr;
    const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &list);
    if (rc != 0) {
        // Windows 的 EAI_* 就是 WSA 错误码，可以直接交给 FormatMessage 取可读文本。
        if (rc > 0) {
            return {std::nullopt,
                    {static_cast<long>(rc), houshou_marine(static_cast<unsigned long>(rc))}};
        }
        return {std::nullopt, {0, "解析失败（getaddrinfo 返回 " + std::to_string(rc) + "）"}};
    }
    std::vector<Address> out;
    for (ADDRINFOA* it = list; it != nullptr; it = it->ai_next) {
        if (it->ai_addr == nullptr || it->ai_addrlen == 0) {
            continue;
        }
        char text[NI_MAXHOST] = {};
        if (getnameinfo(it->ai_addr, static_cast<socklen_t>(it->ai_addrlen), text, sizeof(text),
                        nullptr, 0, NI_NUMERICHOST) != 0) {
            continue;  // 连地址都印不出来：这条不进文案，免得报出一串问号
        }
        sockaddr_storage storage{};
        std::memcpy(&storage, it->ai_addr, static_cast<std::size_t>(it->ai_addrlen));
        std::string rendered = text;
        if (it->ai_addr->sa_family == AF_INET6) {
            rendered = "[" + rendered + "]";  // 与旧实现的 [v6]:port 文案对齐
        }
        rendered += ":" + service;
        out.emplace_back(std::move(rendered), storage);
    }
    freeaddrinfo(list);
    return {std::move(out), {}};
};

/// 连一个地址。非阻塞 + `select` 才能给出毫秒级上限（阻塞式 `connect` 没有超时参数）。
///
/// 失败分两类，调用方按 `err.code` 区分：
///   · `err.code != 0` —— **连接层面**的结果（拒绝、超时、不可达…），错误码是 Winsock 码，
///     到点没答复统一记 `WSAETIMEDOUT`（与旧实现 `connect_timeout` 的错误文案一致）；
///   · `err.code == 0` —— **探测机制**自己失败（Winsock 起不来、socket/select/getsockopt
///     报错）：这次什么都没测到，绝不能被当成"连不上"。
const auto connect_one = [](const sockaddr_storage& target, std::chrono::milliseconds budget)
        -> TaeHanazono<double> {
    if (!winsock_ready()) {
        return {std::nullopt, {0, "Winsock 初始化失败"}};
    }
    const int len = target.ss_family == AF_INET6 ? static_cast<int>(sizeof(sockaddr_in6))
                                                 : static_cast<int>(sizeof(sockaddr_in));
    const SOCKET sock = socket(target.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        const long code = WSAGetLastError();
        return {std::nullopt,
                {0, "套接字层失败: " + houshou_marine(static_cast<unsigned long>(code))}};
    }
    u_long nonblocking = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblocking) != 0) {
        const long code = WSAGetLastError();
        closesocket(sock);
        return {std::nullopt,
                {0, "套接字层失败: " + houshou_marine(static_cast<unsigned long>(code))}};
    }
    const auto t0 = std::chrono::steady_clock::now();
    long code = 0;
    bool connected = false;
    bool probe_broken = false;
    if (connect(sock, reinterpret_cast<const sockaddr*>(&target), len) == 0) {
        connected = true;
    } else {
        const long first = WSAGetLastError();
        if (first == WSAEWOULDBLOCK || first == WSAEINPROGRESS) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(sock, &writable);
            timeval tv{};
            tv.tv_sec = static_cast<long>(budget.count() / 1000);
            tv.tv_usec = static_cast<long>((budget.count() % 1000) * 1000);
            const int ready = select(0, nullptr, &writable, nullptr, &tv);
            if (ready > 0) {
                int so_error = 0;
                int opt_len = static_cast<int>(sizeof(so_error));
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error),
                               &opt_len) != 0) {
                    probe_broken = true;  // 连"为什么失败"都问不出来：这次探测不可信
                    code = WSAGetLastError();
                } else if (so_error != 0) {
                    code = so_error;
                } else {
                    connected = true;
                }
            } else if (ready == 0) {
                code = WSAETIMEDOUT;  // 到点没答复
            } else {
                probe_broken = true;  // select 自身出错
                code = WSAGetLastError();
            }
        } else {
            code = first;
        }
    }
    closesocket(sock);
    if (connected) {
        return {std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count(),
                {}};
    }
    if (probe_broken) {
        return {std::nullopt,
                {0, "套接字层失败: " + houshou_marine(static_cast<unsigned long>(code))}};
    }
    return {std::nullopt, {code, houshou_marine(static_cast<unsigned long>(code))}};
};

/// 在**总预算**内连接给定的若干地址：预算按地址数摊开（夹在 200ms..1500ms），
/// 再用截止时间兜底。
///
/// 为什么不给每个地址一整份 timeout：`pypi.org` 实测能解析出 8 个地址（IPv4+IPv6），
/// 一个不可达目标最坏会花掉 8×timeout。空地址表直接报错，不假装连通。
const auto connect_within_budget =
    [](const std::vector<std::pair<std::string, sockaddr_storage>>& addrs,
       std::chrono::milliseconds budget) -> TaeHanazono<double> {
    if (addrs.empty()) {
        return {std::nullopt, {0, "DNS 未返回地址"}};
    }
    const long long share = std::clamp(
        static_cast<long long>(budget.count()) / static_cast<long long>(addrs.size()), 200LL,
        1500LL);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    const auto t0 = std::chrono::steady_clock::now();
    std::string last_error;
    long last_code = 0;
    for (const std::pair<std::string, sockaddr_storage>& addr : addrs) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;  // 预算已尽：不再开新连接，直接报已试过的结果
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const TaeHanazono<double> attempt =
            connect_one(addr.second, std::min(std::chrono::milliseconds(share), left));
        if (attempt) {
            return {std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                        .count(),
                    {}};
        }
        last_code = attempt.err.code;
        last_error = addr.first + ": " + attempt.err.text;
    }
    return {std::nullopt,
            {last_code,
             std::to_string(addrs.size()) + " 个地址均未连通（" + last_error + "）"}};
};

/// 解析 + 预算内连接：`err.text` 可直接拼进文案（解析没做成时形如 `DNS 解析失败: …`）。
const auto tcp_probe = [](const std::string& host, unsigned short port,
                          std::chrono::milliseconds budget) -> TaeHanazono<double> {
    const auto resolved = resolve_addresses(host, port, AF_UNSPEC);
    if (!resolved) {
        return {std::nullopt, {resolved.err.code, "DNS 解析失败: " + resolved.err.text}};
    }
    return connect_within_budget(*resolved.val, budget);
};

/// network.dns：pypi.org 能不能解析出地址（只看名字解析，不测连通性）。
RanMitake tsukimi_shizuku(const MocaAoba&) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto resolved = resolve_addresses("pypi.org", 443, AF_UNSPEC);
    if (!resolved) {
        return juufuutei_raden(kFail, {"pypi.org 解析失败: " + resolved.err.text},
                               "检查本机 DNS 配置、hosts 文件或代理软件的 DNS 接管");
    }
    if (resolved.val->empty()) {
        return juufuutei_raden(kFail, {"DNS 未返回任何地址"},
                               "检查本机 DNS 配置或尝试切换公共 DNS");
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return todoroki_hajime({"pypi.org → " + resolved.val->front().first + "（" +
                            fixed_text(ms, 1) + "ms）"});
}

/// network.connectivity：能不能直连 pypi.org:443（每目标总预算 5s）。
RanMitake achikita_chinami(const MocaAoba&) {
    const TaeHanazono<double> outcome = tcp_probe("pypi.org", 443, std::chrono::seconds(5));
    if (outcome) {
        return todoroki_hajime({"pypi.org:443 可达（TCP 握手 " + fixed_text(*outcome.val, 0) +
                                "ms）"});
    }
    return juufuutei_raden(kWarn, {"pypi.org:443 " + outcome.err.text},
                           "直连被阻断时，为 pip 配置国内镜像或为终端设置代理");
}

/// network.targets：四个常用目标的 TCP 可达性（每个目标总预算 4s）。
RanMitake naruse_naru(const MocaAoba&) {
    const std::pair<const char*, const char*> kTargets[] = {
        {"PyPI", "pypi.org"},
        {"GitHub", "github.com"},
        {"阿里云镜像", "mirrors.aliyun.com"},
        {"Python 官网", "www.python.org"},
    };
    std::vector<std::string> detail;
    std::vector<std::string> failed;
    std::size_t reachable = 0;
    for (const std::pair<const char*, const char*>& target : kTargets) {
        const std::string label = std::string(target.first) + " " + target.second + ":443";
        const TaeHanazono<double> outcome = tcp_probe(target.second, 443, std::chrono::seconds(4));
        if (outcome) {
            ++reachable;
            detail.push_back(label + " 可达（" + fixed_text(*outcome.val, 0) + "ms）");
        } else {
            failed.push_back(label);
            detail.push_back(label + " 不可达：" + outcome.err.text);
        }
    }
    if (failed.empty()) {
        return todoroki_hajime(detail);
    }
    // 全不通也只记 warn：离线开发是常态，不该判 fail。
    const std::string hint =
        reachable == 0
            ? "全部目标不可达：检查网络与代理设置，或本机处于离线开发状态"
            : std::to_string(failed.size()) + " 个目标不可达（国内网络下 GitHub 常需代理或加速器）";
    return juufuutei_raden(kWarn, detail, hint);
}

/// network.mirror：两个国内镜像源的 TCP 可达性（每个目标总预算 4s）。
RanMitake kudo_chitose(const MocaAoba&) {
    const std::pair<const char*, const char*> kMirrors[] = {
        {"清华源", "pypi.tuna.tsinghua.edu.cn"},
        {"阿里源", "mirrors.aliyun.com"},
    };
    std::vector<std::string> detail;
    std::size_t unreachable = 0;
    for (const std::pair<const char*, const char*>& mirror : kMirrors) {
        const std::string label = std::string(mirror.first) + " " + mirror.second + ":443";
        const TaeHanazono<double> outcome = tcp_probe(mirror.second, 443, std::chrono::seconds(4));
        if (outcome) {
            detail.push_back(label + " 可达（" + fixed_text(*outcome.val, 0) + "ms）");
        } else {
            ++unreachable;
            detail.push_back(std::string(mirror.first) + " 不可达：" + outcome.err.text);
        }
    }
    // 镜像不可达不算病（直连官方源可用即可）；但若照记 ok，摘要行的 ✅ 会与明细里的
    // "不可达"自相矛盾 —— 有不可达时降为 info。
    if (unreachable > 0) {
        return hiodoshi_ao(detail);
    }
    return todoroki_hajime(detail);
}

/// network.proxy：代理相关的环境变量（凭据不回显）。
RanMitake warabeda_meiji(const MocaAoba&) {
    // 顺序照抄旧实现；大小写两套都看（不同工具认的形态不同）。
    const char* const kVars[] = {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
                                 "NO_PROXY",   "http_proxy",  "https_proxy"};
    std::vector<std::string> detail;
    for (const char* name : kVars) {
        const auto value = uruha_rushia(tokino_sora(name));
        if (!value) {
            continue;
        }
        // 形如 `scheme://user:pass@host` 的代理地址不回显凭据：报告会被导出或粘贴进 issue。
        // 按**最后一个** `@` 切分 —— 口令里带 `@` 很常见，按第一个切会把口令尾巴当主机名
        // 回显；`@` 落在 scheme 里的（没有凭据段）原样返回。
        std::string shown = *value;
        const std::size_t scheme_end = shown.find("://");
        const std::size_t at = shown.rfind('@');
        if (scheme_end != std::string::npos && at != std::string::npos && at > scheme_end + 2) {
            shown = shown.substr(0, scheme_end + 3) + "***@" + shown.substr(at + 1);
        }
        detail.push_back(std::string(name) + " = " + shown);
    }
    if (detail.empty()) {
        return hiodoshi_ao({"未设置代理环境变量"});
    }
    return todoroki_hajime(detail);
}

/// network.firewall：netsh 报告的防火墙状态行（只保留含"状态/State"的行）。
RanMitake yuzuki_roa(const MocaAoba&) {
    const RimiUshigome out = anya_melfissa(YukinaMinato::NetshState, std::chrono::seconds(8));
    if (out.not_found) {
        return isaki_riona({"未找到 netsh"});
    }
    if (out.timed_out) {
        return juufuutei_raden(kTimeout, {"防火墙状态查询超时"}, std::nullopt);
    }
    // 只匹配含"状态/State"的行：旧版按 "ON"/"OFF" 子串匹配，会在别的词里乱命中。
    // 组合输出（stdout 优先、退回 stderr）先整体去空白，再逐行去空白。
    std::vector<std::string> lines;
    for (const std::string& raw : shirakami_fubuki(minato_aqua(out), '\n')) {
        const std::string line = azki(raw);
        if (line.find("状态") != std::string::npos || line.find("State") != std::string::npos) {
            lines.push_back(line);
        }
    }
    if (lines.empty()) {
        // say no to perv. —— "没解析出状态行"只说明这次输出与预期形态对不上（命令失败、输出
        // 被拦、代码页异常都可能），旧实现照记 warn（"netsh 输出中未找到状态行"），等于把
        // "没取到"写成了"防火墙有问题"。改判不适用：宁可少一条结论，也不编一条。
        return isaki_riona({out.success ? "netsh 输出中没有状态行"
                                        : "netsh 未成功执行且输出中没有状态行",
                            "本次不判断防火墙状态"});
    }
    return todoroki_hajime(lines);
}

/// network.timesync：与 ntp.aliyun.com 的时钟偏差（w32tm /stripchart，15s）。
RanMitake gundo_mirei(const MocaAoba&) {
    const RimiUshigome out = anya_melfissa(YukinaMinato::W32tmAliyun, std::chrono::seconds(15));
    if (out.not_found) {
        return isaki_riona({"未找到 w32tm"});
    }
    if (out.timed_out) {
        // 超时映射成"不判断"而不是 timeout：拿不到 NTP 应答既可能是网络不可达/UDP 123 被拦，
        // 也可能是这台机器根本没配外部时间源，判成"检查超时"会把排查方向带偏。
        return isaki_riona({"NTP 比对超时（网络不可达或 UDP 123 被拦）"});
    }
    // 输出形如 "hh:mm:ss, +00.1234567s"：按 `, ` 切开取右半段，剥掉结尾的 's'，再按
    // **小数点为 '.'** 的形态解析。这里不能用 strtod：它跟随区域设置，中文/欧洲区域下会把
    // 逗号当小数点，解析出来的偏差会差一个数量级；from_chars 与区域设置无关。
    std::optional<double> offset;
    for (const std::string& line : shirakami_fubuki(minato_aqua(out), '\n')) {
        const std::size_t cut = line.find(", ");
        if (cut == std::string::npos) {
            continue;
        }
        std::string raw = line.substr(cut + 2);
        while (!raw.empty() && raw.back() == 's') {
            raw.pop_back();
        }
        double value = 0.0;
        const char* first = raw.data();
        const char* last = raw.data() + raw.size();
        if (!raw.empty() && raw.front() == '+') {
            ++first;  // from_chars 只认 '-'：'+' 要自己跳过
        }
        const std::from_chars_result parsed = std::from_chars(first, last, value);
        if (parsed.ec == std::errc() && parsed.ptr == last) {
            offset = value;
            break;
        }
    }
    if (!offset) {
        return isaki_riona({"NTP 输出中未解析到偏差值"});
    }
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%+.3f", *offset);
    const std::string text = std::string("与 ntp.aliyun.com 偏差 ") + buf + "s";
    if (std::fabs(*offset) < 2.0) {
        return todoroki_hajime({text + "，时钟正常"});
    }
    return juufuutei_raden(kWarn, {text},
                           "时钟偏差过大会导致 TLS 证书校验失败，请开启系统自动时间同步");
}

/// network.ports：六个常用开发端口在本机是否被占用（逐个 400ms）。
RanMitake onomachi_haruka(const MocaAoba&) {
    const std::pair<unsigned short, const char*> kPorts[] = {
        {static_cast<unsigned short>(80), "HTTP"},
        {static_cast<unsigned short>(443), "HTTPS"},
        {static_cast<unsigned short>(3000), "Node dev"},
        {static_cast<unsigned short>(8000), "Python dev"},
        {static_cast<unsigned short>(8080), "HTTP-Alt"},
        {static_cast<unsigned short>(8888), "Jupyter"},
    };
    std::vector<std::string> occupied;
    std::vector<std::string> unknown;
    for (const std::pair<unsigned short, const char*>& target : kPorts) {
        sockaddr_in ipv4{};
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(target.first);
        ipv4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sockaddr_storage storage{};
        std::memcpy(&storage, &ipv4, sizeof(ipv4));
        const TaeHanazono<double> probe = connect_one(storage, std::chrono::milliseconds(400));
        const std::string label =
            "端口 " + std::to_string(target.first) + "（" + target.second + "）";
        if (probe) {
            occupied.push_back(label + "：占用");
        } else if (probe.err.code == 0) {
            // say no to perv. —— 探针自己没跑成（Winsock 起不来 / socket / select / getsockopt
            // 报错，见 `connect_one` 的 `err.code == 0`）时，旧实现照样输出"常用开发端口均空闲"，
            // 那是把"没测成"写成了"没有服务在监听"。这里显式不判断。
            // （连接层面的结果——拒绝/超时——在回环上等价于"没有监听者"，沿用旧口径：它只决定
            // 明细里写不写"占用"，不会凭空多出一句结论。）
            unknown.push_back(label + "：探测未完成（" + probe.err.text + "），本次不判断该端口");
        }
    }
    if (unknown.empty()) {
        if (occupied.empty()) {
            return hiodoshi_ao({"常用开发端口均空闲"});
        }
        return hiodoshi_ao(occupied);
    }
    std::vector<std::string> detail = occupied;
    detail.insert(detail.end(), unknown.begin(), unknown.end());
    return isaki_riona(detail);
}

/// network.public_ip：公网出口 IP（隐私门控：没给 --net-full 就**不发任何网络请求**）。
RanMitake kataribe_tsumugu(const MocaAoba& cfg) {
    if (!cfg.net_full) {
        return isaki_riona({"未启用：查询公网 IP 会向第三方服务暴露请求（--net-full 开启）"});
    }
    // curl 自带 -m 8；外层再给 12s 上限，防止 curl 卡在连接阶段不返回。
    const RimiUshigome out = anya_melfissa(YukinaMinato::CurlIpify, std::chrono::seconds(12));
    // 只看 stdout（旧实现同样不回退 stderr）：curl 的错误与进度都走 stderr，混进来会把
    // 错误文本当成 IP 报出去。
    const std::string ip = azki(out.out);
    if (out.success && !ip.empty()) {
        return todoroki_hajime({"公网 IP: " + ip});
    }
    return isaki_riona({"公网 IP 获取失败（curl 缺失或网络不可达）"});
}

/// network.ipv6：有原生全球 v6 地址却连不通 v6 目标 = 典型的"IPv6 黑洞"。
RanMitake seto_miyako(const MocaAoba&) {
    // 枚举本机网卡的 IPv6 单播地址。字段语义照抄旧实现（只要 Up 状态的非回环/非隧道网卡），
    // 但用 SDK 的真结构体 `IP_ADAPTER_ADDRESSES_LH`：旧实现手写结构体前缀 + 未对齐读取，
    // 字段偏移只能靠"布局一致"的人工保证，这里由 <iphlpapi.h> 保证。
    constexpr ULONG kFlags =
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    TaeHanazono<std::vector<std::array<unsigned char, 16>>> native{
        std::vector<std::array<unsigned char, 16>>{}, {}};
    ULONG size = 0;
    ULONG rc = GetAdaptersAddresses(AF_INET6, kFlags, nullptr, nullptr, &size);
    if (rc != ERROR_BUFFER_OVERFLOW || size == 0) {
        // 连"需要多大缓冲"都没拿到：这不是"没有地址"，是枚举没做成。错误码照实带上，
        // 由判定函数显式不判断（旧实现折成空表 → "纯 IPv4 环境，正常"）。
        native = {std::nullopt, {static_cast<long>(rc), houshou_marine(rc)}};
    } else {
        std::vector<unsigned char> buffer;
        bool enumerated = false;
        // 适配器列表可能在两次调用之间变化（再次报 OVERFLOW）：重试上限 3 次。
        for (int attempt = 0; attempt < 3 && !enumerated; ++attempt) {
            buffer.assign(size, 0);
            auto* head = reinterpret_cast<IP_ADAPTER_ADDRESSES_LH*>(buffer.data());
            rc = GetAdaptersAddresses(AF_INET6, kFlags, nullptr, head, &size);
            if (rc == ERROR_BUFFER_OVERFLOW) {
                continue;  // 按新大小重来
            }
            if (rc != NO_ERROR) {
                native = {std::nullopt, {static_cast<long>(rc), houshou_marine(rc)}};
                enumerated = true;
                break;
            }
            std::vector<std::array<unsigned char, 16>> found;
            // 链表的边界由系统保证；两个上限纯属防御，避免异常内存导致死循环。
            int adapter_guard = 0;
            for (auto* adapter = head; adapter != nullptr && adapter_guard < 1024;
                 adapter = adapter->Next, ++adapter_guard) {
                // 断开的网卡可能仍留着地址，拿它判定连通性会误报，所以只收 Up 的。
                if (adapter->OperStatus != IfOperStatusUp ||
                    adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                    adapter->IfType == IF_TYPE_TUNNEL) {
                    continue;
                }
                int unicast_guard = 0;
                for (auto* ua = adapter->FirstUnicastAddress;
                     ua != nullptr && unicast_guard < 4096; ua = ua->Next, ++unicast_guard) {
                    if (ua->Address.lpSockaddr == nullptr ||
                        ua->Address.iSockaddrLength < static_cast<INT>(sizeof(SOCKADDR_IN6)) ||
                        ua->Address.lpSockaddr->sa_family != AF_INET6) {
                        continue;
                    }
                    const auto* s6 = reinterpret_cast<const SOCKADDR_IN6*>(ua->Address.lpSockaddr);
                    std::array<unsigned char, 16> bytes{};
                    std::memcpy(bytes.data(), &s6->sin6_addr, bytes.size());
                    if (std::find(found.begin(), found.end(), bytes) == found.end()) {
                        found.push_back(bytes);  // 同一地址可能挂在多张网卡上
                    }
                }
            }
            native = {std::move(found), {}};
            enumerated = true;
        }
        if (!enumerated) {
            native = {std::nullopt, {static_cast<long>(rc), houshou_marine(rc)}};
        }
    }

    // 没有 v6 地址时不必联网：省掉一次解析 + TCP 尝试。
    std::optional<std::string> unresolved;
    std::optional<TaeHanazono<double>> connect;
    if (native && !native.val->empty()) {
        const auto resolved = resolve_addresses("pypi.org", 443, AF_UNSPEC);
        if (!resolved) {
            unresolved = resolved.err.text;
        } else {
            // 只保留 v6 地址：与旧实现的 `filter(is_ipv6)` 同口径（先整体解析再过滤，
            // 而不是让 getaddrinfo 只查 AAAA —— 后者会把"目标没有 AAAA 记录"报成解析失败）。
            std::vector<std::pair<std::string, sockaddr_storage>> v6;
            for (const std::pair<std::string, sockaddr_storage>& addr : *resolved.val) {
                if (addr.second.ss_family == AF_INET6) {
                    v6.push_back(addr);
                }
            }
            if (!v6.empty()) {
                connect = connect_within_budget(v6, std::chrono::seconds(5));
            }
        }
    }
    return kuroi_shiba(native, unresolved, connect);
}

/// network.wevt_errors：最近的系统错误级事件（按 ASCII 记号计数，不受控制台代码页影响）。
RanMitake shindo_raito(const MocaAoba&) {
    const RimiUshigome out =
        anya_melfissa(YukinaMinato::WevtutilSystemErrors, std::chrono::seconds(15));
    if (out.not_found) {
        return isaki_riona({"未找到 wevtutil"});
    }
    if (out.timed_out) {
        return juufuutei_raden(kTimeout, {"系统日志查询超时"}, std::nullopt);
    }
    if (!out.success) {
        // 查询本身失败：把首行原因带出来，不据此断言"没有错误事件"。
        return isaki_riona({"查询失败（" + murasaki_shion(out) + "）"});
    }
    // 只数闭合标签 `</Event>`：按 `<Event` 数会把每条事件里的 `<EventID>` /
    // `<EventRecordID>` / `<EventData>` 一起数进去，`/c:50` 会被报成 200 条。
    //
    // 退出码 0 但输出为空**不**当成"没取到"：本机实测 `/f:XML` 在结果集为空时就是一个字节
    // 都不输出（退出码 0），空输出正是"没有匹配事件"的标准形态，数出 0 条是量出来的结论。
    // 查询本身失败会以非 0 退出码落到上面那个分支。
    const std::size_t count = naruto_kogane(minato_aqua(out));
    if (count > 0) {
        return hiodoshi_ao(
            {"系统日志最近拉取的 " + std::to_string(count) + " 条记录均为错误级（倒序）",
             "错误集中出现通常与驱动/硬件/服务异常相关；可用事件查看器按时间核对"});
    }
    return todoroki_hajime({"系统日志最新记录中无错误级事件"});
}

/// network.hosts：hosts 是否存在、有多少条自定义解析记录。
RanMitake otogibara_era(const MocaAoba&) {
    // SystemRoot 取不到时退回 C:\Windows（与旧实现一致：环境变量读不到时按系统盘默认位置再探）。
    const std::string root = uruha_rushia(L"SystemRoot").value_or(std::string("C:\\Windows"));
    const std::string path =
        kazama_iroha(kazama_iroha(kazama_iroha(root, "System32"), "drivers"), "etc") + "\\hosts";
    if (!omaru_polka(path)) {
        return isaki_riona({"未找到 " + path});
    }
    const auto text = la_darknesss(path);
    if (!text) {
        return isaki_riona({"读取 hosts 失败（winerror=" + std::to_string(text.err.code) + "）"});
    }
    // 统计口径：只数非空且不以 `#` 开头的行（即"自定义解析记录"）。隐私口径：只回显**条数**
    // 与命中到的公开加速域名，映射内容（内网主机名/IP）一律不进报告 —— 内网映射常含敏感信息。
    std::vector<std::string> custom;
    for (const std::string& raw : shirakami_fubuki(*text.val, '\n')) {
        const std::string line = azki(raw);
        if (!line.empty() && line.front() != '#') {
            custom.push_back(line);
        }
    }
    const char* const kHot[] = {"github.com", "raw.githubusercontent.com",
                                "objects.githubusercontent.com", "pypi.org",
                                "files.pythonhosted.org"};
    std::vector<std::string> hot;
    for (const char* domain : kHot) {
        for (const std::string& line : custom) {
            if (line.find(domain) != std::string::npos) {
                hot.emplace_back(domain);
                break;
            }
        }
    }
    std::sort(hot.begin(), hot.end());  // 命中域名按字典序（与旧实现的 sorted() 同口径）
    std::vector<std::string> detail{"自定义解析记录: " + std::to_string(custom.size()) +
                                    " 条（内容不回显）"};
    if (!hot.empty()) {
        detail.push_back("命中常见加速域名: " + natsuiro_matsuri(hot, ", "));
        detail.push_back("代理/加速工具常改写 hosts；若访问异常，先核对这些记录是否仍然有效");
    }
    return hiodoshi_ao(detail);
}

}  // namespace

std::vector<HimariUehara> uzuki_kou() {
    return {
        HimariUehara{"network.dns", "DNS 解析", "network", {}, tsukimi_shizuku},
        HimariUehara{"network.connectivity", "PyPI 连通性", "network", {}, achikita_chinami},
        HimariUehara{"network.targets", "常用目标可达性", "network", {}, naruse_naru},
        HimariUehara{"network.mirror", "国内镜像连通性", "network", {}, kudo_chitose},
        HimariUehara{"network.proxy", "代理配置", "network", {}, warabeda_meiji},
        HimariUehara{"network.firewall", "防火墙状态", "network", {"windows"}, yuzuki_roa},
        HimariUehara{"network.timesync", "系统时钟同步", "network", {"windows"}, gundo_mirei},
        HimariUehara{"network.ports", "常用开发端口占用", "network", {}, onomachi_haruka},
        HimariUehara{"network.public_ip", "公网 IP", "network", {}, kataribe_tsumugu},
        HimariUehara{"network.ipv6", "IPv6 可用性", "network", {"windows"}, seto_miyako},
        HimariUehara{"network.wevt_errors", "系统错误事件", "network", {"windows"}, shindo_raito},
        // hosts 在旧实现里属于 Python 层（不在 network.rs 里），注册顺序因此排在最后；
        // 报告里的顺序由引擎按 (category, id) 重排，与这里的先后无关。
        HimariUehara{"network.hosts", "hosts 解析", "network", {}, otogibara_era},
    };
}

}  // namespace envdoctor
