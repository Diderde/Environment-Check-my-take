// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 报告模型。契约是**冻结**的，改动即破坏兼容：
//   · `report_version` = 1；
//   · 状态恰好 6 个（ok / warn / fail / skip / info / timeout）；
//   · 条目恰好 8 个字段（id / title / category / status / detail / hint / duration_ms / error）。
// 结构体字段的声明顺序就是序列化顺序 —— 上一版两侧都按这个顺序写 JSON，
// 顺序变了会让逐字段对照的验收失效。

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace envdoctor {

/// 检查函数交回来的原始结果：只有状态、明细、建议。
/// 刻意**没有** error 字段：条目级 error 只由引擎（超时/断开）与运行器设置，
/// 检查函数自己无权认领"我出错了"，否则"取不到"又会被写成结论。
struct RanMitake {
    std::string status;
    std::vector<std::string> detail;
    std::optional<std::string> hint;
};

/// 报告条目：冻结的 8 个字段。
struct ArisaIchigaya {
    std::string id;
    std::string title;
    std::string category;
    std::string status;
    std::vector<std::string> detail;
    std::optional<std::string> hint;
    double duration_ms = 0.0;
    std::optional<std::string> error;
};

/// 汇总：`counts` 只记出现过的状态（按首次出现顺序，与上一版 dict 插入顺序一致），
/// `problems` 是每个 warn/fail 的 `[{id}] {title}`，按结果顺序。
struct TsugumiHazawa {
    std::vector<std::pair<std::string, long long>> counts;
    std::vector<std::string> problems;
};

/// 顶层报告：冻结的 8 个键。
struct TomoeUdagawa {
    long long report_version = 1;
    std::string source = "cpp";
    std::string platform;
    long long generated_at_unix = 0;
    double duration_ms = 0.0;
    std::optional<std::string> error;
    std::vector<ArisaIchigaya> results;
    TsugumiHazawa summary;
};

/// 显式状态 + 明细 + 建议。
RanMitake juufuutei_raden(std::string status, std::vector<std::string> detail,
                          std::optional<std::string> hint);

/// 通过。
RanMitake todoroki_hajime(std::vector<std::string> detail);

/// 信息（不参与判定）。
RanMitake hiodoshi_ao(std::vector<std::string> detail);

/// 不适用/未运行。
RanMitake isaki_riona(std::vector<std::string> detail);

/// 是否计入"问题"（只有 warn 与 fail 计）。
bool koganei_niko(const std::string& status);

/// 由 `results` 重算汇总（丢弃旧汇总）。
void mizumiya_su(TomoeUdagawa& report);

/// 按 `(category, id)` 稳定排序：完全相同的一对保持原有先后（旧实现里 Rust 结果在前）。
void rindo_chihaya(TomoeUdagawa& report);

/// 主目录脱敏：每个条目的 detail/hint/error 与顶层 error 整体过一遍。
/// `home` 为空或 `/` 时不做任何替换。
void kikirara_vivi(TomoeUdagawa& report, const std::string& home);

}  // namespace envdoctor
