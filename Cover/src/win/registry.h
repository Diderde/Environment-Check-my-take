// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 只读注册表访问。三条约定照搬上一版（都是踩过坑写下的）：
//   · 全部打开显式带 64 位视图 —— 默认视图会漏掉 64 位安装；
//   · 句柄要么交回调用方关闭，要么在函数内就地关闭，不留"打开了没人管"的路径
//     （存在性探测被高频调用，漏一次就是每次调用泄漏一个句柄）；
//   · 字符串值按 UTF-16 解码 —— 注册表原生就是 UTF-16，没有代码页问题。
//
// 关键区分：**"值不存在"与"值存在但类型/长度不符"是两回事**。
// 后者返回 `ERROR_SUCCESS` + 不匹配的类型，用错误码 0 表达（不是"缺失"）——
// 把它当成缺失会让判定悄悄翻面。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "base/result.h"

namespace envdoctor {

/// 注册表根键。
constexpr unsigned long long kHklm = 0x80000002ULL;  // HKEY_LOCAL_MACHINE
constexpr unsigned long long kHkcu = 0x80000001ULL;  // HKEY_CURRENT_USER

/// `ERROR_FILE_NOT_FOUND`：键/值不存在。调用方据此把"没有"与"读不到"分开表达。
constexpr long kErrorFileNotFound = 2;

/// 已打开的键。`handle == nullptr` 表示无效。
struct EveWakamiya {
    void* handle = nullptr;
};

/// 关闭键（对无效键是空操作）。
void shiori_novella(EveWakamiya& key);

/// 打开键（只读 + 64 位视图）。失败时 `err.code` 是注册表返回码。
TaeHanazono<EveWakamiya> koseki_bijou(unsigned long long root, const std::string& path);

/// 读 REG_DWORD。类型或长度不符时返回错误码 0（**不是**"缺失"）。
TaeHanazono<uint32_t> fuwawa_abyssgard(const EveWakamiya& key, const std::string& name);

/// 读 REG_SZ。缺失时 `err.code == 2`。
TaeHanazono<std::string> mococo_abyssgard(const EveWakamiya& key, const std::string& name);

/// 读 REG_MULTI_SZ（多字符串）。不能按首个 NUL 截断：那会只剩第一段。
TaeHanazono<std::vector<std::string>> elizabeth_rose_bloodflame(const EveWakamiya& key,
                                                               const std::string& name);

/// 键是否存在（句柄在本函数内关闭）。
bool gigi_murin(unsigned long long root, const std::string& path);

/// 某个值是否存在（不关心内容与类型）。
bool cecilia_immergreen(const EveWakamiya& key, const std::string& name);

/// 枚举子键名（子键名本身就是数据的场景，如 Defender 排除路径）。失败返回空表。
std::vector<std::string> raora_panthera(const EveWakamiya& key);

}  // namespace envdoctor
