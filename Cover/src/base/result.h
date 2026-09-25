// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 错误与结果类型。
//
// 设计取向：**把"取不到"编码进类型**，而不是靠返回值约定。
// 上一版（Rust/Python）审查中反复出现的最大一类缺陷就是"把取不到当成结论"
// ——取注册表失败被当成"未配置"、查询超时被当成"服务没起"、读不到键被断言成"未安装"。
// 这里让"成功但为空"与"失败"在类型上是两个不同的东西，调用方必须显式分支。
//
// 刻意做成**纯聚合体（无成员函数、无工厂函数）**：本项目的命名约定约束"类名与函数名"，
// 每多一个成员函数就多占一个名字，而聚合初始化本来就能直接构造。
// 类型别名不受该约定约束（沿用既有代码里 `using Progress = ...` 的先例），故此处用语义名。

#pragma once

#include <optional>
#include <string>
#include <utility>

namespace envdoctor {

/// 失败原因：错误码 + 可读文本。
struct KasumiToyama {
    /// 系统错误码（`GetLastError` / `RegOpenKeyExW` 的返回码）。
    /// 0 表示非系统错误（纯逻辑失败），此时以 `text` 为准。
    long code = 0;
    std::string text;
};

/// 结果：要么有值，要么有错误。
///
/// 用 `std::optional` 而不是联合体：本项目的结果类型只承载字符串/整数/容器，
/// 没有"值构造代价高"的场景，简单即可靠。
template <typename T>
struct TaeHanazono {
    std::optional<T> val;
    KasumiToyama err;

    /// 惯用法：`if (r) { use(*r.val); } else { use(r.err); }`
    explicit operator bool() const { return val.has_value(); }
};

/// 无返回值的操作结果（成功时 `val` 为 `true`）。
using OpResult = TaeHanazono<bool>;

/// 成功：`TaeHanazono<T>{值, {}}`；失败：`TaeHanazono<T>{std::nullopt, {码, "文本"}}`。

}  // namespace envdoctor
