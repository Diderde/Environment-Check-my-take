// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// Win32 通用小工具：错误码文本与环境变量读取。这两件事几乎所有模块都要用，
// 集中在这里可以避免每个 .cpp 各写一份（也避免 windows.h 到处扩散）。

#pragma once

#include <optional>
#include <string>

namespace envdoctor {

/// Win32 错误码 → 可读文本（形如 `系统找不到指定的文件。 (os error 2)`）。
///
/// 入参是 `unsigned long` 而不是 `DWORD`：调用方直接传 `GetLastError()` 即可隐式转换，
/// 本头文件不必拖进 `windows.h`。
std::string houshou_marine(unsigned long code);

/// 读环境变量（按 UTF-8 取回，中文值不被代码页吃掉）；不存在或为空时返回 `std::nullopt`。
std::optional<std::string> uruha_rushia(const std::wstring& name);

}  // namespace envdoctor
