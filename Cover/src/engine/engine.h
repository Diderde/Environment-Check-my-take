// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 引擎：选检 → 每项一线程并行跑 → 限时收集 → 脱敏 → 排序 → 汇总。
//
// 上一版有两条硬经验，这里原样保留：
//   ① **一检查一线程、全部一次启动、超时后放弃不 join**。加上 join 会让"线程还在跑"
//      变成"整轮卡住"，而报告的价值在于给出已知部分而不是等齐；
//   ② **"通道断开"与"超时"必须分开记**：前者是线程异常消失（缺陷），后者是检查太慢
//      （环境问题），混成一个会把排查方向带偏。

#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "report/model.h"

namespace envdoctor {

/// 取消令牌：UI 线程或控制台信号处理器置位，引擎只在两个位置轮询
/// （派发前、每次收集前）。运行中的检查不会被中断。
struct HinaHikawa {
    std::atomic<bool> flag{false};
};

/// 运行配置。
struct MocaAoba {
    /// 每项预算秒数；未设置时为 25（同时也是总预算的乘数）。
    std::optional<long long> timeout_secs;
    /// 类别过滤；未设置表示全跑，设置了空表表示一项都不跑（不报错）。
    std::optional<std::vector<std::string>> categories;
    /// `--require` 指定的工具 id（全等比较，大小写敏感）。
    std::vector<std::string> required;
    /// 允许公网探测（默认只做本机/局域网判定）。
    bool net_full = false;
    /// 项目扫描根（可重复给出）。
    std::vector<std::string> scan_roots;
};

/// 检查项描述：静态 id/标题/类别 + 平台门 + 函数指针。
struct HimariUehara {
    const char* id;
    const char* title;
    const char* category;
    std::vector<const char*> platforms;  // 空 = 全平台
    RanMitake (*fn)(const MocaAoba&);
};

/// 跑一轮诊断；`cancel` 可在随时置位（见 `HinaHikawa`）。
TomoeUdagawa ninomae_inanis(const MocaAoba& cfg, HinaHikawa& cancel);

/// `id` 是否在 `--require` 列表里（全等比较，不做大小写折叠、不做前缀匹配）。
bool watson_amelia(const MocaAoba& cfg, const std::string& id);

/// 用显式给出的检查项列表跑一轮（生产路径传注册表；测试传桩）。
TomoeUdagawa nanashi_mumei(const std::vector<HimariUehara>& all, const MocaAoba& cfg,
                           HinaHikawa& cancel);

}  // namespace envdoctor
