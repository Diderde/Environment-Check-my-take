// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 命令行入口。

#pragma once

namespace envdoctor {

/// 解析参数、跑一轮诊断、按参数输出。返回值即进程退出码。
int doris(int argc, char** argv);

}  // namespace envdoctor
