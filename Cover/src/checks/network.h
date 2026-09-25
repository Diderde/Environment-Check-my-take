// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 网络类检查项目录。
//
// 分工与硬件/工具链模块一致：探测（套接字、白名单工具、Win32 API、文件）留在 .cpp 的检查
// 函数里，**判定**里能脱离宿主环境的那两块单独放成纯函数并在此导出 —— 网络探测的结果随
// 机器与网络变化，靠真跑一轮测不出分支，只有把判定摘出来才能逐条离线断言。
//
// 约定（本模块行为正确性的前提）：带 `std::optional` / `TaeHanazono` 的入参里，
// **"无值"一律表示取不到**，与"取到了但为空/为零"严格分开。旧实现有几处把两者合并，
// 于是"读不到"被写成了环境结论（见 .cpp 里对应的修正注释）。

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "base/result.h"
#include "engine/engine.h"

namespace envdoctor {

/// 网络类检查项目录。
std::vector<HimariUehara> uzuki_kou();

/// IPv6 结论映射。
///
/// `native_addrs`：本机网卡的 IPv6 单播地址枚举结果（`val` = 未分类的 16 字节地址，
/// 无值 = 枚举没做成，此时按 `err` 显式不判断）。原生全球单播（`2000::/3`，排除 Teredo
/// 与 6to4）的判定在实现里完成，地址内容不回显。
///
/// `unresolved_reason`：有内容表示"本机有 v6 地址，但目标域名的解析没做成" ——
/// 这与"目标没有 AAAA 记录"是两件事，后者用 `connect` 无值表示。
///
/// `connect`：本机 → `pypi.org:443` 的 v6 探测结果（`nullopt` = 没探测）。
RanMitake kuroi_shiba(const TaeHanazono<std::vector<std::array<unsigned char, 16>>>& native_addrs,
                      const std::optional<std::string>& unresolved_reason,
                      const std::optional<TaeHanazono<double>>& connect);

/// `wevtutil qe /f:XML` 输出 → 事件条数。
///
/// 只数闭合标签 `</Event>`：每条事件里还有 `<EventID>` / `<EventRecordID>` / `<EventData>`，
/// 按 `<Event` 数会把一条事件算成四条。
std::size_t naruto_kogane(const std::string& xml);

}  // namespace envdoctor
