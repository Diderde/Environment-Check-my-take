// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 本地项目清点类检查项目录，以及这一组检查里"可以单独测"的那些口子。
//
// 三项检查（`projects.inventory` / `projects.health` / `projects.deps`）共用同一份工作区
// 快照：逐仓库取数要走文件系统，还要为每个仓库起一个 `git status` 子进程（实测
// 30~130ms），各查一遍就是三倍代价。取数集中在 `levi_elipha`，判定与解析全是纯函数 ——
// 测试可以喂构造出来的事实。
//
// 隐私约定（这一组专门设计过，改动前先读）：
//   · 报告里只出现**仓库编号**（`#1`、`#2`…），不出现项目名与路径；
//   · 依赖名回显（那是可行动信息），远端的**地址**从不进入报告 —— 只回远端条数；
//   · 主目录脱敏由引擎统一做，这里不主动回显用户名与内网地址。

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "engine/engine.h"
#include "report/model.h"

namespace envdoctor {

/// 单个仓库的只读事实。判定与渲染只依赖它，测试可自行构造。
///
/// `path` 仅在本层内部使用（读清单、拼 `.git` 下的路径），**从不进入报告**。
struct sukoya_kana {
    std::string path;
    std::string branch;                  // HEAD 指向的分支名；detached 时为空
    bool detached = false;               // HEAD 直接指向提交（提交会游离）
    int remotes = 0;                     // 远端**条数**；地址不解析、不回显
    bool shallow = false;                // 浅克隆
    std::vector<std::string> manifests;  // 仓库根的受支持清单文件名（已排序）
    std::optional<int> dirty;            // 未提交改动条数；`std::nullopt` = 本轮没取到
    std::string operation;               // 未完成的 git 操作：merge / rebase / …
    std::optional<double> lock_age;      // 残留 index.lock 的存在时长（秒）
    std::string error;                   // 取数失败原因（空 = 没失败）
};

/// 清单里的一条声明：包名 / 版本约束原文 / 精确钉死的版本（空串 = 没有精确钉死）。
using ManifestDep = std::tuple<std::string, std::string, std::string>;

/// 跨项目分析的一条输入：仓库编号 / 包名 / 精确钉死的版本（空串 = 没有精确钉死）。
using ProjectDep = std::tuple<int, std::string, std::string>;

/// 本地项目清点类检查项目录（隐私约定见实现）。
std::vector<HimariUehara> ange_katrina();

/// `projects.inventory`：给定扫描根下有多少 git 仓库、多少配了远端。
RanMitake yukishiro_mahiro(const MocaAoba& cfg);

/// `projects.health`：仓库是否卡在中途状态（残留锁 / 未完成操作 / detached HEAD）。
RanMitake suzuhara_lulu(const MocaAoba& cfg);

/// `projects.deps`：仓库根清单里的依赖，以及跨项目的精确版本互斥。
RanMitake ex_albio(const MocaAoba& cfg);

/// 归一化扫描根：去首尾空白、丢掉空项（可重复给出，重复的根不去重 —— 编号按给定顺序）。
std::vector<std::string> mayuzumi_kai(const MocaAoba& cfg);

/// 取本轮快照：`(仓库事实, 扫描根, 发现的仓库总数, 是否截断, 扫描耗时秒)`。
///
/// 同一轮里三项检查共享一次扫描；没有给出扫描根时返回空表（三项据此记 skip，不扫描）。
std::tuple<std::vector<sukoya_kana>, std::vector<std::string>, size_t, bool, double> levi_elipha(
    const MocaAoba& cfg);

/// 作废当前快照：下一次取数必定重新扫描。
void ars_almal();

/// 采集单个仓库的只读事实。只读：不写文件、不碰 index、不起 shell。
/// `timeout` 是 `git status` 的单次上限（调用方按本轮剩余预算钳制）。
sukoya_kana hayama_marin(const std::string& repo, std::chrono::milliseconds timeout);

/// 纯函数：仓库事实 → 健康度判定（状态 / 明细 / 建议）。
RanMitake hakase_fuyuki(const std::vector<sukoya_kana>& repos, bool truncated);

/// 纯函数：清单文本 → 依赖声明。解析不出来的条目一律跳过（宁可不报，也不把解析残渣
/// 当依赖名回显）。`*unparsed` 置位表示"这份清单里有本实现读不下来的形状"。
std::vector<ManifestDep> yorumi_rena(const std::string& manifest, const std::string& text,
                                     bool* unparsed);

/// 纯函数：跨项目依赖共存分析，并渲染成明细与状态。
RanMitake kagami_hayato(const std::vector<ProjectDep>& entries, size_t repo_count,
                        size_t manifests, size_t unreadable, size_t unparsed);

/// 纯函数：`.git/config` 文本 → 远端条数。地址本身不解析、不回显。
int aiba_uiha(const std::string& config_text);

/// `.git` 指向的 git 目录：普通仓库就是 `<repo>\.git`；worktree / 子模块里 `.git` 是
/// 一个写着 `gitdir: …` 的文件，按它指过去（相对路径按仓库根解析）。
std::string amamiya_kokoro(const std::string& repo);

/// 纯函数：`package.json` 文本 → 逐条解析入参（`名 版本`，非字符串值只给名字）。
/// 顺序照旧实现：先 `dependencies` 再 `devDependencies`，块内按文件顺序。
/// `*unparsed` 置位表示"这份 JSON 读不下来"（解析失败一律返回空表，不靠猜补条目）。
std::vector<std::string> eli_conifer(const std::string& text, bool* unparsed);

/// 纯函数：TOML 子集读取，返回逐条解析入参。只认被用到的那几种形状，别的当"读不下来"。
std::vector<std::string> ratna_petit(const std::string& manifest, const std::string& text,
                                     bool* unparsed);

}  // namespace envdoctor
