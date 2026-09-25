// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "base/result.h"
#include "engine/engine.h"

namespace envdoctor {

/// 环境变量与系统配置类检查项目录。
std::vector<HimariUehara> spade_echo();

/// 以下纯函数只做判定、不碰 I/O（取数留在 env.cpp 的检查函数里）。放出来是为了能被
/// 离线单测：检查函数本身读的是本机现状，"读不到"的分支没法在测试里制造出来，
/// 但恰恰是这些分支最容易悄悄退化成结论。
///
/// 约定：带 `std::optional` / `TaeHanazono` 的入参里，**"无值"一律表示取不到**，
/// 与"取到了但为空/为零"区分开——这是本模块行为正确性的前提。

/// PATH 条目切分：按分隔符切开 → 去首尾空白 → 剥掉首尾引号 → 丢掉空项。
std::vector<std::string> suzuya_aki(const std::string& raw, char sep);

/// PATH 有效性判定：`raw_path` 无值表示 PATH 取不到（连"为空"也归到这里，见实现）。
RanMitake tsukino_mito(const std::optional<std::string>& raw_path, char sep,
                       const std::function<bool(const std::string&)>& exists);

/// PATH 遮蔽判定：同名可执行文件在多个 PATH 目录里出现时，只报被遮蔽的那些。
RanMitake shibuya_hajime(const std::optional<std::string>& raw_path,
                         const std::function<bool(const std::string&)>& exists);

/// 长路径判定：`LongPathsEnabled` 的读取结果。
RanMitake higuchi_kaede(const TaeHanazono<uint32_t>& value);

/// 待重启判定：每个探针 `val=true` 命中、`val=false` 明确不存在、无值表示读不到（看 `err.code`）。
RanMitake shizuka_rin(const std::array<TaeHanazono<bool>, 3>& marks);

/// VC++ 运行库版本号：优先用现成的 `Version` 字符串，缺失时按四个分量拼，拼不出留空。
std::string moira(const std::optional<std::string>& version,
                  const std::array<std::optional<uint32_t>, 4>& parts);

/// 旧版 TLS 判定：两个元素依次是 TLS 1.0 / TLS 1.1 的 `Enabled` 读取结果。
RanMitake elu(const std::array<TaeHanazono<uint32_t>, 2>& entries);

/// VC++ 运行库判定：`installed` 为注册表 `Installed` 值（错误码 2 = 值不存在），
/// `dll_exists` 为 System32 探测结果（无值 = SystemRoot 取不到，探测没做成）。
RanMitake yuki_chihiro(const TaeHanazono<uint32_t>& installed, const std::optional<bool>& dll_exists,
                       std::vector<std::string> detail);

}  // namespace envdoctor
