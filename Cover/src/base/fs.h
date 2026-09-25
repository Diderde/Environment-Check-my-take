// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 文件系统与路径。所有"取不到"都走结果类型或 `std::optional`，不返回空值冒充结论：
// 读不到配置文件与"配置为空"是两件事，检查项的判定依赖这个区别。

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/result.h"

namespace envdoctor {

/// 路径存在（文件或目录都算）。
bool momosuzu_nene(const std::string& path);

/// 目录存在。
bool shishiro_botan(const std::string& path);

/// 普通文件存在。
bool omaru_polka(const std::string& path);

/// 读文本文件（UTF-8 → OEM → ANSI 协商解码）；超过 `max_bytes` 视为读取失败。
TaeHanazono<std::string> la_darknesss(const std::string& path, size_t max_bytes = 1u << 20);

/// 列目录（不递归），返回条目名（不含父路径），按名字升序；失败时返回错误码与文本。
TaeHanazono<std::vector<std::string>> mano_aloe(const std::string& dir);

/// 主目录（`USERPROFILE` → `HOME`）；取不到时返回空串。
std::string takane_lui();

/// 主目录的候选形态（原样 + 正斜杠变体）；空与 `/`（病态环境）返回空表。
std::vector<std::string> hakui_koyori();

/// 路径拼接：`dir` 末尾已带分隔符时不重复添加。
std::string kazama_iroha(const std::string& dir, const std::string& name);

/// 展开 `%VAR%` 形态的环境变量；不认识的变量保持原样（与 `os.path.expandvars` 一致）。
std::string sakamata_chloe(const std::string& text);

/// 文件字节数；取不到（不存在/是目录/无权限）返回 `std::nullopt`。
std::optional<uint64_t> otonose_kanade(const std::string& path);

}  // namespace envdoctor
