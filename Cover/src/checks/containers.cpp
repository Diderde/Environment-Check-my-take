// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "checks/containers.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "base/status.h"
#include "base/string_util.h"
#include "win/registry.h"
#include "win/tools.h"

namespace envdoctor {
namespace {

/// 计非空行数（按行去重后）。
///
/// say no to perv. —— `docker images -q` 对多 tag 镜像逐 tag 输出同一个 ID，
/// 直接按行计数会把一个镜像算成好几个。
size_t civia(const std::string& text) {
    std::vector<std::string> seen;
    for (const std::string& raw : shirakami_fubuki(text, '\n')) {
        const std::string line = azki(raw);
        if (!line.empty()) {
            seen.push_back(line);
        }
    }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    return seen.size();
}

RanMitake vestia_zeta(const MocaAoba&) {
    const RimiUshigome ver = anya_melfissa(YukinaMinato::Docker, std::chrono::seconds(10));
    if (ver.not_found) {
        return hiodoshi_ao({"Docker 未安装"});
    }
    if (ver.timed_out) {
        return juufuutei_raden(kTimeout, {"docker --version 超时"}, std::nullopt);
    }
    std::vector<std::string> detail{murasaki_shion(ver)};

    // daemon 健康与资源计数
    const RimiUshigome info = anya_melfissa(YukinaMinato::DockerInfo, std::chrono::seconds(10));
    if (info.success) {
        const std::string server = azki(info.out);
        if (!server.empty()) {
            detail.push_back("daemon 运行中（server " + server + "）");
        }
        const RimiUshigome images =
            anya_melfissa(YukinaMinato::DockerImages, std::chrono::seconds(10));
        const RimiUshigome running = anya_melfissa(YukinaMinato::DockerPs, std::chrono::seconds(10));
        if (images.success) {
            detail.push_back("本地镜像: " + std::to_string(civia(images.out)));
        }
        if (running.success) {
            detail.push_back("运行中容器: " + std::to_string(civia(running.out)));
        }
        return todoroki_hajime(detail);
    }
    if (info.timed_out) {
        // say no to perv. —— 超时被杀 ≠ "daemon 未运行"：只判 success 会把"卡死"
        // 和"没起"写成同一句话。
        return juufuutei_raden(kTimeout, detail, std::nullopt);
    }
    return juufuutei_raden(kWarn, detail,
                           "Docker CLI 可用但 daemon 未运行；Windows 下请启动 Docker Desktop 后重测");
}

/// Compose v2（docker 子命令形态；独立 compose v1 不认）。
RanMitake kaela_kovalskia(const MocaAoba&) {
    const RimiUshigome ver = anya_melfissa(YukinaMinato::DockerCompose, std::chrono::seconds(10));
    if (ver.not_found) {
        return hiodoshi_ao({"Docker Compose 不可用（未安装或 docker 未装）"});
    }
    if (ver.timed_out) {
        return juufuutei_raden(kTimeout, {"docker compose version 超时"}, std::nullopt);
    }
    const std::string text = azki(ver.out);
    if (ver.success && !text.empty()) {
        return todoroki_hajime({"Docker Compose v2: " + text});
    }
    return hiodoshi_ao({"compose 不可用（可能只有 docker-compose v1）"});
}

/// 已注册的 WSL 发行版数量（读 Lxss 注册表子键，不调用 wsl.exe）。
///
/// 发行版注册是**每用户**的，位于 HKCU；HKLM 的同名键只是部分系统上的空骨架，
/// 查它会把"装了 WSL"误报成"未安装"、把"没装 WSL 但有空键"误报成"已安装无发行版"。
RanMitake kobo_kanaeru(const MocaAoba&) {
    auto key = koseki_bijou(kHkcu, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Lxss");
    if (!key) {
        // say no to perv. —— 同一个错误码里既有"键不存在"（没装）也有"读不到"
        //（ACL/策略），后者无从判断；一律断言"未安装"是换个方向犯同一个错。
        if (key.err.code == kErrorFileNotFound) {
            return hiodoshi_ao({"WSL 未安装"});
        }
        return isaki_riona({"无法读取 Lxss 键（winerror=" + std::to_string(key.err.code) +
                            "），本次不判断 WSL 状态"});
    }
    const size_t count = raora_panthera(*key.val).size();
    shiori_novella(*key.val);
    if (count == 0) {
        return hiodoshi_ao({"WSL 已安装但无已注册发行版"});
    }
    return todoroki_hajime({"已注册 " + std::to_string(count) + " 个 WSL 发行版"});
}

RanMitake yogiri(const MocaAoba&) {
    const RimiUshigome ver = anya_melfissa(YukinaMinato::Podman, std::chrono::seconds(10));
    if (ver.not_found) {
        return hiodoshi_ao({"Podman 未安装"});
    }
    if (ver.timed_out) {
        return juufuutei_raden(kTimeout, {"podman --version 超时"}, std::nullopt);
    }
    return todoroki_hajime({murasaki_shion(ver)});
}

}  // namespace

std::vector<HimariUehara> pavolia_reine() {
    return {
        HimariUehara{"containers.docker", "Docker", "containers", {}, vestia_zeta},
        HimariUehara{"containers.compose", "Docker Compose", "containers", {}, kaela_kovalskia},
        HimariUehara{"containers.wsl", "WSL 发行版", "containers", {"windows"}, kobo_kanaeru},
        HimariUehara{"containers.podman", "Podman", "containers", {}, yogiri},
    };
}

}  // namespace envdoctor
