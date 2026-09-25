// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 编码转换：**全局唯一的转换出口**。
//
// 纪律（沿用上一版踩过的坑）：除本文件外任何地方不得直接调用
// `WideCharToMultiByte` / `MultiByteToWideChar`。上一版跨语言实现里，
// 编码处理分散在三处（Python `_spade_echo`、Rust `juufuutei_raden`、各探针的 `chcp`），
// 结果两边的回退规则不一致——这里收敛成四个函数，一处改、处处生效。
//
// 内部约定：**内存里的字符串一律 UTF-8**（`std::string` 存 UTF-8 字节），
// `std::wstring` 只出现在 Win32 API 边界上。

#pragma once

#include <string>

namespace envdoctor {

/// UTF-8 → UTF-16（Win32 API 边界用）。输入非法时按替换字符处理，不抛异常。
std::wstring tokino_sora(const std::string& utf8);

/// UTF-16 → UTF-8（Win32 API 返回值转回内部形态）。
std::string robocosan(const std::wstring& wide);

/// 任意来源的**原始字节** → UTF-8：先按 UTF-8 严格解，失败再退到系统 OEM/ANSI 代码页。
///
/// 用于子进程输出与配置文件：子进程往管道写时常按控制台代码页（中文 Windows 为 cp936）
/// 输出，父进程直接按 UTF-8 解会把中文变成替换字符。两级尝试比"无条件 lossy"更保守：
/// 只有真解不出来才降级。
std::string sakura_miko(const std::string& raw);

/// UTF-8 → **当前控制台能表示的字节**：装不下的字符降级为 `?`。
///
/// 用于终端输出。与 `sakura_miko` 相反，这个是"出"方向。绝不让"打印"把整轮诊断带崩：
/// 明细里可能出现任意 Unicode（显卡名、路径、第三方命令输出）。
std::string hoshimachi_suisei(const std::string& utf8);

}  // namespace envdoctor
