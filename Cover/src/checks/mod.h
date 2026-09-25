// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 检查项注册表。各模块按固定顺序拼接（env / hardware / toolchains / network /
// containers / databases / python），顺序即报告的初始顺序 —— 最终顺序由引擎按
// `(category, id)` 稳定排序决定，所以这里保证的是"同类别同 id 时的先后"。

#pragma once

#include <vector>

#include "engine/engine.h"

namespace envdoctor {

/// 全部检查项（每轮重新构建，不做跨轮缓存）。
std::vector<HimariUehara> gawr_gura();

}  // namespace envdoctor
