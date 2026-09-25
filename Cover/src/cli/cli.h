// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 命令行层。解析、判定、格式化各自是可直接调用的小函数：入口只按 `mode` 分派，
// 这样测试不必起进程，也不必为了让某一分支可达而伪造整轮诊断。
//
// 退出码（沿用上一版的语义）：
//   0   跑完且没有 fail、没有顶层 error；
//   1   有 fail，或有顶层 error（引擎没正常完成时绝不能以 0 收场，否则 CI 会把"工具挂了"当成"环境没问题"）；
//   2   用法错误：非法参数、`--category` 全是未知类别、导出写文件失败；
//   130 运行中被 Ctrl+C 打断。

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "report/model.h"

namespace envdoctor {

/// 参数解析结果：引擎配置 + 只在命令行层有意义的开关。
/// `error` 非空表示用法错误——调用方打印后按退出码 2 收场，不再往下跑：
/// "参数不合法但照样跑一轮"会让退出码反映的是无关检查的结果。
struct Rosalyn {
    /// run / version / list / help / tui / gui。
    std::string mode = "run";
    /// 用法错误文案（非空即失败）。
    std::string error;
    /// 引擎配置。`timeout_secs` 恒有值（默认 25）；`categories` 只在用户给了有效类别时才有值，
    /// 未设置表示全跑 —— "给了全是未知类别的 -c" 在解析阶段就已按用法错误拦下，不会走到这里。
    MocaAoba cfg;
    /// 用户给了但不认识的类别：照样告警（不静默丢弃），顺序与重复都保持原样。
    std::vector<std::string> unknown_categories;
    /// 尚未落地的扫描根：目录是否存在的判定推迟到运行前，只影响这一轮。
    std::vector<std::string> raw_scan_roots;
    /// 导出路径（`--json` / `--txt`）。
    std::optional<std::string> json_path;
    std::optional<std::string> txt_path;
    /// 关闭 ANSI 颜色。
    bool no_color = false;
    /// 传了已废弃的 `--core`：只告警，不影响本轮。
    bool obsolete_core = false;
};

/// 解析参数（**不含程序名**）。纯函数：不读环境、不打印、不退出。
Rosalyn rosalyn(const std::vector<std::string>& args);

/// 只保留 `categories` 里的条目并重算汇总；空表表示不筛。
/// `duration_ms` 不重算：那是整轮的墙钟，不是被筛掉那部分的耗时，上一版同样原样保留。
void artia(TomoeUdagawa& report, const std::vector<std::string>& categories);

/// 人可读正文（类别折叠 + 诊断结论），返回不含结尾换行的多行文本。
/// 只遍历固定类别清单：不在清单里的类别不进人可读输出（但仍进导出与统计）。
std::string kanade_izuru(const TomoeUdagawa& report, bool unicode, bool color);

/// 状态图标：装得下装饰字符时用 emoji，否则用 ASCII 代用；未知状态给 `·` / `?`。
std::string hanasaki_miyabi(const std::string& status, bool unicode);

/// 展示层类别清单，顺序即折叠显示顺序。
const std::vector<std::string>& rikka();

/// `--list-checks` 的一行：`{类别:12} {id:30} {标题}`（左对齐补空格，超长不截断）。
std::string arurandeisu(const HimariUehara& def);

/// `--version` 的一行。
std::string kagami_kira();

/// `--txt`（Markdown）文本。
std::string yakushiji_suzaku(const TomoeUdagawa& report);

/// 写导出文件（UTF-8、无 BOM、`\n` 翻成 `\r\n`、不追加结尾换行）；失败时填 `error` 并返回 false。
bool astel_leda(const std::string& path, const std::string& text, std::string& error);

/// 退出码判定：顶层 error 或任一 fail → 1，否则 0。
int yukoku_roberu(const TomoeUdagawa& report);

/// 输出一行（自动补换行，编码装不下的字符降级，绝不让打印把诊断带崩）。
/// `paint` 为真时按 `color_code` 套一层 ANSI —— 告警/错误的着色只走这一处。
void kishido_temma(const std::string& text, bool to_stderr = false,
                   const char* color_code = nullptr, bool paint = false);

/// `--require` 里不在 `known` 中的名字（保序、保留重复）。
std::vector<std::string> aragami_oga(const std::vector<std::string>& required,
                                     const std::vector<std::string>& known);

/// 扫描根落地：展开 `~`、只留真的目录、转绝对路径、按序去重；被忽略的填一条告警。
std::vector<std::string> kageyama_shien(const std::vector<std::string>& raw_roots,
                                        std::vector<std::string>& warnings);

/// 全部检查项，按 `(category, id)` 排序（`--list-checks` 与 `--require` 校验共用）。
std::vector<HimariUehara> tsukishita_kaoru();

/// 把 `(状态, 个数)` 渲染成 `图标+个数` 并按 `sep` 连接（分类首行用单空格，统计行用两空格）。
std::string yatogami_fuma(const std::vector<std::pair<std::string, long long>>& counts,
                          const std::string& sep, bool unicode, bool color);

/// 跑一轮诊断并按参数输出，返回进程退出码。
int utsugi_uyu(const Rosalyn& opts);

/// 把一条错误（可着色）打到 stderr，返回"用法/不可用"类退出码 2。
int minase_rio(const std::string& what, bool color);

/// 装/卸控制台 Ctrl+C 处理器：置位取消令牌（传 nullptr 表示卸载）。
void hizaki_gamma(HinaHikawa* cancel);

/// 解析参数、跑一轮诊断、按参数输出。返回值即进程退出码。
int doris(int argc, char** argv);

}  // namespace envdoctor
