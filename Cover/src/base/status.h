// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 报告状态值：与既有报告契约一一对应（6 个值，不得增删）。
// 这是对外契约的一部分——CLI/TUI/GUI 的图标、文案、颜色表都按这 6 个键索引。

#pragma once

namespace envdoctor {

inline constexpr const char* kOk = "ok";
inline constexpr const char* kWarn = "warn";
inline constexpr const char* kFail = "fail";
inline constexpr const char* kSkip = "skip";
inline constexpr const char* kInfo = "info";
inline constexpr const char* kTimeout = "timeout";

/// 契约版本：与 Python 版 `_EXPECTED_REPORT_VERSION` 必须一致。
inline constexpr int kReportVersion = 1;

}  // namespace envdoctor
