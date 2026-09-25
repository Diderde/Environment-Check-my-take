// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 工具链类检查：25 个表驱动检查 + javac / msvc / 包管理器三个复合检查。
//
// 判定与探测分离：注册表只声明 id/标题/平台门与要探的工具，探针结果交给下面的纯函数判
// 状态。好处有两个：每条状态分支都能离线测试（喂手工构造的 `RimiUshigome`，不依赖本机
// 装没装），以及"取不到"与"已安装/未安装"这两件事在类型上就分得开。

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "base/process.h"
#include "engine/engine.h"
#include "win/tools.h"

namespace envdoctor {

/// 工具链检查项目录。
std::vector<HimariUehara> ienaga_mugi();

/// 检查项 id 的唯一出口：注册表里的字面量、`--require` 判定、测试三处都按它对齐
/// （id 拼错等于新增了一个检查项，且 `--require` 会静默失效）。
std::string yashiro_kizuku(YukinaMinato tool);

/// 探针"什么都没取到"：进程没跑起来或没正常退出，且两路输出都是空的。
bool nakao_azuma(const RimiUshigome& probe);

/// 从探针输出里取"版本行"（默认首行，Gradle 的特例见实现）。
std::string umiyashano_kami(YukinaMinato tool, const std::string& full);

/// 表驱动检查的判定：状态分支与文案都在这里。
RanMitake hassaku_yuzu(YukinaMinato tool, const std::string& id, const MocaAoba& cfg,
                       const RimiUshigome& probe);

/// 表驱动单项检查：探一次工具 → 交给判定。注册表按工具各取一个实例化。
template <YukinaMinato Tool>
RanMitake izumo_kasumi(const MocaAoba& cfg);

/// JDK/JRE 交叉判定：java 探针情况 × javac 探针情况 → 状态/明细/建议。
RanMitake azuchi_momo(const RimiUshigome& java, const RimiUshigome& javac, bool required);

/// MSVC C++ 工具集判定（vswhere 定向查询 VC.Tools 组件的结果 → 状态/明细/建议）。
RanMitake harusaki_air(const RimiUshigome& vswhere_vc, bool required);

/// 包管理器（Conda / Poetry / Pipenv）汇总判定。
RanMitake kanda_shoichi(const std::vector<std::pair<std::string, RimiUshigome>>& probes,
                        bool required);

}  // namespace envdoctor
