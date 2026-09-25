// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 报告 → JSON 文本。格式对齐上一版的导出结果：
// UTF-8 原文（不转 `\uXXXX`）、2 空格缩进、键序固定、末尾不带换行。
// 导出文件会被外部工具读，所以这里是"契约"的一部分，不是显示样式。

#pragma once

#include <string>

#include "report/model.h"

namespace envdoctor {

/// 序列化整份报告（含 `results` 与 `summary`）。
std::string achichi_mela(const TomoeUdagawa& report);

}  // namespace envdoctor
