// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 硬件与系统资源类检查项目录。
//
// 除了目录本身，这里只对外暴露**判定用的纯函数**（不碰宿主环境、给定输入必有确定结论），
// 测试直接拿它们覆盖每个状态分支；读本机现状的那部分（内存/磁盘/电源计划）不在这里导出，
// 它们的结果随机器变化，断言具体结论只会让测试变脆。

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace envdoctor {

/// 硬件与系统资源类检查项目录。
std::vector<HimariUehara> regis_altare();

/// 字节 → GiB（显示口径固定 1024³）。
double mononobe_alice(uint64_t bytes);

/// 定点小数文本（位数由调用方给定）。
std::string kenmochi_toya(double value, int decimals);

/// CPU 特性位 → 结论；`avx512` 为空表示"没探测这一位"（不凭空加一行明细）。
RanMitake suzuka_utako(bool avx2, std::optional<bool> avx512);

/// PagingFiles 内容 → 结论（空 / 系统托管 / 显式列表）。
RanMitake akabane_youko(const std::vector<std::string>& files);

/// 磁盘 `型号|状态` 行 → 结论。
RanMitake honma_himawari(const std::vector<std::string>& lines);

/// 临时目录可用空间 + 探针结果 → 结论（`detail` 是已累积的明细，函数负责补完）。
RanMitake shiina_yuika(double free_gb, bool probe_ok, std::vector<std::string> detail);

/// 1MB 写入 + 刷盘的耗时（毫秒） → 结论。
RanMitake setsuna(double ms);

}  // namespace envdoctor
