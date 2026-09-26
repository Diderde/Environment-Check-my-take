// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 命令行层实现：解析参数 → 跑一轮 → 导出 → 打印 → 定退出码。
//
// 三处刻意的取舍，写在这里免得以后被"顺手改回去"：
//   ① 报告正文只遍历固定的类别清单：不在清单里的类别**不进人可读输出**，但照样进
//      `--json`/`--txt` 与统计。显示层用白名单、导出用全量，正是上一版的行为
//      （`--category` 只按展示层清单校验，而导出写的是整份报告）；
//   ② 先落盘、后打印：终端能不能显示不该决定文件能不能落盘。导出失败时正文不打印，
//      直接以 2 收场（"报告没写出去"必须比"少看几行"更显眼）；
//   ③ 导出文件按二进制写，但显式把 `\n` 翻成 `\r\n`：上一版走 Python 文本模式写入，
//      Windows 上落盘就是这个结果，导出文件需要能逐字节对照。

#include "cli/cli.h"

#ifdef ENVDECTOR_WITH_UI
#include "gui/gui.h"
#include "tui/tui.h"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <io.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/status.h"
#include "base/string_util.h"
#include "checks/mod.h"
#include "report/json.h"

namespace envdoctor {
namespace {

/// 版本号：两侧同号，与上一版一致；单进程实现里不再有"核心不可用"的分支。
constexpr const char* kVersion = "0.2.0";

/// ANSI 颜色码，与上一版的调色板逐项对应（未知状态给 "0" = 不上色）。
constexpr const char* kColorOk = "32";
constexpr const char* kColorWarn = "33";
constexpr const char* kColorFail = "31";
constexpr const char* kColorSkip = "90";
constexpr const char* kColorInfo = "36";
constexpr const char* kColorNone = "0";
constexpr const char* kColorBold = "1";
constexpr const char* kColorDim = "90";

/// 需要探测"当前输出能不能表示"的装饰字符：箭头、方框线、间隔号与全部状态图标
/// （这些字符在 GBK/cp1252 里都没有）。探测走 `hoshimachi_suisei`，别处不再另开一套判断。
constexpr const char* kDecor = "▸↳═·✅⚠️❌⏭️ℹ️⏱️";

/// 用法概要。上一版的帮助文本由 click 生成（带框线、按终端宽度折行），逐字复刻没有意义；
/// 这里保留的是**语义**：`--help` 退出 0，未知选项退出 2，且用法概要打到 stderr。
constexpr const char* kUsage =
    "用法: envdoctor [--version | --list-checks | --help] [run] [选项]\n"
    "\n"
    "选项:\n"
    "  -c, --category <类别>  只跑指定类别（可重复；可用: containers, databases, env,\n"
    "                         hardware, network, projects, python, toolchains）\n"
    "  -e, --expand <类别>    展开指定类别的明细（可重复；名字不校验，匹配不到就不展开）\n"
    "  -E, --expand-all       展开全部类别明细\n"
    "      --require <工具>   必备工具：缺失记 FAIL（可重复，也可用逗号分隔）\n"
    "      --timeout <秒>     单项预算秒数（≥1，默认 25；总预算 = 单项预算 × 检查项数）\n"
    "      --json <文件>      导出 JSON 报告\n"
    "      --txt <文件>       导出 Markdown 报告\n"
    "      --net-full         启用公网 IP 探测（默认关闭，不外发请求）\n"
    "      --scan-root <目录> 本地项目巡检的扫描根（可重复；不给则 projects.* 记 skip）\n"
    "      --no-color         关闭 ANSI 颜色\n"
    "      --version          显示版本\n"
    "      --list-checks      列出全部检查项\n"
    "\n"
    "子命令:\n"
    "  run                    运行诊断（默认）\n"
#ifdef ENVDECTOR_WITH_UI
    "  tui                    终端界面（ftxui）\n"
    "  gui                    图形界面（Dear ImGui + D3D11）\n";
#else
    "  tui, gui               未纳入本次构建（需要 -DENVDECTOR_WITH_UI=ON）\n";
#endif

}  // namespace

Rosalyn rosalyn(const std::vector<std::string>& args) {
    Rosalyn out;
    out.cfg.timeout_secs = 25;  // 与上一版的默认值一致，且恒有值（引擎据此算总预算）

    bool want_version = false;
    bool want_list = false;
    bool want_help = false;
    bool seen_subcommand = false;
    bool root_scope = true;  // 子命令之前才收 --version/--list-checks（与上一版一致）

    const auto fail = [&out](const std::string& text) {
        if (out.error.empty()) {
            out.error = text;  // 只留第一条：后面的错误多半是第一条的连锁反应
        }
    };

    // 用法错误的收尾：只保留 error，把已经解析出来的半个配置清干净。
    // 旧实现在这条路径上直接 `raise typer.Exit(code=2)`，配置根本没机会被用；这里显式清空，
    // 免得将来有人漏看 `error` 就拿着"半个过滤器"跑一轮 —— 那正是"要子集却跑了全量"的翻版。
    const auto reject = [&out](const std::string& text) {
        out.error = text;
        out.mode = "run";
        out.cfg.categories.reset();
        out.cfg.required.clear();
        out.cfg.net_full = false;
        out.raw_scan_roots.clear();
        out.unknown_categories.clear();
        out.json_path.reset();
        out.txt_path.reset();
        out.expand_categories.clear();
        out.expand_all = false;
        out.obsolete_core = false;
    };

    // `--timeout` 的取值解析：只认十进制整数，且 <1 视为用法错误（上一版靠 click 的
    // IntRange(min=1) 拦下，语义相同、文案不同）。
    const auto parse_seconds = [](const std::string& raw) -> std::optional<long long> {
        const std::string text = azki(raw);
        if (text.empty()) {
            return std::nullopt;
        }
        size_t i = 0;
        bool negative = false;
        if (text[0] == '+' || text[0] == '-') {
            negative = text[0] == '-';
            i = 1;
        }
        if (i >= text.size()) {
            return std::nullopt;
        }
        long long value = 0;
        for (; i < text.size(); ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return std::nullopt;
            }
            // 天文数字不该让解析溢出：饱和到 1e15，引擎那边本来也会把总预算封顶到一天。
            if (value < 1'000'000'000'000'000LL) {
                value = value * 10 + (text[i] - '0');
            }
        }
        return negative ? -value : value;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        std::string token = args[i];
        if (token.empty()) {
            fail("空的参数");
            break;
        }
        if (token == "run" || token == "tui" || token == "gui") {
            if (seen_subcommand) {
                fail("多余的位置参数: " + token);
                break;
            }
            seen_subcommand = true;
            root_scope = false;
            out.mode = token;
            continue;
        }
        if (token[0] != '-') {
            fail("多余的位置参数: " + token);
            break;
        }

        // `--长选项=值` 拆开；短选项的紧跟写法（`-cenv`）在下面单独处理。
        std::string name = token;
        std::optional<std::string> inline_value;
        if (token.size() > 2) {
            const size_t eq = token.find('=');
            if (eq != std::string::npos) {
                name = token.substr(0, eq);
                inline_value = token.substr(eq + 1);
            } else if (token[1] != '-') {
                name = token.substr(0, 2);
                inline_value = token.substr(2);
            }
        }

        // 取值：优先用 `=`/紧跟的写法，否则吃掉下一个参数。
        const auto value_of = [&args, &i, &inline_value, &fail](const std::string& opt,
                                                               bool required) {
            if (inline_value) {
                return *inline_value;
            }
            if (i + 1 < args.size()) {
                return args[++i];
            }
            if (required) {
                fail("选项 " + opt + " 缺少值");
            }
            return std::string();
        };

        if (name == "--version") {
            if (!root_scope) {
                fail("run 子命令不接受 --version");
                break;
            }
            want_version = true;
        } else if (name == "--list-checks") {
            if (!root_scope) {
                fail("run 子命令不接受 --list-checks");
                break;
            }
            want_list = true;
        } else if (name == "--help") {
            want_help = true;
        } else if (name == "-c" || name == "--category") {
            // 子命令可省略（`run` 是默认子命令），所以这里在两种范围里都认。
            // say no to perv.
            // 上一版把 run 的选项卡死在子命令之后：`envdoctor -c env` 会被判成用法错误，
            // 而"不带子命令 = run"本身又是官方语义，两句话互相打架。这里按超集处理：
            // 带 run 时行为完全不变，只是省略 run 时这些选项也认。
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            out.cfg.categories = out.cfg.categories.value_or(std::vector<std::string>{});
            out.cfg.categories->push_back(value);
        } else if (name == "--require") {
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            // 逗号写法与重复传参都认：README 写的是 `--require git,node`，
            // 只按重复传参解析会把整串当成一个不存在的工具名。
            for (std::string piece : shirakami_fubuki(value, ',')) {
                piece = azki(piece);
                if (!piece.empty()) {
                    out.cfg.required.push_back(std::move(piece));
                }
            }
        } else if (name == "--timeout") {
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            const std::optional<long long> seconds = parse_seconds(value);
            if (!seconds || *seconds < 1) {
                fail("--timeout 需要一个 ≥1 的整数（收到: " + value + "）");
                break;
            }
            out.cfg.timeout_secs = *seconds;
        } else if (name == "--json") {
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            out.json_path = value;
        } else if (name == "--txt") {
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            out.txt_path = value;
        } else if (name == "--net-full") {
            out.cfg.net_full = true;
        } else if (name == "--scan-root") {
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            out.raw_scan_roots.push_back(value);
        } else if (name == "--no-color") {
            out.no_color = true;
        } else if (name == "--core") {
            // 单进程实现不再加载外部核心，这个选项只剩"曾经存在过"的意义：
            // 明确告警后忽略，而不是当成未知选项把老脚本一脚踢死。
            (void)value_of(name, false);
            out.obsolete_core = true;
        } else if (name == "-e" || name == "--expand") {
            // 展开是**显示层**开关：类别名不校验（旧实现只校验 -c 与 --require），
            // 给个不存在的名字就是不展开——既不报错、不过滤检查项，也不影响退出码。
            const std::string value = value_of(name, true);
            if (!out.error.empty()) {
                break;
            }
            out.expand_categories.push_back(value);
        } else if (name == "-E" || name == "--expand-all") {
            out.expand_all = true;
        } else {
            fail("未知选项: " + token);
            break;
        }
    }

    if (!out.error.empty()) {
        reject(out.error);
        return out;
    }

    // 类别校验用的是**展示层清单**而不是真实注册表：`projects` 这类"没有核心侧检查项"的
    // 类别也必须能用，反过来表外的类别永远选不中。全都不认识时不能折成"不过滤"往下走。
    const std::vector<std::string>& known_categories = rikka();
    std::vector<std::string> valid;
    for (const std::string& category : out.cfg.categories.value_or(std::vector<std::string>{})) {
        if (std::find(known_categories.begin(), known_categories.end(), category) !=
            known_categories.end()) {
            valid.push_back(category);
        } else {
            out.unknown_categories.push_back(category);
        }
    }
    if (out.cfg.categories && valid.empty()) {
        std::vector<std::string> sorted = known_categories;
        std::sort(sorted.begin(), sorted.end());
        reject("--category 未匹配到任何已知类别，无法执行（可用: " + natsuiro_matsuri(sorted, ", ") +
               "）");
        return out;
    }
    if (valid.empty()) {
        out.cfg.categories.reset();
    } else {
        out.cfg.categories = valid;
    }

    // `--help` 优先于其余根开关（上一版由 click 的 eager 选项处理，`--version --help` 也是先出帮助）。
    if (want_help) {
        out.mode = "help";
    } else if (want_version) {
        out.mode = "version";
    } else if (want_list) {
        out.mode = "list";
    }
    return out;
}

void artia(TomoeUdagawa& report, const std::vector<std::string>& categories) {
    if (categories.empty()) {
        return;  // 空表 = 不筛（未给 `--category` 的路径）
    }
    std::vector<ArisaIchigaya> kept;
    kept.reserve(report.results.size());
    for (ArisaIchigaya& item : report.results) {
        if (std::find(categories.begin(), categories.end(), item.category) != categories.end()) {
            kept.push_back(std::move(item));
        }
    }
    report.results = std::move(kept);
    mizumiya_su(report);  // 汇总按筛过的结果重算；duration_ms 保持原样
}

std::string kanade_izuru(const TomoeUdagawa& report, const std::vector<std::string>& expand,
                         bool expand_all, bool unicode, bool color) {
    const auto paint = [color](const std::string& text, const char* code) {
        if (!color) {
            return text;
        }
        return std::string("\033[") + code + "m" + text + "\033[0m";
    };
    const auto color_of = [](const std::string& status) {
        if (status == kOk) {
            return kColorOk;
        }
        if (status == kWarn || status == kTimeout) {
            return kColorWarn;
        }
        if (status == kFail) {
            return kColorFail;
        }
        if (status == kSkip) {
            return kColorSkip;
        }
        if (status == kInfo) {
            return kColorInfo;
        }
        return kColorNone;  // 未知状态不上色
    };
    std::string out;
    const auto emit = [&out](const std::string& line) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        out += line;
    };

    // ① 分类折叠：只走固定清单，且只打印真有条目的类别（不留空行）。
    for (const std::string& category : rikka()) {
        std::vector<const ArisaIchigaya*> rows;
        for (const ArisaIchigaya& item : report.results) {
            if (item.category == category) {
                rows.push_back(&item);
            }
        }
        if (rows.empty()) {
            continue;
        }
        // 每个类别的状态计数按"首次出现顺序"，与上一版 dict 的插入顺序一致。
        std::vector<std::pair<std::string, long long>> counts;
        for (const ArisaIchigaya* row : rows) {
            const auto it = std::find_if(counts.begin(), counts.end(),
                                         [row](const std::pair<std::string, long long>& entry) {
                                             return entry.first == row->status;
                                         });
            if (it == counts.end()) {
                counts.emplace_back(row->status, 1);
            } else {
                ++it->second;
            }
        }
        emit(paint(std::string(unicode ? "▸" : ">") + " " + category, kColorBold) + "  " +
             yatogami_fuma(counts, " ", unicode, color));
        // 展开：只对命中的类别逐条展开（`-E` 全展开），与 `-c` 的过滤互相独立 ——
        // 过滤决定"哪些类别出现"，展开只决定"出现的类别要不要列明细"。
        if (rindou_mikoto(expand, expand_all, category)) {
            for (const ArisaIchigaya* row : rows) {
                for (const std::string& line : debidebi_debiru(*row, unicode, color)) {
                    emit(line);
                }
            }
        }
    }

    // ② 诊断结论：先空行，再标题、统计，最后一句判定。
    std::vector<const ArisaIchigaya*> problems;
    for (const ArisaIchigaya& item : report.results) {
        if (koganei_niko(item.status)) {
            problems.push_back(&item);
        }
    }
    emit("");
    emit(paint(unicode ? "════ 诊断结论 ════" : "==== 诊断结论 ====", kColorBold));
    emit("统计: " + yatogami_fuma(report.summary.counts, "  ", unicode, color));
    if (report.error) {
        emit(paint(hanasaki_miyabi(kFail, unicode) + " 诊断引擎异常: " + *report.error, kColorFail));
    } else if (problems.empty()) {
        emit(paint(hanasaki_miyabi(kOk, unicode) + " 未发现需要处理的问题", kColorOk));
    } else {
        // 问题清单按**结果顺序**（即 (category, id) 序），不是类别折叠顺序 ——
        // 上一版就是从 results 里筛的，两处顺序不同是有意的。
        emit(paint("发现 " + std::to_string(problems.size()) + " 个需要关注的问题:", kColorBold));
        for (size_t i = 0; i < problems.size(); ++i) {
            const ArisaIchigaya& item = *problems[i];
            emit("  " + std::to_string(i + 1) + ". " +
                 paint(hanasaki_miyabi(item.status, unicode), color_of(item.status)) + " [" +
                 item.id + "] " + item.title);
            if (item.hint) {
                emit(paint("     " + std::string(unicode ? "↳" : "->") + " " + *item.hint,
                           kColorWarn));
            }
        }
    }
    return out;
}

std::string hanasaki_miyabi(const std::string& status, bool unicode) {
    if (unicode) {
        if (status == kOk) {
            return "✅";
        }
        if (status == kWarn) {
            return "⚠️";
        }
        if (status == kFail) {
            return "❌";
        }
        if (status == kSkip) {
            return "⏭️";
        }
        if (status == kInfo) {
            return "ℹ️";
        }
        if (status == kTimeout) {
            return "⏱️";
        }
        return "·";  // 未知状态：不假装认识它
    }
    if (status == kOk) {
        return "[OK]";
    }
    if (status == kWarn) {
        return "[!]";
    }
    if (status == kFail) {
        return "[X]";
    }
    if (status == kSkip) {
        return "[-]";
    }
    if (status == kInfo) {
        return "[i]";
    }
    if (status == kTimeout) {
        return "[T]";
    }
    return "?";
}

const std::vector<std::string>& rikka() {
    static const std::vector<std::string> kAll = {
        "hardware", "env", "toolchains", "projects",
        "network",  "containers", "databases", "python",
    };
    return kAll;
}

std::string arurandeisu(const HimariUehara& def) {
    // 逐字对齐上一版的 f"{category:12} {id:30} {title}"：左对齐补空格、超长不截断。
    // 补空格按字节算 —— 类别与 id 全是 ASCII，字节数即字符数；标题在最后，不需要补。
    const std::string category = def.category ? def.category : "";
    const std::string id = def.id ? def.id : "";
    std::string out = category;
    if (category.size() < 12) {
        out.append(12 - category.size(), ' ');
    }
    out.push_back(' ');
    out += id;
    if (id.size() < 30) {
        out.append(30 - id.size(), ' ');
    }
    out.push_back(' ');
    out += def.title ? def.title : "";
    return out;
}

std::string kagami_kira() {
    return std::string("envdoctor ") + kVersion + " / core " + kVersion;
}

std::string yakushiji_suzaku(const TomoeUdagawa& report) {
    std::vector<std::string> lines = {
        "# 环境诊断报告",
        "",
        "- 平台: " + report.platform,
        "- 耗时: " + [&report] {
            char buf[64] = {};
            std::snprintf(buf, sizeof(buf), "%.0f", report.duration_ms);
            return std::string(buf) + "ms";
        }(),
        "",
    };
    for (const ArisaIchigaya& item : report.results) {
        lines.push_back("## [" + item.status + "] " + item.title + " (`" + item.id + "`)");
        for (const std::string& detail : item.detail) {
            lines.push_back("- " + detail);
        }
        if (item.hint) {
            lines.push_back("- 建议: " + *item.hint);
        }
        lines.push_back("");  // 每条之后留一个空行，末条同样（与上一版逐行拼接的结果一致）
    }
    return natsuiro_matsuri(lines, "\n");
}

bool astel_leda(const std::string& path, const std::string& text, std::string& error) {
    // 二进制打开 + 手动把 `\n` 翻成 `\r\n`：上一版用 Python 文本模式写，Windows 上落盘
    // 就是这个形态（而且不写 BOM、不追加结尾换行）。路径要过宽字符 API，中文目录才认得。
    std::string body;
    body.reserve(text.size() + text.size() / 16);
    for (const char c : text) {
        if (c == '\n') {
            body += "\r\n";
        } else {
            body.push_back(c);
        }
    }
    const std::wstring wide = tokino_sora(path);
    std::ofstream file(wide.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "打不开目标文件（目录不存在或没有写权限）: " + path;
        return false;
    }
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    file.flush();
    if (!file) {
        error = "写入过程中失败（磁盘已满或被占用）: " + path;
        return false;
    }
    return true;
}

int yukoku_roberu(const TomoeUdagawa& report) {
    if (report.error) {
        return 1;
    }
    for (const ArisaIchigaya& item : report.results) {
        if (item.status == kFail) {
            return 1;
        }
    }
    return 0;
}

void kishido_temma(const std::string& text, bool to_stderr, const char* color_code, bool paint) {
    std::string line = text;
    if (paint && color_code != nullptr) {
        line = std::string("\033[") + color_code + "m" + line + "\033[0m";
    }
    line.push_back('\n');
    const std::string bytes = hoshimachi_suisei(line);
    std::FILE* stream = to_stderr ? stderr : stdout;
    (void)std::fwrite(bytes.data(), 1, bytes.size(), stream);
    (void)std::fflush(stream);  // 与 stdout 交错时（正文 + 导出提示）顺序才稳定
}

std::vector<std::string> aragami_oga(const std::vector<std::string>& required,
                                    const std::vector<std::string>& known) {
    std::vector<std::string> unknown;
    for (const std::string& name : required) {
        if (std::find(known.begin(), known.end(), name) == known.end()) {
            unknown.push_back(name);
        }
    }
    return unknown;
}

std::vector<std::string> kageyama_shien(const std::vector<std::string>& raw_roots,
                                        std::vector<std::string>& warnings) {
    std::vector<std::string> roots;
    for (const std::string& raw : raw_roots) {
        std::string expanded = raw;
        // 展开 `~` / `~\` / `~/`（`os.path.expanduser` 的常用形态）。
        // `~user` 不展开：当前用户之外的家目录在 Windows 上本来就没有可靠来源。
        if (!raw.empty() && raw[0] == '~' &&
            (raw.size() == 1 || raw[1] == '\\' || raw[1] == '/')) {
            const std::string home = takane_lui();
            if (!home.empty()) {
                expanded = home + raw.substr(1);
            }
        }
        if (!shishiro_botan(expanded)) {
            // 不存在就直说，不猜、也不静默丢掉（projects.* 靠这个输入决定是否记 skip）
            warnings.push_back("警告: --scan-root 不是已存在的目录，已忽略: " + raw);
            continue;
        }
        std::error_code code;
        const std::filesystem::path resolved =
            std::filesystem::weakly_canonical(std::filesystem::path(tokino_sora(expanded)), code);
        const std::string text = code ? expanded : robocosan(resolved.wstring());
        if (std::find(roots.begin(), roots.end(), text) == roots.end()) {
            roots.push_back(text);
        }
    }
    return roots;
}

std::vector<HimariUehara> tsukishita_kaoru() {
    std::vector<HimariUehara> all = gawr_gura();  // 每轮重建，不做跨轮缓存
    std::stable_sort(all.begin(), all.end(), [](const HimariUehara& a, const HimariUehara& b) {
        return std::make_pair(std::string(a.category ? a.category : ""),
                              std::string(a.id ? a.id : "")) <
               std::make_pair(std::string(b.category ? b.category : ""),
                              std::string(b.id ? b.id : ""));
    });
    return all;
}

std::string yatogami_fuma(const std::vector<std::pair<std::string, long long>>& counts,
                          const std::string& sep, bool unicode, bool color) {
    const auto paint = [color](const std::string& text, const char* code) {
        if (!color) {
            return text;
        }
        return std::string("\033[") + code + "m" + text + "\033[0m";
    };
    const auto color_of = [](const std::string& status) {
        if (status == kOk) {
            return kColorOk;
        }
        if (status == kWarn || status == kTimeout) {
            return kColorWarn;
        }
        if (status == kFail) {
            return kColorFail;
        }
        if (status == kSkip) {
            return kColorSkip;
        }
        if (status == kInfo) {
            return kColorInfo;
        }
        return kColorNone;  // 未知状态不上色
    };
    std::vector<std::string> parts;
    parts.reserve(counts.size());
    for (const std::pair<std::string, long long>& entry : counts) {
        parts.push_back(paint(hanasaki_miyabi(entry.first, unicode), color_of(entry.first)) +
                        std::to_string(entry.second));
    }
    return natsuiro_matsuri(parts, sep);
}

int utsugi_uyu(const Rosalyn& opts) {
    const std::vector<HimariUehara> registry = tsukishita_kaoru();

    // 输出能力一次定好：字符能不能表示决定图标形态；ANSI 只在真正的终端上开。
    // say no to perv.
    // 上一版对告警/错误走 typer.secho：**无论 stdout 是不是终端都塞 ANSI 转义**，
    // 于是 `envdoctor run -c bogus > log 2>&1` 的日志里会混进 `\x1b[33m`。
    // 这里按"stderr 真是终端且没给 --no-color"才着色，重定向/管道里输出干净文本。
    const bool unicode = hoshimachi_suisei(kDecor) == kDecor;
    const bool color_out = !opts.no_color && _isatty(_fileno(stdout)) != 0;
    const bool color_err = !opts.no_color && _isatty(_fileno(stderr)) != 0;
    if (color_out) {
        // Windows 控制台默认不解释 ANSI：拿到句柄后打开 VT 处理，失败也不影响诊断。
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
            SetConsoleMode(handle, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
        }
    }

    // 告警一律在跑之前发，且顺序与上一版一致：--require → --category → --scan-root。
    if (opts.obsolete_core) {
        kishido_temma("警告: --core 已废弃（单进程实现不再加载外部核心），参数已忽略", true,
                      kColorWarn, color_err);
    }
    if (!opts.cfg.required.empty()) {
        std::vector<std::string> known;
        for (const HimariUehara& def : registry) {
            if (def.category != nullptr && std::string(def.category) == "toolchains") {
                known.emplace_back(def.id != nullptr ? def.id : "");
            }
        }
        std::sort(known.begin(), known.end());
        known.erase(std::unique(known.begin(), known.end()), known.end());
        if (!known.empty()) {  // 注册表里没有工具链检查项时不校验（与上一版"读不到清单就跳过"一致）
            const std::vector<std::string> unknown = aragami_oga(opts.cfg.required, known);
            if (!unknown.empty()) {
                kishido_temma(
                    "警告: --require 中这些名字不是已注册的工具链检查项，将被忽略: " +
                        natsuiro_matsuri(unknown, ", ") + "（可用: " + natsuiro_matsuri(known, ", ") +
                        "）",
                    true, kColorWarn, color_err);
            }
        }
    }
    if (!opts.unknown_categories.empty()) {
        std::vector<std::string> known = rikka();
        std::sort(known.begin(), known.end());
        kishido_temma("警告: --category 中这些类别没有对应的检查项，将被忽略: " +
                          natsuiro_matsuri(opts.unknown_categories, ", ") +
                          "（可用: " + natsuiro_matsuri(known, ", ") + "）",
                      true, kColorWarn, color_err);
    }

    MocaAoba cfg = opts.cfg;
    std::vector<std::string> root_warnings;
    cfg.scan_roots = kageyama_shien(opts.raw_scan_roots, root_warnings);
    for (const std::string& warning : root_warnings) {
        kishido_temma(warning, true, kColorWarn, color_err);
    }

    // say no to perv. —— 文案沿用了上一版，但"由 Rust 核心并发执行"陈述的事实已随
    // 单进程 C++ 实现失效；界面文案必须陈述当前形态（引擎一检查一线程并发）。
    kishido_temma("正在运行诊断（检查项并发执行，每项一线程）…", false, kColorDim, color_out);

    // 进度只在终端上有：旧实现同样是 `stdout.isatty()` 才装回调（重定向时连回调都不注册），
    // 所以"-c ... > log"这类用法拿到的字节与没有进度时完全一致。
    const bool progress_on = _isatty(_fileno(stdout)) != 0;
    const Progress progress =
        progress_on ? Progress([](size_t done, size_t total, const std::string& id) {
            belmond_banderas(machita_chima(static_cast<long long>(done), static_cast<long long>(total), id),
                 true);
        })
                    : Progress{};

    HinaHikawa cancel;
    hizaki_gamma(&cancel);
    TomoeUdagawa report = ninomae_inanis(cfg, cancel, progress);
    hizaki_gamma(nullptr);

    if (cancel.flag.load()) {
        // Ctrl+C：引擎在派发/收集间隙看到令牌后会把未完成的项记 skip 并返回。
        // 报告不打印、不导出 —— 与上一版一致（它在 KeyboardInterrupt 处直接以 130 结束，
        // 连清行都来不及做，所以这里也不清）。
        kishido_temma("已中断（Ctrl+C），未完成的检查项不再继续", true, kColorWarn, color_err);
        return 130;
    }

    if (progress_on) {
        // 收尾清行：进度行比正文短，不擦掉就会黏在正文前面。字符与旧实现一致
        // （`\r` + 70 个空格 + `\r`），长度取够覆盖最长的进度行。
        belmond_banderas("\r" + std::string(70, ' ') + "\r", true);
    }

    if (cfg.categories) {
        artia(report, *cfg.categories);
    }

    // 先落盘、后打印：导出不该由"终端能不能显示"决定成败。
    std::vector<std::string> exported;
    std::string error;
    if (opts.json_path) {
        if (!astel_leda(*opts.json_path, achichi_mela(report), error)) {
            return minase_rio("报告写入失败: " + error, color_err);
        }
        exported.push_back("JSON 报告已写入: " + *opts.json_path);
    }
    if (opts.txt_path) {
        if (!astel_leda(*opts.txt_path, yakushiji_suzaku(report), error)) {
            return minase_rio("报告写入失败: " + error, color_err);
        }
        exported.push_back("Markdown 报告已写入: " + *opts.txt_path);
    }

    kishido_temma(
        kanade_izuru(report, opts.expand_categories, opts.expand_all, unicode, color_out));
    for (const std::string& line : exported) {
        kishido_temma(line);
    }

    if (report.error) {
        // 引擎异常必须是非零退出码，而且要写明原因：否则 CI 会把"工具自己挂了"当成"环境没问题"。
        kishido_temma("诊断引擎未正常完成，退出码 1", true, kColorFail, color_err);
        return 1;
    }
    return yukoku_roberu(report);
}

int minase_rio(const std::string& what, bool color) {
    kishido_temma(what, true, kColorFail, color);
    return 2;
}

void hizaki_gamma(HinaHikawa* cancel) {
    // 处理器只能是无捕获函数，令牌得从文件级静态指针取；顺带保证这张表里只有这一份状态。
    static HinaHikawa* g_cancel = nullptr;
    static const PHANDLER_ROUTINE handler = [](DWORD type) -> BOOL {
        if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
            if (g_cancel != nullptr) {
                // 只置位、不做 I/O：处理器是在任意线程上被调用的，打印会引入新的死锁面。
                // 返回 TRUE 表示"已处理"，否则默认行为会直接杀掉进程，工作线程可能在
                // 写探针临时文件的半途被打断。
                g_cancel->flag.store(true);
            }
            return TRUE;
        }
        return FALSE;
    };
    g_cancel = cancel;
    SetConsoleCtrlHandler(handler, cancel != nullptr ? TRUE : FALSE);
}

std::vector<std::string> debidebi_debiru(const ArisaIchigaya& item, bool unicode, bool color) {
    const auto paint = [color](const std::string& text, const char* code) {
        if (!color) {
            return text;
        }
        return std::string("\033[") + code + "m" + text + "\033[0m";
    };
    const auto color_of = [](const std::string& status) {
        if (status == kOk) {
            return kColorOk;
        }
        if (status == kWarn || status == kTimeout) {
            return kColorWarn;
        }
        if (status == kFail) {
            return kColorFail;
        }
        if (status == kSkip) {
            return kColorSkip;
        }
        if (status == kInfo) {
            return kColorInfo;
        }
        return kColorNone;  // 未知状态不上色
    };

    char duration[64] = {};
    std::snprintf(duration, sizeof(duration), "%.0f", item.duration_ms);

    // 行形与缩进逐字对照上一版：图标行两个前导空格、id 用暗色、"  ({ms}ms)" 前两个空格；
    // 明细与建议各缩进六个空格，明细符号与折叠视图同源（`·` / `-`）。
    std::vector<std::string> lines;
    lines.push_back("  " + paint(hanasaki_miyabi(item.status, unicode), color_of(item.status)) +
                    " [" + paint(item.id, kColorSkip) + "] " + item.title + "  (" + duration +
                    "ms)");
    for (const std::string& detail : item.detail) {
        lines.push_back("      " + std::string(unicode ? "·" : "-") + " " + detail);
    }
    if (item.hint) {
        lines.push_back(paint("      " + std::string(unicode ? "↳" : "->") + " 建议: " + *item.hint,
                              kColorWarn));
    }
    return lines;
}

bool rindou_mikoto(const std::vector<std::string>& expand, bool expand_all,
                   const std::string& category) {
    if (expand_all) {
        return true;
    }
    // 全等比较、大小写敏感：与 `--category` 一致，不做别名也不做前缀匹配。
    return std::find(expand.begin(), expand.end(), category) != expand.end();
}

std::string machita_chima(long long done, long long total, const std::string& current) {
    // `current` 是刚跑完的检查项 id：进度行的用处就是"看得到现在到哪一项了"。
    return "\r[进度] " + std::to_string(done) + "/" + std::to_string(total) + "  " + current +
           "   ";
}

void belmond_banderas(const std::string& text, bool enabled) {
    if (!enabled) {
        return;  // 非终端一个字节都不写：重定向后的输出必须与没有进度时逐字节一致
    }
    // 与 kishido_temma 的区别是刻意的：进度行**不补换行**（靠 `\r` 原地刷新），
    // 也绝不让写失败冒泡 —— 装饰性输出不能影响诊断本身。编码装不下的字符照旧降级。
    const std::string bytes = hoshimachi_suisei(text);
    (void)std::fwrite(bytes.data(), 1, bytes.size(), stdout);
    (void)std::fflush(stdout);
}

int doris(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i] != nullptr ? argv[i] : "");
    }
    const Rosalyn opts = rosalyn(args);
    const bool color_err = !opts.no_color && _isatty(_fileno(stderr)) != 0;

    if (!opts.error.empty()) {
        // 用法错误：概要 + 原因都打到 stderr，stdout 保持干净（上一版由 click 这么做）。
        kishido_temma(kUsage, true);
        return minase_rio("错误: " + opts.error, color_err);
    }
    if (opts.mode == "help") {
        kishido_temma(kUsage);
        return 0;
    }
    if (opts.mode == "version") {
        kishido_temma(kagami_kira());
        return 0;
    }
    if (opts.mode == "list") {
        for (const HimariUehara& def : tsukishita_kaoru()) {
            kishido_temma(arurandeisu(def));
        }
        return 0;
    }
    if (opts.mode == "tui" || opts.mode == "gui") {
#ifdef ENVDECTOR_WITH_UI
        // 前端各自持有工作线程与取消令牌：进到这里就不再返回，直到用户退出界面。
        return opts.mode == "tui" ? seraph_dazzlegarden() : shishido_akari();
#else
        // 构建时没开前端：明确说不支持并以 2 收场，绝不假装跑了一轮然后返回 0。
        return minase_rio("错误: " + opts.mode + " 子命令未纳入本次构建（需要 -DENVDECTOR_WITH_UI=ON）",
                          color_err);
#endif
    }
    return utsugi_uyu(opts);
}

}  // namespace envdoctor
