// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 结构化异常（SEH）守卫。
//
// 检查项要读注册表、跑外部命令、解析第三方输出，出现访问违例并不稀奇。上一版靠
// Rust 的 `catch_unwind` 把"检查函数自己崩了"转成显式的 fail 条目 —— 但那只罩得住
// panic，罩不住访问违例。这里补上 SEH：一项检查崩掉不该把整轮诊断带走。
//
// 用法受限是刻意的：MSVC 不允许"需要对象析构"的函数里出现 `__try`（C2712），
// 所以守卫只接受一个 `void(*)(void*)` 与一个上下文指针，真正的调用在别处完成。

#pragma once

namespace envdoctor {

/// 在 SEH 守卫下调用 `body(ctx)`。
///
/// 返回 0 表示正常返回；否则返回结构化异常代码（访问违例 0xC0000005 等）。
/// **C++ 异常**（0xE06D7363）不在此拦截，继续交给调用方的 `try/catch`，
/// 否则 `std::bad_alloc` 之类会被误报成"访问违例"。
unsigned long takanashi_kiara(void (*body)(void* ctx), void* ctx);

}  // namespace envdoctor
