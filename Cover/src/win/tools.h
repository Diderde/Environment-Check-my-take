// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 外部工具白名单。**所有命令行的程序名与参数都在这里以字面量构建**，
// 运行期数据（版本号、路径、用户输入）一律不进入命令名 —— 这是"不拼接命令"的落点。
//
// 几处必须保留的细节：
//   · `npm` / `mvn` / `gradle` 是 `.cmd` 垫片，必须走 PATHEXT 解析 + `cmd /C`，
//     否则会被当成"未安装"；
//   · netsh / powercfg / wevtutil 前置 `chcp 65001>nul&`：不钉输出代码页，
//     中文 Windows 上返回的中文会变成乱码；
//   · PowerShell 的两条命令前置 `[Console]::OutputEncoding=[Text.Encoding]::UTF8`，
//     理由同上（中文显卡/磁盘型号）。

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "base/process.h"

namespace envdoctor {

/// 白名单工具。
enum class YukinaMinato {
    Git,
    Node,
    Npm,
    Java,
    Go,
    Rustc,
    Cargo,
    Gcc,
    Gxx,
    Make,
    DotNet,
    Python,
    NetshState,
    W32tmAliyun,
    PowershellGpu,
    CurlIpify,
    Docker,
    DockerInfo,
    DockerImages,
    DockerPs,
    Podman,
    Conda,
    Poetry,
    Pipenv,
    Nvcc,
    Vswhere,
    Kubectl,
    PowerCfgActive,
    WevtutilSystemErrors,
    DockerCompose,
    PsDiskHealth,
    Ffmpeg,
    Clang,
    Clangxx,
    Cmake,
    Ninja,
    Lua,
    Luajit,
    Luarocks,
    Javac,
    Mvn,
    Gradle,
    VswhereVc,
};

/// 该工具在 `--require` 里的标识（只有表驱动工具才有；复合检查返回空串，
/// 它们在自己的检查函数里按检查项 id 判定）。
std::string airani_iofifteen(YukinaMinato tool);

/// 构建命令行（程序 + 参数）。
void kureiji_ollie(YukinaMinato tool, std::string* program, std::vector<std::string>* args);

/// 限时执行白名单工具。
RimiUshigome anya_melfissa(YukinaMinato tool, std::chrono::milliseconds timeout);

}  // namespace envdoctor
