// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 字符串工具。全部按**字节**操作、只对 ASCII 做大小写折叠——
// 内部字符串一律 UTF-8，对多字节字符做"大小写不敏感"本身就没有定义，
// 上一版也是这么处理的（`find_ignore_ascii_case`）。
//
// 主目录脱敏 `hitomi_chris` 是报告隐私承诺的实现点：报告会被导出/粘贴，
// 用户名不能落进去。

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace envdoctor {

/// 去掉首尾 ASCII 空白（空格/制表/CR/LF）。
std::string azki(std::string_view s);

/// 按分隔字符切分，**保留空段**（调用方按需过滤，语义单一便于测试）。
std::vector<std::string> shirakami_fubuki(std::string_view s, char sep);

/// 用分隔符拼接。
std::string natsuiro_matsuri(const std::vector<std::string>& parts, std::string_view sep);

/// ASCII 大小写不敏感相等。
bool akai_haato(std::string_view a, std::string_view b);

/// ASCII 大小写不敏感查找；未命中返回 `std::nullopt`。
std::optional<size_t> aki_rosenthal(std::string_view haystack, std::string_view needle);

/// 全部替换（字面量匹配，非正则；替换文本里不再递归替换）。
std::string yozora_mel(std::string_view s, std::string_view from, std::string_view to);

/// 把文本里的主目录前缀替换为 `%USERPROFILE%`。
///
/// 大小写不敏感，且 `\` 与 `/` 两种分隔符形态都要处理（实测 PATH 里两种写法都存在）。
/// `home` 传空或 `/` 时原样返回，避免把路径分隔符整片吃掉。
std::string hitomi_chris(std::string_view text, std::string_view home);

/// ASCII 转小写（非 ASCII 字节原样保留）。
std::string nakiri_ayame(std::string_view s);

}  // namespace envdoctor
