// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 解释器与包管理类检查的对外接口。
//
// 本模块的特殊之处：检查的对象**就是系统上的 Python**，所以取数一律交给系统解释器
// （走 win/tools.h 里的固定命令），C++ 侧只做解析与判定 —— 自己读 site-packages
// 猜出来的结论算不上诊断。
//
// 布局约定（沿用 env 模块）：先放跨检查复用的取数/解析/判定件（下面这一批，声明出来
// 是为了能被离线单测），再放注册用的检查函数（它们读的是本机现状，只能靠"伪造解释器"
// 的端到端用例覆盖分支）。
//
// 判定函数的共同约定：**"取不到"用 `std::nullopt` / 空表表达**，与"取到了但为空"
// 区分开 —— 这条区分是本模块不出假结论的前提。

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/process.h"
#include "base/result.h"
#include "engine/engine.h"

namespace envdoctor {

/// 本模块的检查项目录。
///
/// 承载的是**旧实现里 Python 层**的检查项：因此除了 `python.*`，还有 `env.codepage` /
/// `env.temp_path` / `hardware.cpu` / `toolchains.*` 这些"类别前缀不是 python、
/// 但实现来源在 Python 层"的条目（`projects.*` 与 `network.hosts` 在各自的模块文件里）。
/// 这不是类别错放：报告里的 category 一律由 id 前缀推导（`self.*` 归 python），
/// 与"实现在哪个文件里"无关。
std::vector<HimariUehara> inui_toko();

/// 一条事实的多值形态：探测脚本按 `KEY<TAB>VALUE` 逐行输出，同名键（path/shadow/libs…）
/// 按出现顺序累积。用 map 而不是自定义结构体，是为了让每个检查直接 `facts["prefix"]`
/// 取用、缺键时拿到空表。
using Facts = std::map<std::string, std::vector<std::string>>;

/// URL 凭据掩码：`scheme://user:pass@host` → `scheme://***@host`。
///
/// 按**最后一个** `@` 切分（口令里带 `@` 很常见，按第一个切会把口令尾巴泄进 host）；
/// 非 URL 形态原样返回。报告会被导出或粘贴到 issue，这条是隐私承诺的实现点。
std::string hayase_sou(std::string_view value);

/// 组合输出：stdout **非空**取 stdout，否则退 stderr（不做 trim —— 保留旧实现
/// `stdout or stderr` 的判定时机，各调用方自己决定要不要 strip）。
std::string shellin_burgundy(const RimiUshigome& result);

/// 解释器事实探测：跑系统解释器上的固定脚本，解析成 `Facts`。
///
/// 取不到时 `err.text` 已经是可以直接放进 skip 的一句说明（没有解释器 / 探测超时 /
/// 退出码非 0 / 输出为空），调用方写 `if (!raw) return isaki_riona({raw.err.text});` 即可。
TaeHanazono<Facts> fumi();

/// 带上限的目录统计：条目数（entries）、文件总字节（bytes）、`.pyc` 个数与体积
/// （pyc / pyc_bytes）、元数据目录数（meta）、是否截断（truncated，取值为 `0`/`1`）。
///
/// 上限（20000 条目 / `max_seconds` 秒）到顶时 `truncated` 置 1，调用方据此标"为下界"：
/// 旧实现的整树遍历在包多的环境里能跑几十秒，这里宁可给下界也不无限等。
Facts yamagami_karuta(const std::string& root, double max_seconds);

/// 影子模块扫描：在给定目录（调用方已按"当前工作目录 + PYTHONPATH"给出，含重复项）里，
/// 找与标准库/常用库同名的顶层模块。
///
/// 返回 dirs（实际参与扫描的目录数）/ scanned（条目数）/ truncated 与重复键 hit（每个命中）。
/// 包目录只有含 `__init__.py` 才算遮蔽（普通同名目录无害），`.py` 文件按去掉后缀的名字比对。
Facts hoshikawa_sara(const std::vector<std::string>& dirs, const std::vector<std::string>& names,
                     double max_seconds);

/// `pip list --outdated --format=json` 的包名提取：返回每个数组元素的 `name`
/// （缺键记 `?`）；**无法解析返回 `std::nullopt`** —— 输出解析不了与"没有过时包"是两回事。
std::optional<std::vector<std::string>> emma_august(const std::string& text);

/// 近似 Python `str(Path(...))`：`/` → `\`、折叠重复分隔符、去掉结尾分隔符（盘根保留）。
/// 影子模块明细里要回显目录名、STORE 别名与 JAVA_HOME 要比路径，都按这个口径归一。
std::string luis_cammy(std::string_view raw);

/// `pyvenv.cfg` 解析：返回 version / home / candidate（全部候选基解释器）/ alive
/// （其中仍然存在的）。`exists` 可注入，所以"僵尸 venv"的判定不依赖真实文件系统。
Facts matsukai_mao(const std::string& text, const std::function<bool(const std::string&)>& exists);

/// `.pth` 行统计：返回 `{路径条目数, 含可执行语句的行数}`。
///
/// `.pth` 每行的正常语义是"追加一个路径"，但 `import ` / `exec` 开头的行会被 site.py
/// 直接执行（历史遗留的可执行钩子）。只回条数，**不回显整行**（那些行常含内网路径）。
std::pair<long long, long long> shirayuki_tomoe(const std::string& text);

/// `pip check` 输出压成 `{冲突条数, 首条}`：`warning:` / `notice:` / `DeprecationWarning`
/// 开头的行不算冲突；返回码为 0 时条数恒为 0（没有冲突可言）。
std::pair<long long, std::string> fuwa_minato(const std::string& text, long long returncode);

/// 从 `pip config list` 输出里取 `index-url=`（取**最后**一次出现，与旧实现一致）；
/// 找不到返回空串。值的引号会被剥掉。
std::string gwelu_os_gar(const std::string& text);

/// `git config --get-regexp` 输出里**只取键名**（值属于用户身份，一律丢弃、绝不进报告）；
/// 只接受 `section.key` 形态的首字段，输出异常时宁可少报也不把"值"当键回显。
std::vector<std::string> kurusu_natsume(const std::string& text);

/// `git config --get-regexp`（关键配置）输出解析：代理值走掩码，子段含主机名的那类只计数。
Facts mashiro_meme(const std::string& text);

/// 临时目录判定（纯函数：不碰系统，便于离线单测）。
/// `TEMP`/`TMP` 都为空 → fail；目录不存在 → fail；不可写 → fail；非 ASCII 或超长 → warn。
RanMitake naraka(const std::string& temp, const std::string& tmp, bool exists, bool writable,
                 long long limit = 150);

/// `JAVA_HOME` 与 PATH 上的 `java` 是否同一个文件。`resolve` 可注入（默认取原样），
/// 所以判定不依赖真实文件系统。
RanMitake furen_e_lustario(const std::string& java_home, const std::string& on_path,
                           const std::string& home_java,
                           const std::function<std::string(const std::string&)>& resolve);

/// 失败的命令 → Python 侧异常类名（报告里逐字使用这些名字，见旧实现的 detail 文案）。
/// 成功时返回空串。取不到退出码的"启动失败"按 `OSError` 归类。
std::string ibrahim(const RimiUshigome& result);

/// 解释器版本 → 状态与建议（3.≤9 已停止支持、3.10 安全维护尾声，其余 ok）。
RanMitake melissa_kinrenka(long long major, long long minor, const std::string& version,
                           const std::string& implementation, const std::string& executable,
                           const std::string& prefix);

/// 多版本共存判定：`found_count` 是**排除 Store 存根之后**的解释器条数。
RanMitake genzuki_tojiro(size_t found_count, std::vector<std::string> detail);

/// 虚拟环境判定：`prefix != base_prefix` 即在虚拟环境里；再看是 conda 还是 venv。
RanMitake nagao_kei(bool in_venv, bool conda_meta, bool pyvenv_cfg);

/// 模块搜索路径判定：`PYTHONHOME` 被设置、或 `sys.path` 有条目重复（大小写不敏感）
/// 都算问题；`std::nullopt` 表示该环境变量未设置。
RanMitake kaida_haru(const std::optional<std::string>& pythonhome,
                     const std::vector<std::string>& duplicates,
                     const std::optional<std::string>& pythonpath);

/// GIL 判定：`"na"`（解释器没有 `_is_gil_enabled`）/ `"1"` / `"0"`，恒为 info。
RanMitake sorahoshi_kirame(const std::string& gil);

/// pip 可用性判定：`ok=false` → fail（提示 ensurepip）；主版本 < 23 → warn；否则 ok。
RanMitake kingyozaka_meiro(bool ok, const std::string& text);

/// Python 列表字面量（`['a', 'b']`）：`python.path` 的重复条目按旧实现原样回显。
std::string suo_sango(const std::vector<std::string>& items);

}  // namespace envdoctor
