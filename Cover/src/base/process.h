// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 外部命令的限时执行。所有 program / args 都只允许来自编译期字面量（白名单表），
// 不存在运行时输入进入命令行的通路。
//
// 三件事在这里一次性做对，后面每个检查都不必重复处理：
//   ① 按 PATHEXT 解析可执行文件 —— CreateProcess 不读 PATHEXT，而 Node 官方包给的是
//      `npm.cmd`（没有 npm.exe），不解析就会把"已安装"误报成"未安装"；
//   ② 超时后杀掉**整棵进程树** —— 只杀直接子进程时，孙进程继承着管道写端不放，
//      排空会挂到天荒地老；
//   ③ 捕获的输出按 UTF-8 → OEM → ANSI 协商解码 —— 中文 Windows 上子进程常按 cp936
//      写管道，直接当 UTF-8 解会把中文变成替换字符。

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace envdoctor {

/// 一次限时执行的结果。`not_found` 单独留一个字段：上层靠它判定"未安装"，
/// 不能从"退出码非 0"里推断（那会把"装了但版本输出解析失败"混进来）。
struct RimiUshigome {
    bool success = false;
    bool timed_out = false;
    bool not_found = false;
    std::string out;
    std::string err;
};

/// 组合输出：stdout 去空白后非空就用它，否则退回 stderr（javac `-version` 这类打到 stderr）。
std::string minato_aqua(const RimiUshigome& r);

/// 组合输出的第一行（版本号通常在首行）。
std::string murasaki_shion(const RimiUshigome& r);

/// 按 PATHEXT 在 PATH 中解析可执行文件；解析不到返回 `std::nullopt`。
///
/// 带路径分隔符的入参按"原样路径"处理（vswhere 就是绝对路径调用的）。
std::optional<std::string> ookami_mio(const std::string& program);

/// 限时执行并回收输出。超时杀掉整棵进程树，`timed_out` 置位。
///
/// 解析不到程序时**退回裸名**去启动，让失败保持"找不到文件"这一形态 —— 上层据此报
/// "未安装"；若改用 `cmd /C` 兜底，cmd 会返回退出码 1 + "不是内部或外部命令"，
/// 被误判成"已安装但版本解析失败"。
///
/// `cwd` 为空表示继承当前目录。需要"在某个目录里跑"的命令（如仓库内的只读 `git status`）
/// 走这个通道传路径，**不要**把路径拼进参数 —— 参数一律来自编译期字面量。
RimiUshigome nekomata_okayu(const std::string& program, const std::vector<std::string>& args,
                            std::chrono::milliseconds timeout, const std::string& cwd = {});

}  // namespace envdoctor
