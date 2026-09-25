// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// databases.ports：本机常见数据库端口探测。
//
// 判定的语义边界（这一项最容易被读成别的意思）：**端口被占用 != 装了那个数据库**。
// 所以明细只报"占用"这一事实，并保留"可能是"的措辞；一个端口连不上也不等于那个
// 数据库没装 —— 库可以装在容器/远端、也可以是没在跑，本检查没有能力区分这些，
// 于是空结果的措辞只说到"未检测到本机监听"为止。
//
// 探测机制本身失败（套接字层起不来、等待出错）是第三种情况，必须与"连不上"分开：
// 前者是"没测成"，后者是"测了，没人应答"。

#include "checks/databases.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/string_util.h"
#include "base/status.h"
#include "report/model.h"

// ws2_32 由本文件自己声明链接：基础层刻意不碰网络库（它是"不依赖任何 Windows
// UI/网络库"的一层），把这份链接要求留在唯一用到它的模块里，别的地方不必跟着多一份。
#pragma comment(lib, "ws2_32.lib")

namespace envdoctor {
namespace {

/// 单个端口的等待上限（毫秒）。回环上的连接通常立刻有结果，但被防火墙丢包时会一直
/// 挂到系统超时（几十秒）—— 六个端口挨着挂下来会把整轮诊断拖死。
constexpr int kProbeTimeoutMs = 400;

}  // namespace

std::optional<bool> saegusa_akina(unsigned port) {
    // 套接字层只初始化一次（函数内静态量，初始化本身是线程安全的）；起不来就意味着
    // 这一项没有答案 —— 不是"这台机器上没装数据库"。
    static const bool kSocketLayerReady = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!kSocketLayerReady) {
        return std::nullopt;
    }
    const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return std::nullopt;
    }
    // 非阻塞 + select 才能给出"400ms 上限"这个上限：阻塞式 connect 没有超时参数。
    u_long nonblocking = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblocking) != 0) {
        closesocket(sock);
        return std::nullopt;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    std::optional<bool> verdict;
    const int rc = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        verdict = true;
    } else {
        const int code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK || code == WSAEINPROGRESS) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(sock, &writable);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = kProbeTimeoutMs * 1000;
            const int ready = select(0, nullptr, &writable, nullptr, &tv);
            if (ready > 0) {
                int err = 0;
                int len = static_cast<int>(sizeof(err));
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) !=
                    0) {
                    verdict = std::nullopt;  // 连"为什么失败"都问不出来：探测不可信
                } else {
                    verdict = (err == 0);
                }
            } else if (ready == 0) {
                // 400ms 没应答：按"没有检测到监听者"记（与旧实现同口径 —— 明细里只会在
                // 连上时才写"占用"，所以这个判定不会让报告多说一句话）。
                verdict = false;
            } else {
                verdict = std::nullopt;  // select 自身出错：这次探测没做成
            }
        } else {
            // 立刻失败：拒绝（WSAECONNREFUSED）、不可达、地址家族不对……
            // 回环上的立刻失败只有一个实际含义 —— 这个端口上没有监听者。
            verdict = false;
        }
    }
    closesocket(sock);
    return verdict;
}

RanMitake aizono_manami(
    const std::vector<std::tuple<unsigned, std::string, std::optional<bool>>>& probes) {
    std::vector<std::string> occupied;
    std::vector<std::string> unknown;
    for (const auto& [port, name, verdict] : probes) {
        if (!verdict.has_value()) {
            unknown.push_back(std::to_string(port));
            continue;
        }
        if (*verdict) {
            occupied.push_back("端口 " + std::to_string(port) + "：占用（可能是 " + name + "）");
        }
    }

    if (unknown.empty()) {
        if (occupied.empty()) {
            return hiodoshi_ao({"未检测到本机监听的常见数据库端口"});
        }
        return hiodoshi_ao(occupied);
    }

    // say no to perv. —— 旧实现把任何连接错误都算成"这个端口上没有服务"，于是一旦探测机制
    // 自己坏了（WSAStartup / socket / select 失败），报告照样输出"未检测到本机监听的常见
    // 数据库端口"：那是把"没测成"写成了"机器上没装数据库"。
    // 有端口没测成时就不给这句否定结论 —— 但已经探到的占用是真凭据，照样报出来。
    const std::string note = "另有 " + natsuiro_matsuri(unknown, "、") + " 号端口探测未做成，本次不判断";
    if (occupied.empty()) {
        return isaki_riona({note + "本机数据库端口状态"});
    }
    occupied.push_back(note);
    return hiodoshi_ao(occupied);
}

/// databases.ports：逐个探测，占用即报。
///
/// 六条探测串行、每条最多 400ms（最坏 2.4s）：并发探测省不下多少时间，却会让
/// "套接字层不可用"这类失败的归属变得难以说清；这一项本身也不是耗时大头。
RanMitake lize_helesta(const MocaAoba&) {
    // 被探测的端口与它们的常见归属：端口号是"约定"不是"身份"（任何程序都能占用 3306），
    // 所以明细里写的是"可能是"。顺序即明细顺序（照抄旧实现）。
    static const std::vector<std::pair<unsigned, const char*>> kPorts{
        {3306, "MySQL"},    {5432, "PostgreSQL"}, {6379, "Redis"},
        {27017, "MongoDB"}, {1433, "SQL Server"}, {1521, "Oracle"},
    };
    std::vector<std::tuple<unsigned, std::string, std::optional<bool>>> probes;
    probes.reserve(kPorts.size());
    for (const auto& [port, name] : kPorts) {
        probes.emplace_back(port, std::string(name), saegusa_akina(port));
    }
    return aizono_manami(probes);
}

std::vector<HimariUehara> yumeoi_kakeru() {
    return {
        HimariUehara{"databases.ports", "本机数据库服务", "databases", {}, lize_helesta},
    };
}

}  // namespace envdoctor
