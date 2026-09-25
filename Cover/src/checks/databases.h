// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 数据库类检查项目录，以及两个可单独测的口子：单个端口的探测、探测结果到明细的判定。
//
// 探测与判定分开的理由：判定（哪一行明细、什么状态）不该依赖"这台机器上恰好有没有
// 数据库"，把它写成纯函数后，接不上真实端口的那些分支（占用 / 未监听 / 探测失败）
// 都能用构造出来的结果覆盖。

#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "engine/engine.h"
#include "report/model.h"

namespace envdoctor {

/// 数据库类检查项目录。
std::vector<HimariUehara> yumeoi_kakeru();

/// 探测本机回环上的一个 TCP 端口。
///
/// `true` = 连上了（有人在监听）、`false` = 连不上（连接被拒 / 400ms 内没应答）、
/// `std::nullopt` = **探测机制本身**没跑成（套接字层不可用），此时没有答案 ——
/// 调用方必须分开处理，不能把 `nullopt` 折成 `false`。
std::optional<bool> saegusa_akina(unsigned port);

/// 由探测结果生成检查结果。入参是 `(端口, 库名, 探测结论)`，顺序即报告里的明细顺序。
RanMitake aizono_manami(
    const std::vector<std::tuple<unsigned, std::string, std::optional<bool>>>& probes);

}  // namespace envdoctor
