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
    // 以下都是"解释器与包管理"检查用的复合命令：程序与参数**全部是编译期字面量**，
    // 运行期数据（库名、镜像地址）一律不进命令行 —— 库名直接内联成各自独立的一条命令，
    // 镜像地址经固定环境变量交给固定的探测脚本（见 tools.cpp 的说明）。
    // 追加在末尾：已有的枚举值一个都不动、不重排（值被序列化/比较过，重排等于改语义）。
    PythonInfo,
    PythonStartupPing,
    WherePython,
    PyLauncherList,
    PythonPipVersion,
    PythonPipListFreeze,
    PythonPipListOutdated,
    PythonPipConfigList,
    PythonPipCacheDir,
    PythonPipCheck,
    PythonImportScan,
    PythonImportPip,
    PythonImportSetuptools,
    PythonImportWheel,
    PythonImportRequests,
    PythonImportNumpy,
    PythonImportPandas,
    PythonSslProbe,
    PythonUrlProbe,
    // 这两条是 git 的关键配置查询：pattern 是**编译期字面量**（`--get-regexp` 的入参），
    // 因此不需要任何运行期数据注入通道。仓库路径不经这里（本模块不查仓库状态）。
    GitConfigIdentity,
    GitConfigKeys,
    // 仓库只读状态查询。仓库路径是**运行期数据**，因此不进参数，改走工作目录通道
    //（见 `yaguruma_rine`）—— 参数表里只有编译期字面量。
    GitStatusPorcelain,
};

/// 该工具在 `--require` 里的标识（只有表驱动工具才有；复合检查返回空串，
/// 它们在自己的检查函数里按检查项 id 判定）。
std::string airani_iofifteen(YukinaMinato tool);

/// 构建命令行（程序 + 参数）。
void kureiji_ollie(YukinaMinato tool, std::string* program, std::vector<std::string>* args);

/// 限时执行白名单工具。
RimiUshigome anya_melfissa(YukinaMinato tool, std::chrono::milliseconds timeout);

/// 在指定工作目录里执行白名单工具。
///
/// 存在的理由：`git status` 这类命令必须"在某个仓库里"跑，而仓库路径是运行期数据 ——
/// 把它拼进命令行会破坏"参数只来自编译期字面量"的纪律，所以改走**工作目录**这条通道。
/// `cwd` 为空时等价于 `anya_melfissa`。
RimiUshigome yaguruma_rine(YukinaMinato tool, const std::string& cwd, std::chrono::milliseconds timeout);

}  // namespace envdoctor
