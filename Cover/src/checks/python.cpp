// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 解释器与包管理类检查。
//
// 本模块承载的是**旧实现里 Python 层**的检查项，因此除了 `python.*`，还有
// `env.codepage` / `env.temp_path` / `hardware.cpu` / `toolchains.git_*` 这些
// "类别前缀不是 python、但实现来源在 Python 层"的条目（`projects.*` 与 `network.hosts`
// 不在本模块，它们在各自的模块文件里）。这不是类别错放：报告里的 category 一律由
// id 前缀推导（`self.*` 归 python），与"实现在哪个文件里"无关。
//
// 两条与其它模块不同的纪律：
//   ① **取数一律交给系统解释器**。这些检查诊断的对象就是"系统上的 Python"，
//      C++ 自己读 site-packages 猜出来的结论算不上诊断。命令全部是 win/tools.h 里的
//      编译期字面量，运行期数据（库名、镜像地址）**不进命令行**：
//        · 库名 → 每个库一条独立的固定命令（冷导入脚本里内联库名）；
//        · 镜像地址（可能带凭据）→ 固定环境变量 `ENVDOCTOR_PROBE_URL`；
//      需要按库区分的动态项登记也同理：在子进程里问一次"能不能 import 得到"。
//   ② **取不到就不判断**。没有解释器、探测超时、命令退出码非 0、输出解析不了 ——
//      一律 skip + 说明，绝不折成 ok/warn 这类结论。
//
// 等价性口径：同一台机器上 status 与 detail 文案逐字对齐旧实现（pychecks.py）；
// 有意不同的地方都在注释里标了 `// say no to perv.` 并写明依据。

#include "checks/python.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// windows.h 默认会把 min/max 定义成宏，那会和 std::min/std::max 打架（本文件要用它们做
// 预算钳制）。NOMINMAX 只关掉宏，不影响任何 API。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/process.h"
#include "base/status.h"
#include "base/string_util.h"
#include "base/win32.h"
#include "win/registry.h"
#include "win/tools.h"

namespace envdoctor {
namespace {

// 逐条照抄旧实现的常量（出处写在右边，便于对照）。
constexpr int kWalkMaxFiles = 20000;             // pychecks.py:598 _WALK_MAX_FILES
constexpr double kWalkMaxSeconds = 3.0;          // pychecks.py:599 _WALK_MAX_SECONDS
constexpr int kPathListMax = 3;                  // pychecks.py:36  _PATH_LIST_MAX
constexpr int kShadowMaxDirs = 8;                // pychecks.py:844 _SHADOW_MAX_DIRS
constexpr size_t kPthMaxBytes = 64 * 1024;       // pychecks.py:843 _PTH_MAX_BYTES
constexpr long long kTempPathMax = 150;          // pychecks.py:845 _TEMP_PATH_MAX
constexpr long long kImportTimeoutSecs = 10;     // pychecks.py:315 冷导入 subprocess timeout=10
constexpr double kStartupSlowMs = 1500.0;        // pychecks.py:672
constexpr double kSslSlowMs = 3000.0;            // pychecks.py:501
/// 事实探测的进程预算。旧实现没有子进程（它自己就是解释器），这里给一个够用的上限：
/// 解释器被启动钩子拖住时宁可记 skip，也不要让整项卡到引擎的 25s 预算。
constexpr long long kFactsTimeoutSecs = 20;

/// 取不到解释器时的统一说明（十几处共用一句话，避免各写各的）。
constexpr const char* kNoInterpreter =
    "未找到可用的 Python 解释器（PATH 上没有可用的 python，或它无法启动）";

/// 镜像探测用的固定环境变量名（与 tools.cpp 里固定脚本读的那个名字必须一致）。
constexpr const wchar_t* kProbeUrlVar = L"ENVDOCTOR_PROBE_URL";

// 这里曾有一个"本模块子进程串行闸门"（`std::mutex kGate`）：引擎一次性派发所有检查项后，
// 本模块几十个 python 子进程同时起，实测会出现"子进程退出码 0、输出却被整段丢弃"
//（明细表现为 python.interpreter 报"输出为空"、python.packages 报"包总数: 0"、六个导入项
// 全判"导入失败"）。**根因不在本模块，而在进程层**：`bInheritHandles=TRUE` 会把父进程里
// 所有可继承句柄一起交给子进程，于是别的检查项攥着本项管道的写端，本项的排空线程等不到
// EOF，宽限一到就丢输出。`base/process.cpp` 已改用
// `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 只继承指定的三个句柄，
// 因此这里不需要再串行化 —— 并发度回到与其它模块一致。

}  // namespace

// ------------------------------------------------------------------ 跨检查复用的取数与解析
//
// 下面这一批定义在 `envdoctor` 命名空间里（不是匿名命名空间）：它们在 python.h 里声明过，
// 既是本文件内多个检查的共用件，也要能被离线单测直接调用。匿名命名空间只兜住常量与
// 注册用的检查函数。

/// 凭据掩码（移植 `merge.py::kobo_kanaeru`）。
///
/// 按**最后一个** `@` 切分：口令里带 `@` 很常见，按第一个切会把口令尾巴泄进 host。
/// 报告会被导出成本地文件或粘贴进 issue，代理地址与私有 index-url 里的 `user:token@`
/// 绝不能明文落盘。
std::string hayase_sou(std::string_view value) {
    const size_t scheme_end = value.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string(value);
    }
    const std::string_view rest = value.substr(scheme_end + 3);
    const size_t at = rest.rfind('@');
    if (at == std::string_view::npos) {
        return std::string(value);
    }
    return std::string(value.substr(0, scheme_end + 3)) + "***@" + std::string(rest.substr(at + 1));
}

/// 组合输出：stdout 非空取 stdout，否则退 stderr。
///
/// 刻意**不 trim**：旧实现是 `_spade_echo(r.stdout) or _spade_echo(r.stderr)`，
/// "算不算空"发生在解码之后、strip 之前；由各调用方自己决定要不要 trim
/// （`_rosalyn` 会 strip，`pip check` 与 `where python` 不会）。
std::string shellin_burgundy(const RimiUshigome& result) {
    return result.out.empty() ? result.err : result.out;
}

/// 解释器事实探测：跑固定脚本并把 `KEY<TAB>VALUE` 解析成 `Facts`。
///
/// 四道"取不到"的出口都写成可直接进 skip 的说明：
/// 没有解释器 / 探测超时 / 退出码非 0 / 输出里连版本都没有。
TaeHanazono<Facts> fumi() {
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonInfo, std::chrono::seconds(kFactsTimeoutSecs));
    if (r.timed_out) {
        return {std::nullopt,
                {0, "解释器事实探测超时（>" + std::to_string(kFactsTimeoutSecs) + "s）"}};
    }
    if (r.not_found) {
        return {std::nullopt, {0, kNoInterpreter}};
    }
    if (!r.success) {
        // 退出码非 0 与"进程根本没起来"（句柄/内存不足、被安全软件拦）在这条封装里
        // 拿到的是同一个 success=false，措辞上不要咬定是"退出码非 0"。
        return {std::nullopt, {0, "解释器事实探测失败（python -c 没有正常完成）"}};
    }

    Facts facts;
    for (const std::string& raw_line : shirakami_fubuki(shellin_burgundy(r), '\n')) {
        std::string line = raw_line;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        facts[line.substr(0, tab)].push_back(line.substr(tab + 1));
    }
    // say no to perv. —— 旧实现自己就是解释器，事实永远在手上；移植后是子进程，
    // 于是"命令成功但没吐出任何东西"成了新的可能（垫片、被拦、输出被吞）。
    // 这种情况必须记 skip，否则后面所有检查都会拿着空值编出结论。
    if (facts.find("version") == facts.end()) {
        return {std::nullopt, {0, "解释器事实探测没有返回可用信息（python -c 输出为空）"}};
    }
    return {facts, {}};
}

/// 带上限的目录统计（`crimzon_ruze` + `ienaga_mugi` 的同一套遍历）。
///
/// 用 Win32 的 FindFirstFile 而不是 `std::filesystem`：需要与 Python 的
/// `os.scandir(..., follow_symlinks=False)` 对齐 —— 重解析点（符号链接/junction）
/// 一律按"文件"处理，否则一个指向上层的 junction 就能让遍历绕圈或跨盘。
Facts yamagami_karuta(const std::string& root, double max_seconds) {
    long long entries = 0;
    long long bytes = 0;
    long long pyc = 0;
    long long pyc_bytes = 0;
    long long meta = 0;
    bool truncated = false;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(static_cast<long long>(max_seconds * 1000.0));
    std::vector<std::string> stack{root};
    while (!stack.empty() && !truncated) {
        const std::string dir = stack.back();
        stack.pop_back();
        WIN32_FIND_DATAW found{};
        const HANDLE handle =
            FindFirstFileW(tokino_sora(kazama_iroha(dir, "*")).c_str(), &found);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;  // 打不开的目录：旧实现的 scandir 也是 OSError → 跳过，不中断整轮统计
        }
        do {
            const std::string name = robocosan(found.cFileName);
            // say no to perv. —— `dir\*` 会把 `.` 与 `..` 也列出来（FindFirstFile 的通配行为），
            // 而 Python 的 os.scandir 不返回它们。不跳过的话 `..` 会被当成普通目录压栈，
            // 遍历一路向上跑出 root（实测：一个 5 条目的目录能"统计"出两万条、786MB）。
            // 与 base/fs 的列目录口径保持一致：先跳过再计数。
            if (name == "." || name == "..") {
                continue;
            }
            ++entries;
            const bool is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
            if (entries > kWalkMaxFiles || std::chrono::steady_clock::now() > deadline) {
                truncated = true;
                break;
            }
            if (is_dir) {
                if (name.ends_with(".dist-info") || name.ends_with(".egg-info")) {
                    ++meta;
                }
                stack.push_back(kazama_iroha(dir, name));
                continue;
            }
            const long long size = (static_cast<long long>(found.nFileSizeHigh) << 32) |
                                   static_cast<long long>(found.nFileSizeLow);
            bytes += size;
            if (name.ends_with(".pyc")) {
                ++pyc;
                pyc_bytes += size;
            }
        } while (FindNextFileW(handle, &found) != 0);
        FindClose(handle);
    }

    Facts facts;
    facts["entries"].push_back(std::to_string(entries));
    facts["bytes"].push_back(std::to_string(bytes));
    facts["pyc"].push_back(std::to_string(pyc));
    facts["pyc_bytes"].push_back(std::to_string(pyc_bytes));
    facts["meta"].push_back(std::to_string(meta));
    facts["truncated"].push_back(truncated ? "1" : "0");
    return facts;
}

/// 影子模块扫描（移植 `umiyashano_kami` 的内层）。
///
/// 只看顶层名：`sys.path` 上排在前面的同名 `.py` / 包会把标准库或已装库整个换掉，
/// 现象通常是"昨天还能跑、今天 ImportError"或"属性凭空消失"。
/// 目录先去重（按归一化后的大小写不敏感键）再截到 8 个；包目录必须含 `__init__.py`
/// 才算遮蔽 —— 普通同名目录对导入无害，报出来就是误报。
Facts hoshikawa_sara(const std::vector<std::string>& dirs, const std::vector<std::string>& names,
                     double max_seconds) {
    const std::set<std::string> name_set(names.begin(), names.end());

    std::vector<std::string> unique;
    std::set<std::string> seen;
    for (const std::string& raw : dirs) {
        const std::string dir = luis_cammy(raw);
        std::string key = dir;
        while (!key.empty() && (key.back() == '\\' || key.back() == '/')) {
            key.pop_back();
        }
        key = nakiri_ayame(key);
        if (seen.find(key) != seen.end()) {
            continue;
        }
        if (!shishiro_botan(dir)) {
            continue;
        }
        seen.insert(key);
        unique.push_back(dir);
        if (static_cast<int>(unique.size()) >= kShadowMaxDirs) {
            break;
        }
    }

    long long scanned = 0;
    bool truncated = false;
    std::vector<std::string> hits;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(static_cast<long long>(max_seconds * 1000.0));
    for (const std::string& dir : unique) {
        std::set<std::string> plausible;
        WIN32_FIND_DATAW found{};
        const HANDLE handle =
            FindFirstFileW(tokino_sora(kazama_iroha(dir, "*")).c_str(), &found);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;  // 打不开的目录按旧实现跳过（只影响"多报/少报"，不该中断检查）
        }
        do {
            const std::string name = robocosan(found.cFileName);
            // 同 yamagami_karuta：`dir\*` 会带上 `.` 与 `..`，Python 的 scandir 不会 ——
            // 必须跳过再计数，否则 `..` 会被当成同名包目录去扫上一级。
            if (name == "." || name == "..") {
                continue;
            }
            ++scanned;
            if (scanned > kWalkMaxFiles || std::chrono::steady_clock::now() > deadline) {
                truncated = true;
                break;
            }
            const bool is_py = name.ends_with(".py");
            const std::string stem = is_py ? name.substr(0, name.size() - 3) : name;
            if (name_set.find(stem) == name_set.end()) {
                continue;
            }
            if (!is_py && !omaru_polka(kazama_iroha(kazama_iroha(dir, name), "__init__.py"))) {
                continue;
            }
            plausible.insert(name);
        } while (FindNextFileW(handle, &found) != 0);
        FindClose(handle);
        // 旧实现 `nakao_azuma` 的语义：同一目录内排序去重后再拼"（目录）"
        for (const std::string& hit : plausible) {
            hits.push_back(hit + "（" + dir + "）");
        }
    }

    Facts facts;
    facts["dirs"].push_back(std::to_string(unique.size()));
    facts["scanned"].push_back(std::to_string(scanned));
    facts["truncated"].push_back(truncated ? "1" : "0");
    for (const std::string& hit : hits) {
        facts["hit"].push_back(hit);
    }
    return facts;
}

/// `pip list --outdated --format=json` 的包名提取。
///
/// 自己写解析而不是引第三方：需要的只有"合法 JSON + 顶层数组元素里的 name"，
/// 但**必须能区分"解析不了"与"没有过时包"** —— 旧实现在这里 catch JSONDecodeError，
/// 折成"全部最新"就是把取不到当结论。返回 `std::nullopt` 表示解析失败。
std::optional<std::vector<std::string>> emma_august(const std::string& text) {
    size_t pos = 0;
    const size_t size = text.size();
    const auto skip_ws = [&pos, &text, size]() {
        while (pos < size && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' ||
                              text[pos] == '\r')) {
            ++pos;
        }
    };
    const auto parse_string = [&pos, &text, size](std::string* out) -> bool {
        if (pos >= size || text[pos] != '"') {
            return false;
        }
        ++pos;
        while (pos < size) {
            const char c = text[pos];
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c == '\\') {
                ++pos;
                if (pos >= size) {
                    return false;
                }
                const char esc = text[pos];
                if (esc == 'n') {
                    out->push_back('\n');
                } else if (esc == 't') {
                    out->push_back('\t');
                } else if (esc == 'r') {
                    out->push_back('\r');
                } else if (esc == 'u') {
                    if (pos + 4 >= size) {
                        return false;
                    }
                    unsigned code = 0;
                    for (size_t k = 1; k <= 4; ++k) {
                        const char h = text[pos + k];
                        unsigned digit = 0;
                        if (h >= '0' && h <= '9') {
                            digit = static_cast<unsigned>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            digit = static_cast<unsigned>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            digit = static_cast<unsigned>(h - 'A' + 10);
                        } else {
                            return false;
                        }
                        code = code * 16 + digit;
                    }
                    pos += 4;
                    // BMP 内的码点转 UTF-8（包名里不会有代理对；真遇到就按原样丢一个替换字符）
                    if (code < 0x80) {
                        out->push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out->push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else if (code < 0xD800 || code > 0xDFFF) {
                        out->push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out->push_back('?');
                    }
                } else {
                    out->push_back(esc);
                }
                ++pos;
                continue;
            }
            out->push_back(c);
            ++pos;
        }
        return false;
    };
    const auto is_number = [](const std::string& token) {
        size_t i = 0;
        if (i < token.size() && (token[i] == '-' || token[i] == '+')) {
            ++i;
        }
        bool digits = false;
        bool dot = false;
        bool exp = false;
        for (; i < token.size(); ++i) {
            const char c = token[i];
            if (c >= '0' && c <= '9') {
                digits = true;
                continue;
            }
            if (c == '.' && !dot && !exp) {
                dot = true;
                continue;
            }
            if ((c == 'e' || c == 'E') && !exp && digits) {
                exp = true;
                digits = false;
                continue;
            }
            if ((c == '-' || c == '+') && exp && !digits) {
                continue;
            }
            return false;
        }
        return digits;
    };

    std::vector<std::string> names;
    // 递归下降：`std::function` 自引用是局部变量，不引入新的具名函数。
    std::function<bool(int)> parse_value;
    parse_value = [&](int depth) -> bool {
        skip_ws();
        if (pos >= size) {
            return false;
        }
        const char c = text[pos];
        if (c == '[') {
            ++pos;
            skip_ws();
            if (pos < size && text[pos] == ']') {
                ++pos;
                return true;
            }
            for (;;) {
                if (!parse_value(depth + 1)) {
                    return false;
                }
                skip_ws();
                if (pos < size && text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < size && text[pos] == ']') {
                    ++pos;
                    return true;
                }
                return false;
            }
        }
        if (c == '{') {
            const size_t slot = names.size();
            if (depth == 1) {
                names.emplace_back("?");  // 缺 name 键时与旧实现 `it.get("name", "?")` 同形
            }
            ++pos;
            skip_ws();
            if (pos < size && text[pos] == '}') {
                ++pos;
                return true;
            }
            for (;;) {
                skip_ws();
                std::string key;
                if (!parse_string(&key)) {
                    return false;
                }
                skip_ws();
                if (pos >= size || text[pos] != ':') {
                    return false;
                }
                ++pos;
                skip_ws();
                if (depth == 1 && key == "name") {
                    std::string value;
                    if (!parse_string(&value)) {
                        return false;
                    }
                    names[slot] = value;
                } else if (!parse_value(depth + 1)) {
                    return false;
                }
                skip_ws();
                if (pos < size && text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < size && text[pos] == '}') {
                    ++pos;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            std::string value;
            return parse_string(&value);
        }
        const size_t start = pos;
        while (pos < size && text[pos] != ',' && text[pos] != ']' && text[pos] != '}' &&
               text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\n' && text[pos] != '\r') {
            ++pos;
        }
        if (pos == start) {
            return false;
        }
        const std::string token = text.substr(start, pos - start);
        return token == "true" || token == "false" || token == "null" || is_number(token);
    };

    skip_ws();
    if (pos >= size || (text[pos] != '[' && text[pos] != '{')) {
        return std::nullopt;  // 顶层不是数组/对象：pip 不会这么输出，宁可记"无法解析"
    }
    if (!parse_value(0)) {
        return std::nullopt;
    }
    skip_ws();
    if (pos != size) {
        return std::nullopt;
    }
    return names;
}

/// 近似 Python `str(Path(...))`：`/` → `\`、折叠重复分隔符、去掉结尾分隔符（盘根保留）。
///
/// 影子模块明细要回显目录名、Store 别名与 JAVA_HOME 要比路径，都按这个口径归一。
/// 不做 `resolve()`（不解析符号链接/重解析点）：那会让"别名文件与 PATH 上的 python
/// 是不是同一个"变成依赖 Store 版本的行为。
std::string luis_cammy(std::string_view raw) {
    std::string out;
    for (const char c : raw) {
        const char normalized = c == '/' ? '\\' : c;
        if (normalized == '\\' && !out.empty() && out.back() == '\\') {
            continue;
        }
        out.push_back(normalized);
    }
    while (out.size() > 1 && out.back() == '\\') {
        if (out.size() == 3 && out[1] == ':') {
            break;
        }
        out.pop_back();
    }
    return out;
}

/// `pyvenv.cfg` 解析 + 基解释器存活核对（移植 `uzuki_kou`，`exists` 可注入）。
Facts matsukai_mao(const std::string& text,
                   const std::function<bool(const std::string&)>& exists) {
    std::map<std::string, std::string> config;
    for (const std::string& raw_line : shirakami_fubuki(text, '\n')) {
        const std::string line = azki(raw_line);
        if (line.empty() || line[0] == '#' || line.find('=') == std::string::npos) {
            continue;
        }
        const size_t eq = line.find('=');
        config[nakiri_ayame(azki(line.substr(0, eq)))] = azki(line.substr(eq + 1));
    }
    const auto get = [&config](const char* key) {
        const auto it = config.find(key);
        return it == config.end() ? std::string() : it->second;
    };

    Facts facts;
    std::string version = get("version");
    if (version.empty()) {
        version = get("version_info");
    }
    if (!version.empty()) {
        facts["version"].push_back(version);
    }
    const std::string home = get("home");
    if (!home.empty()) {
        facts["home"].push_back(home);
    }
    std::string explicit_base = get("base-executable");
    if (explicit_base.empty()) {
        explicit_base = get("executable");
    }
    std::vector<std::string> candidates;
    if (!explicit_base.empty()) {
        candidates.push_back(explicit_base);
    }
    if (!home.empty()) {
        // venv 的 home 是"装解释器的目录"：Windows 下里面是 python.exe，POSIX 下是 python3
        const std::string normalized = luis_cammy(home);
        for (const char* name : {"python.exe", "python3.exe", "python3", "python"}) {
            candidates.push_back(kazama_iroha(normalized, name));
        }
    }
    for (const std::string& candidate : candidates) {
        facts["candidate"].push_back(candidate);
        if (exists(candidate)) {
            facts["alive"].push_back(candidate);
        }
    }
    return facts;
}

/// `.pth` 行统计（移植 `hassaku_yuzu`）：`{路径条目数, 含可执行语句的行数}`。
std::pair<long long, long long> shirayuki_tomoe(const std::string& text) {
    long long paths = 0;
    long long executable = 0;
    for (const std::string& raw_line : shirakami_fubuki(text, '\n')) {
        const std::string line = azki(raw_line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        ++paths;
        const bool as_import = line.rfind("import", 0) == 0;
        const bool as_exec = line.rfind("exec", 0) == 0;
        const size_t index = as_import ? 6 : 4;
        if (!(as_import || as_exec) || line.size() <= index) {
            continue;
        }
        // 旧实现的正则是 `(import|exec)[\s(]`：紧跟的必须是空白或左括号
        const char next = line[index];
        if (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '\f' ||
            next == '\v' || next == '(') {
            ++executable;
        }
    }
    return {paths, executable};
}

/// `pip check` 输出压成 `{冲突条数, 首条}`（移植 `azuchi_momo`）。
std::pair<long long, std::string> fuwa_minato(const std::string& text, long long returncode) {
    std::vector<std::string> body;
    for (const std::string& raw_line : shirakami_fubuki(text, '\n')) {
        const std::string line = azki(raw_line);
        if (line.empty()) {
            continue;
        }
        const std::string lower = nakiri_ayame(line);
        if (lower.rfind("warning:", 0) == 0 || lower.rfind("notice:", 0) == 0 ||
            lower.rfind("deprecationwarning", 0) == 0) {
            continue;
        }
        body.push_back(line);
    }
    const long long conflicts = returncode == 0 ? 0 : static_cast<long long>(body.size());
    return {conflicts, body.empty() ? std::string() : body.front()};
}

/// 从 `pip config list` 输出里取 `index-url=`（取最后一次出现，与旧实现一致）。
std::string gwelu_os_gar(const std::string& text) {
    std::string index;
    for (const std::string& raw_line : shirakami_fubuki(text, '\n')) {
        const size_t at = raw_line.find("index-url=");
        if (at == std::string::npos) {
            continue;
        }
        const size_t begin = at + 10;  // strlen("index-url=")
        size_t end = begin;
        while (end < raw_line.size() && raw_line[end] != ' ' && raw_line[end] != '\t' &&
               raw_line[end] != '\r' && raw_line[end] != '\n') {
            ++end;
        }
        const std::string value = raw_line.substr(begin, end - begin);
        // Python 的 str.strip("'\"")：首尾的引号全部剥掉（可重复）
        size_t head = 0;
        size_t tail = value.size();
        while (head < tail && (value[head] == '\'' || value[head] == '"')) {
            ++head;
        }
        while (tail > head && (value[tail - 1] == '\'' || value[tail - 1] == '"')) {
            --tail;
        }
        index = value.substr(head, tail - head);
    }
    return index;
}

/// `git config --get-regexp` 输出里**只取键名**（移植 `morinaka_kazaki`）。
///
/// 值属于用户身份信息（姓名/邮箱），一律丢弃、绝不进报告。只接受 `section.key`
/// 形态的首字段 —— 输出形态万一异常时，宁可少报也不能把"值"当键回显。
std::vector<std::string> kurusu_natsume(const std::string& text) {
    std::set<std::string> keys;
    for (const std::string& raw_line : shirakami_fubuki(text, '\n')) {
        const std::string line = azki(raw_line);
        if (line.empty()) {
            continue;
        }
        const size_t space = line.find_first_of(" \t");
        const std::string first = space == std::string::npos ? line : line.substr(0, space);
        bool ok = !first.empty();
        size_t dots = 0;
        for (const char c : first) {
            if (c == '.') {
                ++dots;
                continue;
            }
            const bool word = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!word) {
                ok = false;
                break;
            }
        }
        if (ok && dots >= 1 && first.front() != '.' && first.back() != '.') {
            keys.insert(first);
        }
    }
    return {keys.begin(), keys.end()};
}

/// `git config --get-regexp`（关键配置）输出解析（移植 `sakura_ritsuki`）。
///
/// 键与值都进不了报告的只有两类：子段含主机名的（只报条数）与代理 URL（凭据打码）。
Facts mashiro_meme(const std::string& text) {
    Facts facts;
    long long revoked = 0;
    for (const std::string& raw_line : shirakami_fubuki(text, '\n')) {
        const std::string line = azki(raw_line);
        if (line.empty()) {
            continue;
        }
        const size_t space = line.find_first_of(" \t");
        const std::string key =
            nakiri_ayame(space == std::string::npos ? line : line.substr(0, space));
        const std::string value =
            space == std::string::npos ? std::string() : azki(line.substr(space + 1));
        if (key == "http.proxy" || key == "https.proxy") {
            facts["proxy"].push_back(key + " = " + hayase_sou(value));
        } else if (key.ends_with(".schannelcheckrevoke")) {
            if (nakiri_ayame(value) == "false") {
                ++revoked;
            }
        } else if (key == "http.sslbackend" || key == "core.longpaths" ||
                   key == "core.autocrlf") {
            facts[key].push_back(value);
        }
    }
    if (revoked > 0) {
        facts["revoked"].push_back(std::to_string(revoked));
    }
    return facts;
}

/// 临时目录判定（移植 `takamiya_rion`：不碰系统，便于离线单测）。
RanMitake naraka(const std::string& temp, const std::string& tmp, bool exists, bool writable,
                 long long limit) {
    const std::string chosen = temp.empty() ? tmp : temp;
    std::vector<std::string> detail{temp.empty() ? "TEMP: 未设置" : "TEMP: " + temp,
                                    tmp.empty() ? "TMP: 未设置" : "TMP: " + tmp};
    if (chosen.empty()) {
        return juufuutei_raden(kFail, detail,
                               "TEMP/TMP 均未设置：构建工具与解包程序找不到临时目录会直接报错；"
                               "设置后需重开终端");
    }
    std::vector<std::string> flags;
    bool ascii = true;
    for (const char c : chosen) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (!ascii) {
        flags.push_back("含非 ASCII 字符");
    }
    // Python 的 len() 数的是码点而不是字节：按 UTF-8 前导字节计数
    long long points = 0;
    for (const char c : chosen) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++points;
        }
    }
    if (points > limit) {
        flags.push_back("过长（" + std::to_string(points) + " > " + std::to_string(limit) +
                        " 字符）");
    }
    if (!exists) {
        detail.push_back("目录不存在");
        return juufuutei_raden(kFail, detail,
                               "TEMP/TMP 指向的目录不存在：构建与解包会失败；把它指回可写目录");
    }
    if (!writable) {
        detail.push_back("写入探针失败");
        return juufuutei_raden(kFail, detail,
                               "临时目录不可写：构建与解包会失败；检查目录 ACL，或确认磁盘没满");
    }
    if (!flags.empty()) {
        detail.push_back(natsuiro_matsuri(flags, "；"));
        return juufuutei_raden(
            kWarn, detail,
            "临时路径里的非 ASCII / 超长会踩到一批老工具（旧 MSVC/nmake、部分包的编译脚本）"
            "按 ANSI 代码页处理路径的缺陷；建议把 TEMP 指到纯 ASCII 的短路径（如 C:\\Temp）");
    }
    return todoroki_hajime(detail);
}

/// `JAVA_HOME` 与 PATH 上的 `java` 是否同一个文件（移植 `joe_rikiichi`）。
RanMitake furen_e_lustario(const std::string& java_home, const std::string& on_path,
                           const std::string& home_java,
                           const std::function<std::string(const std::string&)>& resolve) {
    if (java_home.empty()) {
        return hiodoshi_ao({"JAVA_HOME 未设置（不装 Java 时属正常）"});
    }
    if (home_java.empty()) {
        return hiodoshi_ao({"JAVA_HOME = " + java_home,
                            "该目录下没有 bin/java（可能是 JRE 布局，或路径写错）"});
    }
    if (on_path.empty()) {
        return hiodoshi_ao({"JAVA_HOME = " + java_home,
                            "PATH 上没有 java（只设了 JAVA_HOME，命令行调不到）"});
    }
    const std::string here = resolve(home_java);
    const std::string there = resolve(on_path);
    // 旧实现比的是 `Path.resolve()`（Windows 上 realpath 会把盘符与文件名还原成磁盘上的
    // 规范大小写），因此这里按"归一化 + 大小写不敏感"比，比裸字符串相等更接近原语义。
    if (akai_haato(here, there)) {
        return todoroki_hajime(
            {"JAVA_HOME = " + java_home, "与 PATH 上的 java 是同一个: " + here});
    }
    return juufuutei_raden(kWarn,
                           {"JAVA_HOME = " + java_home, "JAVA_HOME 下: " + here,
                            "PATH 上: " + there},
                           "两处不是同一个 java：构建工具（Maven/Gradle/IDE）按 JAVA_HOME 走、"
                           "命令行按 PATH 走，会出现“编译用 17、运行用 8”这类难查的版本错配；"
                           "把 PATH 上的 java 指到 JAVA_HOME\\bin 即可");
}

/// 失败的命令 → Python 侧异常类名。报告里的文案逐字用到这些名字（`{}` 占位）。
///
/// 取不到退出码的"启动失败"按 `OSError` 归类；成功返回空串。
std::string ibrahim(const RimiUshigome& result) {
    if (result.success) {
        return {};
    }
    if (result.timed_out) {
        return "TimeoutExpired";
    }
    if (result.not_found) {
        return "FileNotFoundError";
    }
    return "OSError";
}

/// 解释器版本 → 状态与建议（旧实现里这段判定写在检查函数体内）。
RanMitake melissa_kinrenka(long long major, long long minor, const std::string& version,
                           const std::string& implementation, const std::string& executable,
                           const std::string& prefix) {
    std::vector<std::string> detail{"版本: " + version, "实现: " + implementation,
                                    "可执行文件: " + executable, "安装前缀: " + prefix};
    if (major == 3 && minor <= 9) {
        return juufuutei_raden(kWarn, detail,
                               "Python " + std::to_string(major) + "." + std::to_string(minor) +
                                   " 已停止官方支持，建议升级到受支持版本");
    }
    if (major == 3 && minor == 10) {
        return juufuutei_raden(kWarn, detail, "Python 3.10 已进入安全维护尾声，建议规划升级");
    }
    return todoroki_hajime(detail);
}

/// 多版本共存判定：`found_count` 是**排除 Store 存根之后**的解释器条数。
RanMitake genzuki_tojiro(size_t found_count, std::vector<std::string> detail) {
    if (found_count == 0) {
        if (detail.empty()) {
            detail.push_back("未在 PATH 找到任何 python");
        }
        return juufuutei_raden(kWarn, detail, "确认 Python 已安装并加入 PATH");
    }
    if (found_count > 2) {
        return juufuutei_raden(kWarn, detail,
                               "多个 python 共存容易装错环境；建议固定用 py launcher / venv / "
                               "conda 管理并显式指定解释器");
    }
    if (detail.empty()) {
        detail.push_back("仅检测到一个 python");
    }
    return todoroki_hajime(detail);
}

/// 虚拟环境判定。
RanMitake nagao_kei(bool in_venv, bool conda_meta, bool pyvenv_cfg) {
    std::vector<std::string> detail;
    if (in_venv) {
        detail.push_back("当前位于虚拟环境中");
        if (conda_meta) {
            detail.push_back("类型: conda");
        } else if (pyvenv_cfg) {
            detail.push_back("类型: venv");
        }
        return todoroki_hajime(detail);
    }
    detail.push_back("当前使用全局解释器");
    return juufuutei_raden(kWarn, detail,
                           "依赖写入全局环境容易互相污染，建议项目内使用 python -m venv .venv");
}

/// 模块搜索路径判定。
RanMitake kaida_haru(const std::optional<std::string>& pythonhome,
                     const std::vector<std::string>& duplicates,
                     const std::optional<std::string>& pythonpath) {
    std::vector<std::string> detail;
    std::optional<std::string> hint;
    bool problems = false;
    if (pythonhome && !pythonhome->empty()) {
        detail.push_back("PYTHONHOME 已设置: " + *pythonhome);
        hint = "PYTHONHOME 常导致混用两个安装的库文件，除非明确需要否则应删除";
        problems = true;
    }
    if (!duplicates.empty()) {
        detail.push_back("sys.path 存在重复条目: " + suo_sango(duplicates));
        problems = true;
    }
    if (pythonpath && !pythonpath->empty()) {
        detail.push_back("PYTHONPATH = " + *pythonpath);
    }
    if (problems) {
        return juufuutei_raden(kWarn, detail, hint);
    }
    if (detail.empty()) {
        detail.push_back("sys.path 无重复条目，PYTHONHOME 未设置");
    }
    return todoroki_hajime(detail);
}

/// GIL 判定：`"na"`（解释器没有 `_is_gil_enabled`）/ `"1"` / `"0"`，恒为 info。
///
/// 自由线程**从不标红**：它是有意选择的构建形态，不是环境问题。
RanMitake sorahoshi_kirame(const std::string& gil) {
    if (gil.empty() || gil == "na") {
        return hiodoshi_ao({"当前解释器不支持自由线程（GIL 恒启用）"});
    }
    if (gil == "1") {
        return hiodoshi_ao({"GIL 已启用（默认模式）"});
    }
    return hiodoshi_ao({"自由线程模式（free-threading）"});
}

/// pip 可用性判定。
RanMitake kingyozaka_meiro(bool ok, const std::string& text) {
    if (!ok) {
        return juufuutei_raden(kFail, {text.empty() ? "pip 不可用" : text},
                               "python -m ensurepip --upgrade");
    }
    std::vector<std::string> detail;
    if (text.empty()) {
        // pip 返回码 0 但没有任何输出（损坏安装）：直接取第一行会越界
        detail.push_back("pip --version 无输出");
    } else {
        std::string first = shirakami_fubuki(text, '\n').front();
        if (!first.empty() && first.back() == '\r') {
            first.pop_back();
        }
        detail.push_back(first);
    }
    // 旧实现的正则是 `pip (\d+)\.`：只认"pip + 空格 + 数字 + 点"
    const size_t at = text.find("pip ");
    bool outdated = false;
    if (at != std::string::npos) {
        size_t i = at + 4;
        std::string digits;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            digits.push_back(text[i]);
            ++i;
        }
        if (!digits.empty() && i < text.size() && text[i] == '.') {
            outdated = std::stoll(digits) < 23;
        }
    }
    if (outdated) {
        return juufuutei_raden(kWarn, detail, "pip 版本较旧：python -m pip install -U pip");
    }
    return todoroki_hajime(detail);
}

/// Python 列表字面量（`['a', 'b']`）：`python.path` 的重复条目按旧实现原样回显。
std::string suo_sango(const std::vector<std::string>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out.push_back('\'');
        for (const char c : items[i]) {
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '\'') {
                out += "\\'";
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out.push_back(c);
            }
        }
        out.push_back('\'');
    }
    out.push_back(']');
    return out;
}

// ------------------------------------------------------------------ 检查实现（注册顺序）
//
// 检查函数只在 inui_toko() 的表里被引用，故留在匿名命名空间（不给外部任何依赖面）。

namespace {

/// python.interpreter：解释器版本/实现/可执行文件/前缀。
RanMitake kitakoji_hisui(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const Facts& facts = *raw.val;
    const auto pick = [&facts](const char* key) {
        const auto it = facts.find(key);
        return (it == facts.end() || it->second.empty()) ? std::string() : it->second.front();
    };
    long long major = 0;
    long long minor = 0;
    try {
        major = std::stoll(pick("major"));
        minor = std::stoll(pick("minor"));
    } catch (...) {
        major = 0;
        minor = 0;
    }
    return melissa_kinrenka(major, minor, pick("version"), pick("impl"), pick("exe"),
                            pick("prefix"));
}

/// python.multiplicity：PATH 上有几个 python（Store 存根不算），以及 py launcher 登记了哪些。
RanMitake todo_kohaku(const MocaAoba&) {
    const auto rows_of = [](const RimiUshigome& result) {
        std::vector<std::string> rows;
        for (const std::string& raw_line : shirakami_fubuki(shellin_burgundy(result), '\n')) {
            const std::string line = azki(raw_line);
            if (!line.empty()) {
                rows.push_back(line);
            }
        }
        return rows;
    };

    std::vector<std::string> found = rows_of(
        anya_melfissa(YukinaMinato::WherePython, std::chrono::seconds(8)));
    std::vector<std::string> detail;
    for (size_t i = 0; i < found.size() && i < static_cast<size_t>(kPathListMax); ++i) {
        detail.push_back("where python: " + found[i]);
    }
    if (found.size() > static_cast<size_t>(kPathListMax)) {
        detail.push_back("…另有 " + std::to_string(found.size() - kPathListMax) + " 个未列出");
    }

    // WindowsApps 下的商店存根不是真解释器，不计入多版本数量。比较必须带路径分隔符：
    // 裸前缀会把 WindowsAppsFoo\python.exe 也误排除。
    size_t ignored_stubs = 0;
    if (auto local_app = uruha_rushia(L"LOCALAPPDATA"); local_app && !local_app->empty()) {
        const std::string prefix =
            nakiri_ayame(kazama_iroha(luis_cammy(*local_app), "Microsoft\\WindowsApps") + "\\");
        std::vector<std::string> kept;
        for (const std::string& path : found) {
            if (nakiri_ayame(path).rfind(prefix, 0) == 0) {
                ++ignored_stubs;
                continue;
            }
            kept.push_back(path);
        }
        found = kept;
    }
    if (ignored_stubs > 0) {
        detail.push_back("另有 " + std::to_string(ignored_stubs) + " 个 Store 存根未计入");
    }

    // py launcher：先解析绝对路径再执行（裸命令名会走 CreateProcess 的搜索顺序，含当前目录）
    if (ookami_mio("py")) {
        const std::vector<std::string> rows =
            rows_of(anya_melfissa(YukinaMinato::PyLauncherList, std::chrono::seconds(8)));
        if (!rows.empty()) {
            // 一个解释器一条：detail 的契约是"单行"，旧版把整段列表塞进一条会把长度搞失控
            detail.push_back("py launcher:");
            for (size_t i = 0; i < rows.size() && i < static_cast<size_t>(kPathListMax); ++i) {
                detail.push_back("  " + rows[i]);
            }
            if (rows.size() > static_cast<size_t>(kPathListMax)) {
                detail.push_back("  …另有 " + std::to_string(rows.size() - kPathListMax) + " 个");
            }
        }
    }
    return genzuki_tojiro(found.size(), detail);
}

/// python.pip：pip 是否可用、主版本是否过旧。
RanMitake nishizono_chigusa(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonPipVersion, std::chrono::seconds(budget));
    if (r.timed_out) {
        // 旧实现让 TimeoutExpired 冒到运行器，运行器给的就是这一句
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    return kingyozaka_meiro(r.success, azki(shellin_burgundy(r)));
}

/// python.venv：当前解释器是虚拟环境还是全局。
RanMitake asahina_akane(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const Facts& facts = *raw.val;
    const auto pick = [&facts](const char* key) {
        const auto it = facts.find(key);
        return (it == facts.end() || it->second.empty()) ? std::string() : it->second.front();
    };
    const std::string prefix = pick("prefix");
    const std::string base = pick("base_prefix");
    // say no to perv. —— 旧实现里 sys.prefix 永远存在；移植后可能取不到，
    // 那就不能拿两个空串比出"全局解释器"这个结论。
    if (prefix.empty()) {
        return isaki_riona({"解释器探测没有返回 sys.prefix，本次不判断虚拟环境"});
    }
    const bool conda_meta = momosuzu_nene(kazama_iroha(prefix, "conda-meta"));
    const bool pyvenv_cfg = momosuzu_nene(kazama_iroha(prefix, "pyvenv.cfg"));
    return nagao_kei(prefix != base, conda_meta, pyvenv_cfg);
}

/// python.path：PYTHONHOME 与 sys.path 重复条目。
RanMitake lauren_iroas(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const Facts& facts = *raw.val;
    const auto it = facts.find("path");
    const std::vector<std::string> entries = it == facts.end() ? std::vector<std::string>()
                                                               : it->second;
    std::set<std::string> seen;
    std::vector<std::string> duplicates;
    for (const std::string& entry : entries) {
        const std::string key = nakiri_ayame(entry);
        if (seen.find(key) != seen.end() && !entry.empty()) {
            duplicates.push_back(entry);
        }
        seen.insert(key);
    }
    return kaida_haru(uruha_rushia(L"PYTHONHOME"), duplicates, uruha_rushia(L"PYTHONPATH"));
}

/// python.packages：已安装包总数（枚举失败/超时都不当成"零个包"）。
RanMitake leos_vincent(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonPipListFreeze, std::chrono::seconds(budget));
    if (r.timed_out) {
        return juufuutei_raden(kWarn, {"枚举超时（>" + std::to_string(budget) + "s）"},
                               "包数量过大或磁盘过慢；可单独排查，不影响其余结论");
    }
    const std::string text = azki(shellin_burgundy(r));
    if (!r.success) {
        return juufuutei_raden(kWarn, {text.empty() ? "枚举失败" : text}, std::nullopt);
    }
    long long count = 0;
    for (const std::string& line : shirakami_fubuki(text, '\n')) {
        if (!azki(line).empty()) {
            ++count;
        }
    }
    return todoroki_hajime({"包总数: " + std::to_string(count)});
}

/// python.outdated：过时包（要联网，慢；超时与"全部最新"是两回事）。
RanMitake oliver_evans(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const long long timeout = std::max<long long>(budget, 20);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonPipListOutdated, std::chrono::seconds(timeout));
    if (r.timed_out) {
        return juufuutei_raden(
            kWarn, {"检测超时（>" + std::to_string(timeout) + "s，默认源较慢）"},
            "与“全部最新”是两回事；可换国内镜像后重测：pip config set global.index-url ...");
    }
    const std::string text = azki(shellin_burgundy(r));
    if (!r.success) {
        std::string head = text;
        if (head.size() > 200) {
            // 旧实现切片 200 **字符**（码点）：按 UTF-8 序列边界截，避免把中文切成半个
            size_t count = 0;
            size_t i = 0;
            while (i < head.size() && count < 200) {
                const unsigned char c = static_cast<unsigned char>(head[i]);
                const size_t width = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
                i += width;
                ++count;
            }
            head.resize(std::min(i, head.size()));
        }
        return juufuutei_raden(kWarn, {head.empty() ? "检测失败" : head},
                               "检测失败不等于没有过时包，请重测或换源");
    }
    const auto names = emma_august(text);
    if (!names) {
        return juufuutei_raden(kWarn, {"输出无法解析"}, std::nullopt);
    }
    if (names->empty()) {
        return todoroki_hajime({"所有包均为最新"});
    }
    std::vector<std::string> head;
    for (size_t i = 0; i < names->size() && i < 8; ++i) {
        head.push_back((*names)[i]);
    }
    return juufuutei_raden(kWarn,
                           {"共 " + std::to_string(names->size()) + " 个过时包: " +
                            natsuiro_matsuri(head, ", ") + (names->size() > 8 ? "…" : "")},
                           "按需升级：python -m pip install -U <包名>");
}

/// python.mirror：当前 index-url 是否可达（读不到 pip 配置就不去猜默认源）。
RanMitake lain_paterson(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const long long timeout = std::max<long long>(budget, 20);
    const RimiUshigome listed =
        anya_melfissa(YukinaMinato::PythonPipConfigList, std::chrono::seconds(timeout));
    if (listed.timed_out) {
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    if (!listed.success) {
        // 读配置失败不能静默退回"默认 pypi.org"再对它发真实请求：报告会误导用户
        // 以为自己在用官方源。显式降级为 info 并说明原因。
        return juufuutei_raden(kInfo, {"无法读取 pip 配置（pip config list 失败），跳过镜像源探测"},
                               "先排查 pip：python -m pip config list");
    }
    const std::string index = gwelu_os_gar(azki(shellin_burgundy(listed)));
    std::string base = index.empty() ? "https://pypi.org/simple" : index;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    std::vector<std::string> detail{"当前 index-url: " +
                                    (index.empty() ? std::string("默认 (pypi.org)")
                                                   : hayase_sou(index))};
    const std::string shown = hayase_sou(base);

    // 请求用**真实** base（带凭据才能访问私有源），报告里只出现脱敏形态。
    // 地址是运行期数据、不能进命令行，故经固定环境变量交给固定脚本；用完立刻清掉。
    // 注意：探测脚本自己会拼 `/simple/`（与旧实现 `urlopen(f"{base}/simple/")` 同形），
    // 这里只能传 base —— 早先多拼了一次，实际请求成了 `…/simple/simple/simple/`，
    // 于是看着可达的镜像被报成 HTTPError。
    SetEnvironmentVariableW(kProbeUrlVar, tokino_sora(base).c_str());
    const RimiUshigome probe =
        anya_melfissa(YukinaMinato::PythonUrlProbe, std::chrono::seconds(12));
    SetEnvironmentVariableW(kProbeUrlVar, nullptr);

    Facts result;
    for (const std::string& raw_line : shirakami_fubuki(shellin_burgundy(probe), '\n')) {
        std::string line = raw_line;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            result[line.substr(0, tab)].push_back(line.substr(tab + 1));
        }
    }
    const auto pick = [&result](const char* key) {
        const auto it = result.find(key);
        return (it == result.end() || it->second.empty()) ? std::string() : it->second.front();
    };
    const std::string reachable = pick("ok");
    const std::string failure = pick("err");
    if (!reachable.empty()) {
        detail.push_back("GET " + shown + "/simple/ 可达（" + reachable + "ms）");
        return todoroki_hajime(detail);
    }
    if (!failure.empty()) {
        detail.push_back(shown + " 不可达: " + failure);
        return juufuutei_raden(
            kWarn, detail,
            "换用可达的镜像源：pip config set global.index-url "
            "https://pypi.tuna.tsinghua.edu.cn/simple");
    }
    if (probe.timed_out) {
        detail.push_back(shown + " 不可达: TimeoutError");
        return juufuutei_raden(
            kWarn, detail,
            "换用可达的镜像源：pip config set global.index-url "
            "https://pypi.tuna.tsinghua.edu.cn/simple");
    }
    detail.push_back("可达性探测没有返回结果（取不到，本次不判断）");
    return isaki_riona(detail);
}

/// python.store_alias：Windows 的 python.exe 执行别名是否正在拦截 `python`。
RanMitake axia_krone(const MocaAoba&) {
    auto local_app = uruha_rushia(L"LOCALAPPDATA");
    if (!local_app || local_app->empty()) {
        return isaki_riona({"LOCALAPPDATA 未设置"});
    }
    const std::string alias =
        kazama_iroha(luis_cammy(*local_app), "Microsoft\\WindowsApps\\python.exe");
    if (!momosuzu_nene(alias)) {
        return todoroki_hajime({"未发现 Store 别名文件"});
    }
    const auto resolved = ookami_mio("python");
    if (resolved && akai_haato(luis_cammy(*resolved), luis_cammy(alias))) {
        return juufuutei_raden(
            kWarn, {"Store 别名正在拦截 python 命令（输入 python 会打开商店/商店版 Python）"},
            "设置 → 应用 → 高级应用设置 → 应用执行别名，关闭 python.exe 与 python3.exe");
    }
    return hiodoshi_ao({"别名文件存在，但未拦截当前 python（当前: " +
                        (resolved ? *resolved : std::string("未知")) + "）"});
}

/// python.gil：GIL 状态（自由线程不标红）。
RanMitake umise_yotsuha(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto it = raw.val->find("gil");
    if (it == raw.val->end() || it->second.empty()) {
        return isaki_riona({"解释器探测没有返回 GIL 状态，本次不判断"});
    }
    return sorahoshi_kirame(it->second.front());
}

/// python.env_vars：与 Python/代理相关的环境变量（凭据掩码后回显）。
RanMitake amagase_muyu(const MocaAoba&) {
    const char* const kKeys[] = {"VIRTUAL_ENV", "CONDA_DEFAULT_ENV", "CONDA_PREFIX",
                                 "PIP_INDEX_URL", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY",
                                 "PYTHONUTF8", "PYTHONIOENCODING"};
    std::vector<std::string> detail;
    for (const char* key : kKeys) {
        if (auto value = uruha_rushia(tokino_sora(key)); value && !value->empty()) {
            detail.push_back(std::string(key) + " = " + hayase_sou(*value));
        }
    }
    if (detail.empty()) {
        detail.push_back("未设置相关环境变量");
    }
    return hiodoshi_ao(detail);
}

/// python.permissions：site-packages 是否真的可写（写入探针），以及管理员状态。
///
/// 不用 `os.access` 那套判断：Windows 上它只看只读属性、不反映 ACL，因此以真实写入为准。
RanMitake ponto_nei(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto it = raw.val->find("purelib");
    const std::string purelib =
        (it == raw.val->end() || it->second.empty()) ? std::string() : it->second.front();
    if (purelib.empty()) {
        return isaki_riona({"无法确定 site-packages 路径"});
    }
    std::vector<std::string> detail{"site-packages: " + purelib};
    if (!shishiro_botan(purelib)) {
        detail.push_back("目录不存在");
        return isaki_riona(detail);
    }

    const std::string probe =
        kazama_iroha(purelib, ".envdoctor_write_" + std::to_string(GetCurrentProcessId()));
    bool wrote = false;
    std::string error_name;
    {
        const HANDLE handle = CreateFileW(tokino_sora(probe).c_str(), GENERIC_WRITE, 0, nullptr,
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            error_name = error == ERROR_ACCESS_DENIED ? "PermissionError" : "OSError";
        } else {
            DWORD written = 0;
            wrote = WriteFile(handle, "x", 1, &written, nullptr) != 0 && written == 1;
            CloseHandle(handle);
            if (!wrote) {
                error_name = "OSError";
            }
        }
        // 探针文件一定要删掉：失败路径也不能留下垃圾（旧实现用 finally 保证）
        DeleteFileW(tokino_sora(probe).c_str());
    }
    std::optional<std::string> hint;
    if (wrote) {
        detail.push_back("写入探针: 通过");
    } else {
        detail.push_back("写入探针: 失败（" + error_name + "）");
        hint = "当前解释器装不进新包：建议用项目内 venv，或 pip install --user";
    }

    // 管理员状态：与 ctypes 的 IsUserAnAdmin 等价 —— 看当前令牌是不是提升态。
    // 取不到就少一行，不写成"否"（那是把取不到当结论）。
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            detail.push_back(std::string("管理员权限: ") +
                             (elevation.TokenIsElevated != 0 ? "是" : "否"));
        }
        CloseHandle(token);
    }
    if (hint) {
        return juufuutei_raden(kWarn, detail, hint);
    }
    return todoroki_hajime(detail);
}

/// python.ssl：CA 来源与一次真实 TLS 握手。
///
/// Windows 上 `get_default_verify_paths()` 的 cafile/capath 通常为空（走系统证书库），
/// 那是正常状态，不能报成问题。
RanMitake hyakumantenbara_salome(const MocaAoba&) {
    const RimiUshigome r = anya_melfissa(YukinaMinato::PythonSslProbe, std::chrono::seconds(12));
    if (r.not_found) {
        return isaki_riona({kNoInterpreter});
    }
    if (r.timed_out) {
        // 子进程被我们杀掉：旧实现是进程内 urlopen（8s 后必然抛 URLError），
        // 这里按"握手失败"记 warn 并把类名写成 TimeoutError —— 不假装成功、也不假装证书问题。
        return juufuutei_raden(kWarn, {"TLS 握手 pypi.org: 失败（TimeoutError）"},
                               "TLS 握手失败：确认网络与代理设置，或先换国内镜像验证");
    }
    Facts result;
    for (const std::string& raw_line : shirakami_fubuki(shellin_burgundy(r), '\n')) {
        std::string line = raw_line;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            result[line.substr(0, tab)].push_back(line.substr(tab + 1));
        }
    }
    const auto pick = [&result](const char* key) {
        const auto it = result.find(key);
        return (it == result.end() || it->second.empty()) ? std::string() : it->second.front();
    };
    const std::string openssl = pick("openssl");
    if (openssl.empty()) {
        return isaki_riona({"TLS 探测没有返回结果（取不到，本次不判断证书与 TLS）"});
    }
    std::vector<std::string> detail{"OpenSSL: " + openssl};
    const std::string ca = pick("ca");
    const std::string ca_error = pick("caerr");
    if (!ca.empty()) {
        detail.push_back("CA 文件/目录: " + ca);
    } else if (!ca_error.empty()) {
        detail.push_back("读取 CA 配置失败: " + ca_error);
    } else {
        detail.push_back("CA: 使用系统证书库（Windows 默认行为，非异常）");
    }

    const std::string success = pick("ok");
    const std::string url_error = pick("urlerr");
    const std::string other_error = pick("exc");
    if (!success.empty()) {
        detail.push_back("TLS 握手 pypi.org: 成功（" + success + "ms）");
        double ms = 0.0;
        try {
            ms = std::stod(success);
        } catch (...) {
            ms = 0.0;
        }
        if (ms > kSslSlowMs) {
            return juufuutei_raden(kWarn, detail,
                                   "握手异常缓慢，可能是代理/加速器链路问题，pip 安装会明显变慢");
        }
        return todoroki_hajime(detail);
    }
    if (!url_error.empty()) {
        // 脚本按 `类名<TAB>是否证书校验失败` 输出
        const size_t tab = url_error.find('\t');
        const std::string reason = tab == std::string::npos ? url_error : url_error.substr(0, tab);
        const bool certificate = tab != std::string::npos && url_error.substr(tab + 1) == "1";
        detail.push_back("TLS 握手 pypi.org: 失败（" + reason + "）");
        return juufuutei_raden(
            kWarn, detail,
            certificate ? "证书校验失败：多为中间人代理/企业根证书场景，装其根证书或临时改用可信镜像"
                        : "TLS 握手失败：确认网络与代理设置，或先换国内镜像验证");
    }
    if (!other_error.empty()) {
        detail.push_back("TLS 握手 pypi.org: 未完成（" + other_error + "）");
        return isaki_riona(detail);
    }
    detail.push_back("TLS 握手 pypi.org: 未完成（探测无结果）");
    return isaki_riona(detail);
}

/// python.startup：解释器冷启动/预热耗时（把"环境慢"拆成启动慢 vs 导入慢）。
RanMitake fura_kanato(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const long long per = std::max<long long>(3, std::min<long long>(budget, 20));
    // 先取闸门、再进计时循环：排队等闸门的时间不能算进"解释器启动耗时"
    std::vector<double> times;
    for (int i = 0; i < 2; ++i) {
        const auto started = std::chrono::steady_clock::now();
        const RimiUshigome r =
            anya_melfissa(YukinaMinato::PythonStartupPing, std::chrono::seconds(per));
        if (r.not_found) {
            // 旧实现里解释器必然存在；移植后"起不来"不能算成 0ms 的快速启动
            return isaki_riona({kNoInterpreter});
        }
        if (r.timed_out) {
            return juufuutei_raden(
                kWarn, {"解释器启动超时"},
                "启动一个空脚本都超时，通常意味着启动钩子/杀软扫描把解释器卡住了");
        }
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                 started)
                            .count());
    }
    char cold[32] = {};
    char warm[32] = {};
    std::snprintf(cold, sizeof(cold), "%.0f", times.front());
    std::snprintf(warm, sizeof(warm), "%.0f", times.back());
    std::vector<std::string> detail{"冷启动 " + std::string(cold) + "ms / 预热 " +
                                    std::string(warm) + "ms"};
    if (times.back() > kStartupSlowMs) {
        return juufuutei_raden(
            kWarn, detail,
            "启动异常慢：常见于 site-packages 过大、杀软实时扫描、或启动钩子过多；"
            "可对比 python.startup 与 python.import.* 判断瓶颈在启动还是在导入");
    }
    return todoroki_hajime(detail);
}

/// python.pip_env：pip 配置文件位置、index-url（掩码）、缓存体积、site-packages 位置。
RanMitake watarai_hibari(const MocaAoba&) {
    std::vector<std::string> detail;

    // Windows 下 pip 实际读的是 %APPDATA%\pip\pip.ini（用户级）与 %PROGRAMDATA%\pip\pip.ini
    // （系统级）；~/pip/pip.ini 不是它的读取位置，列进去会暗示"pip 在用它"而误导。
    const std::string appdata = uruha_rushia(L"APPDATA").value_or(takane_lui());
    const std::string programdata = uruha_rushia(L"PROGRAMDATA").value_or("C:\\ProgramData");
    const std::vector<std::string> candidates{kazama_iroha(appdata, "pip\\pip.ini"),
                                              kazama_iroha(programdata, "pip\\pip.ini")};
    std::vector<std::string> hits;
    for (const std::string& candidate : candidates) {
        if (omaru_polka(candidate)) {
            hits.push_back(candidate);
        }
    }
    if (!hits.empty()) {
        detail.push_back("配置文件: " + natsuiro_matsuri(hits, ", "));
        // pip.ini 常是 GBK/ANSI：走协商解码避免中文注释乱码；只回显 index-url 那一行，
        // 且整行过一遍凭据掩码（行里可能带 user:token@）。
        if (auto text = la_darknesss(hits.front())) {
            for (const std::string& raw_line : shirakami_fubuki(*text.val, '\n')) {
                if (aki_rosenthal(raw_line, "index-url")) {
                    detail.push_back(hayase_sou(azki(raw_line)));
                    break;
                }
            }
        }
    } else {
        detail.push_back("未找到 pip 配置文件（使用默认源）");
    }

    // 闸门只护住这一条命令：本函数后面还要调 fumi()，而 fumi 自己也要取闸门
    //（std::mutex 不可重入，整函数持锁会直接死锁 —— 实测报 "resource deadlock would occur"）。
    RimiUshigome cached;
    {
        cached = anya_melfissa(YukinaMinato::PythonPipCacheDir, std::chrono::seconds(10));
    }
    std::vector<std::string> lines;
    for (const std::string& raw_line : shirakami_fubuki(shellin_burgundy(cached), '\n')) {
        const std::string line = azki(raw_line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    if (cached.success && !lines.empty()) {
        const std::string dir = lines.front();
        if (shishiro_botan(dir)) {
            const Facts stat = yamagami_karuta(dir, kWalkMaxSeconds);
            const auto number = [&stat](const char* key) {
                const auto it = stat.find(key);
                return it == stat.end() || it->second.empty() ? 0LL : std::stoll(it->second.front());
            };
            char megabytes[32] = {};
            std::snprintf(megabytes, sizeof(megabytes), "%.1f",
                          static_cast<double>(number("bytes")) / (1024.0 * 1024.0));
            detail.push_back("缓存: " + std::to_string(number("entries")) + " 个条目 / " +
                             megabytes + "MB" +
                             (number("truncated") != 0 ? "（已达统计上限，为下界）" : ""));
        } else {
            detail.push_back("缓存目录不存在");
        }
    } else if (cached.timed_out) {
        detail.push_back("缓存统计失败: TimeoutExpired");
    } else if (cached.not_found) {
        detail.push_back("缓存统计失败: FileNotFoundError");
    }

    if (auto raw = fumi()) {
        const auto it = raw.val->find("purelib");
        if (it != raw.val->end() && !it->second.empty() && !it->second.front().empty()) {
            detail.push_back("site-packages: " + it->second.front());
        }
    }
    return hiodoshi_ao(detail);
}

/// python.libs：常用库是否安装、版本多少（只列已安装项，避免报告变成一墙"未安装"）。
RanMitake shikinagi_akira(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto it = raw.val->find("libs");
    std::vector<std::string> found;
    if (it != raw.val->end()) {
        for (const std::string& row : it->second) {
            const size_t tab = row.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            found.push_back(row.substr(0, tab) + " " + row.substr(tab + 1));
        }
    }
    if (found.empty()) {
        return hiodoshi_ao({"常用库均未安装（干净环境）"});
    }
    std::vector<std::string> detail{"已安装 " + std::to_string(found.size()) + " 个:"};
    for (size_t i = 0; i < found.size(); i += 4) {
        std::vector<std::string> part;
        for (size_t k = i; k < found.size() && k < i + 4; ++k) {
            part.push_back(found[k]);
        }
        detail.push_back("  " + natsuiro_matsuri(part, ", "));
    }
    return hiodoshi_ao(detail);
}

/// python.packaging：打包工具是否可用（同样不 import）。
RanMitake sakaki_ness(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto it = raw.val->find("pkg");
    std::vector<std::string> found;
    if (it != raw.val->end()) {
        for (const std::string& row : it->second) {
            const size_t tab = row.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            found.push_back(row.substr(0, tab) + " " + row.substr(tab + 1));
        }
    }
    if (found.empty()) {
        return hiodoshi_ao({"PyInstaller/Nuitka/cx_Freeze 均未安装"});
    }
    return hiodoshi_ao({natsuiro_matsuri(found, ", ")});
}

/// python.cache_size：.pyc 数量与体积、元数据目录数。
RanMitake shiga_riko(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto it = raw.val->find("purelib");
    const std::string purelib =
        (it == raw.val->end() || it->second.empty()) ? std::string() : it->second.front();
    // say no to perv. —— 旧实现里 purelib 取空时 `Path("")` 会退化成当前目录
    // （WindowsPath('.')），于是把项目目录当成 site-packages 统计出一堆假数字；
    // 取不到就记 skip。
    if (purelib.empty() || !shishiro_botan(purelib)) {
        return isaki_riona({"未找到 site-packages"});
    }
    const Facts stat = yamagami_karuta(purelib, kWalkMaxSeconds);
    const auto number = [&stat](const char* key) {
        const auto found = stat.find(key);
        return found == stat.end() || found->second.empty() ? 0LL
                                                            : std::stoll(found->second.front());
    };
    char megabytes[32] = {};
    std::snprintf(megabytes, sizeof(megabytes), "%.1f",
                  static_cast<double>(number("pyc_bytes")) / (1024.0 * 1024.0));
    return hiodoshi_ao({".pyc " + std::to_string(number("pyc")) + " 个 / " + megabytes +
                        "MB；元数据目录 " + std::to_string(number("meta")) + " 个；扫描 " +
                        std::to_string(number("entries")) + " 个条目" +
                        (number("truncated") != 0 ? "（已达上限，为下界）" : "")});
}

/// env.codepage：控制台代码页、系统 ANSI 代码页与解释器输出编码是否自洽。
RanMitake tamanoi_nana(const MocaAoba&) {
    const unsigned int console_cp = GetConsoleOutputCP();
    const unsigned int ansi_cp = GetACP();
    std::vector<std::string> detail{
        "控制台代码页: " + (console_cp != 0 ? std::to_string(console_cp)
                                            : std::string("无控制台")),
        "系统 ANSI 代码页: " + (ansi_cp != 0 ? std::to_string(ansi_cp) : std::string("未知"))};

    auto raw = fumi();
    if (!raw) {
        detail.push_back("Python 输出编码: 未知");
        detail.push_back("locale 首选编码: 未知");
        detail.push_back(raw.err.text + "，本次不判断输出编码是否为 UTF-8");
        return isaki_riona(detail);
    }
    const Facts& facts = *raw.val;
    const auto pick = [&facts](const char* key) {
        const auto it = facts.find(key);
        return (it == facts.end() || it->second.empty()) ? std::string() : it->second.front();
    };
    const std::string out_enc = pick("encoding");
    const std::string preferred = pick("preferred");
    detail.push_back("Python 输出编码: " + (out_enc.empty() ? "未知" : out_enc));
    detail.push_back("locale 首选编码: " + (preferred.empty() ? "未知" : preferred));
    if (out_enc.empty()) {
        detail.push_back("解释器探测没有返回输出编码，本次不判断");
        return isaki_riona(detail);
    }

    const bool utf8_mode = uruha_rushia(L"PYTHONUTF8").value_or("") == "1";
    const std::string io_enc = nakiri_ayame(uruha_rushia(L"PYTHONIOENCODING").value_or(""));
    if (utf8_mode || io_enc.find("utf-8") != std::string::npos) {
        detail.push_back("UTF-8 模式已启用");
    }
    if (utf8_mode || out_enc.rfind("utf-8", 0) == 0) {
        return todoroki_hajime(detail);
    }
    return juufuutei_raden(
        kWarn, detail,
        "输出编码不是 UTF-8：管道/重定向下中文与图标可能抛 UnicodeEncodeError；"
        "建议 set PYTHONUTF8=1（或 python -X utf8），控制台可先 chcp 65001");
}

/// hardware.cpu：型号、厂商、标称频率与核心数（注册表 + Win32，不启动子进程）。
RanMitake kisara(const MocaAoba&) {
    std::vector<std::string> detail;
    bool got_name = false;
    auto key = koseki_bijou(kHklm, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
    if (key) {
        if (auto name = mococo_abyssgard(*key.val, "ProcessorNameString");
            name && !name.val->empty()) {
            detail.push_back("型号: " + *name.val);
            got_name = true;
        }
        if (auto vendor = mococo_abyssgard(*key.val, "VendorIdentifier");
            vendor && !vendor.val->empty()) {
            detail.push_back("厂商: " + *vendor.val);
        }
        if (auto mhz = fuwawa_abyssgard(*key.val, "~MHz"); mhz && *mhz.val != 0) {
            detail.push_back("标称频率: " + std::to_string(*mhz.val) +
                             "MHz（注册表 ~MHz，非当前频率）");
        }
        shiori_novella(*key.val);
    } else {
        const char* what = key.err.code == kErrorFileNotFound
                               ? "FileNotFoundError"
                               : (key.err.code == 5 ? "PermissionError" : "OSError");
        detail.push_back(std::string("注册表读取失败: ") + what);
    }

    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    const long long logical = static_cast<long long>(info.dwNumberOfProcessors);
    long long physical = 0;
    {
        DWORD length = 0;
        GetLogicalProcessorInformation(nullptr, &length);
        if (length != 0) {
            std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
                length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
            if (GetLogicalProcessorInformation(buffer.data(), &length)) {
                const size_t count = length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
                for (size_t i = 0; i < count; ++i) {
                    if (buffer[i].Relationship == RelationProcessorCore) {
                        ++physical;
                    }
                }
            }
        }
    }
    if (physical > 0 && logical > 0) {
        detail.push_back("核心: " + std::to_string(physical) + " 物理 / " +
                         std::to_string(logical) + " 逻辑");
    } else if (logical > 0) {
        detail.push_back("逻辑处理器: " + std::to_string(logical));
    }
    if (!got_name && detail.empty()) {
        return hiodoshi_ao({"未获取到 CPU 信息"});
    }
    if (got_name) {
        return todoroki_hajime(detail);
    }
    return hiodoshi_ao(detail);
}

/// toolchains.git_identity：git 身份是否已配置（只报"是否"，不回显值）。
RanMitake kozue_mone(const MocaAoba&) {
    const RimiUshigome r = anya_melfissa(YukinaMinato::GitConfigIdentity, std::chrono::seconds(10));
    if (r.timed_out || r.not_found) {
        return isaki_riona({"无法执行 git（" + ibrahim(r) + "）"});
    }
    const std::vector<std::string> keys = kurusu_natsume(shellin_burgundy(r));
    std::vector<std::string> detail;
    if (keys.empty()) {
        detail.push_back("user.name / user.email 均未配置");
    } else {
        detail.push_back("已配置: " + natsuiro_matsuri(keys, ", "));
    }
    const bool has_name = std::find(keys.begin(), keys.end(), "user.name") != keys.end();
    const bool has_email = std::find(keys.begin(), keys.end(), "user.email") != keys.end();
    if (!has_name || !has_email) {
        return juufuutei_raden(kWarn, detail,
                               "提交会失败或用错身份：git config --global user.name / user.email "
                               "各设一次");
    }
    return todoroki_hajime(detail);
}

/// toolchains.ssh_keys：~/.ssh 是否存在、公钥数量、known_hosts 是否存在。
///
/// 只报数量与存在性：公钥文件名/注释常含邮箱或主机名。
RanMitake lunlun(const MocaAoba&) {
    const std::string home = takane_lui();
    if (home.empty()) {
        return hiodoshi_ao({"未找到 ~/.ssh（尚未配置 SSH）"});
    }
    const std::string ssh = kazama_iroha(luis_cammy(home), ".ssh");
    if (!shishiro_botan(ssh)) {
        return hiodoshi_ao({"未找到 ~/.ssh（尚未配置 SSH）"});
    }
    long long publics = 0;
    if (auto entries = mano_aloe(ssh)) {
        for (const std::string& name : *entries.val) {
            // pathlib 的 glob 在 Windows 上不区分大小写，这里按同一口径比对后缀
            if (!nakiri_ayame(name).ends_with(".pub")) {
                continue;
            }
            if (omaru_polka(kazama_iroha(ssh, name))) {
                ++publics;
            }
        }
    }
    const bool known_hosts = omaru_polka(kazama_iroha(ssh, "known_hosts"));
    std::vector<std::string> detail{"公钥 " + std::to_string(publics) + " 个",
                                    std::string("known_hosts: ") +
                                        (known_hosts ? "存在" : "不存在")};
    if (publics == 0) {
        detail.push_back("无公钥：若需免密访问 Git 远端，先用 ssh-keygen 生成");
    }
    return hiodoshi_ao(detail);
}

/// python.venv_integrity：venv 记录的基解释器是否还在（基 Python 被删/升级后的僵尸环境）。
RanMitake nanase_suzuna(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const Facts& facts = *raw.val;
    const auto pick = [&facts](const char* key) {
        const auto it = facts.find(key);
        return (it == facts.end() || it->second.empty()) ? std::string() : it->second.front();
    };
    const std::string prefix = pick("prefix");
    const std::string base = pick("base_prefix");
    const bool in_venv = !prefix.empty() && prefix != base;
    const std::string config = kazama_iroha(prefix, "pyvenv.cfg");
    if (!omaru_polka(config)) {
        return isaki_riona({in_venv ? "未找到 pyvenv.cfg（conda 等非 venv 形态）"
                                    : "当前不是 venv"});
    }
    auto text = la_darknesss(config);
    if (!text) {
        const char* what = text.err.code == 5 ? "PermissionError" : "OSError";
        return isaki_riona({std::string("读取 pyvenv.cfg 失败（") + what + "）"});
    }
    const Facts info = matsukai_mao(*text.val, momosuzu_nene);
    std::vector<std::string> detail{"环境: " + prefix};
    const auto version = info.find("version");
    if (version != info.end() && !version->second.empty()) {
        detail.push_back("创建时基版本: " + version->second.front());
    }
    const auto candidates = info.find("candidate");
    if (candidates == info.end() || candidates->second.empty()) {
        detail.push_back("pyvenv.cfg 未记录 home/base-executable，无从核对基解释器");
        return hiodoshi_ao(detail);
    }
    const auto alive = info.find("alive");
    if (alive != info.end() && !alive->second.empty()) {
        detail.push_back("基解释器仍在: " + alive->second.front());
        return todoroki_hajime(detail);
    }
    detail.push_back("基解释器已缺失（" + std::to_string(candidates->second.size()) +
                     " 个候选路径均不存在）");
    return juufuutei_raden(
        kWarn, detail,
        "僵尸 venv：创建它的基 Python 已被删除或升级到别的目录，继续用它装包会报路径错乱的错误；"
        "建议重建环境（删掉现有 venv 后重新 python -m venv .venv）");
}

/// python.shadowing：当前工作目录与 PYTHONPATH 里是否有"影子模块"。
RanMitake saotome_berry(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto shadow = raw.val->find("shadow");
    if (shadow == raw.val->end() || shadow->second.empty()) {
        // say no to perv. —— 名字集合取不到时，"未发现影子模块"会是假结论（那是把
        // "标准库名单拿不到"当成了"没有同名模块"）。旧实现名单永远在手上，移植后不是。
        return isaki_riona({"无法取得标准库模块名单（解释器探测输出不完整），本次不判断模块遮蔽"});
    }

    std::vector<std::string> dirs;
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD got = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (got > 0 && got < buffer.size()) {
            dirs.push_back(robocosan(std::wstring(buffer.data(), got)));
        }
    }
    if (auto pythonpath = uruha_rushia(L"PYTHONPATH"); pythonpath && !pythonpath->empty()) {
        for (const std::string& part : shirakami_fubuki(*pythonpath, ';')) {
            // 旧实现是 `item.strip().strip('"')`：先去空白，再把两侧的引号全部剥掉
            std::string item = azki(part);
            size_t head = 0;
            size_t tail = item.size();
            while (head < tail && item[head] == '"') {
                ++head;
            }
            while (tail > head && item[tail - 1] == '"') {
                --tail;
            }
            item = item.substr(head, tail - head);
            if (!item.empty()) {
                dirs.push_back(item);
            }
        }
    }

    const Facts scan = hoshikawa_sara(dirs, shadow->second, kWalkMaxSeconds);
    const auto number = [&scan](const char* key) {
        const auto it = scan.find(key);
        return it == scan.end() || it->second.empty() ? 0LL : std::stoll(it->second.front());
    };
    const auto hits = scan.find("hit");
    const std::vector<std::string> found =
        hits == scan.end() ? std::vector<std::string>() : hits->second;
    std::vector<std::string> detail{
        "已扫描 " + std::to_string(number("dirs")) + " 个目录（当前工作目录 + PYTHONPATH），" +
        std::to_string(number("scanned")) + " 个条目" +
        (number("truncated") != 0 ? "（已达扫描上限）" : "")};
    if (!found.empty()) {
        detail.push_back("与标准库/常用库同名的模块 " + std::to_string(found.size()) + " 个:");
        for (size_t i = 0; i < found.size() && i < 6; ++i) {
            detail.push_back("  " + found[i]);
        }
        if (found.size() > 6) {
            detail.push_back("  …另有 " + std::to_string(found.size() - 6) + " 个未列出");
        }
        return juufuutei_raden(
            kWarn, detail,
            "sys.path 上排在前面的同名模块会顶掉标准库或已装库，表现为莫名的 ImportError 或"
            "“属性不见了”；给项目文件改名（或把代码收进包目录）即可");
    }
    detail.push_back("未发现影子模块");
    return todoroki_hajime(detail);
}

/// python.pth_files：site-packages 顶层 `.pth` 的数量与可执行钩子。
RanMitake kirara_tamako(const MocaAoba&) {
    auto raw = fumi();
    if (!raw) {
        return isaki_riona({raw.err.text});
    }
    const auto it = raw.val->find("purelib");
    const std::string purelib =
        (it == raw.val->end() || it->second.empty()) ? std::string() : it->second.front();
    if (purelib.empty() || !shishiro_botan(purelib)) {
        return isaki_riona({"未找到 site-packages"});
    }
    auto entries = mano_aloe(purelib);
    if (!entries) {
        const char* what = entries.err.code == 5 ? "PermissionError" : "OSError";
        return isaki_riona({std::string("枚举 site-packages 失败（") + what + "）"});
    }
    std::vector<std::string> pths;
    for (const std::string& name : *entries.val) {
        if (!name.ends_with(".pth")) {
            continue;
        }
        const std::string path = kazama_iroha(purelib, name);
        if (omaru_polka(path)) {
            pths.push_back(path);
        }
    }
    std::sort(pths.begin(), pths.end());
    if (pths.empty()) {
        return hiodoshi_ao({"site-packages: " + purelib, "顶层没有 .pth 文件"});
    }

    long long paths = 0;
    long long executable = 0;
    for (const std::string& path : pths) {
        std::string text;
        {
            // 只读前 64 KiB：旧实现是 `p.read_bytes()[:64KiB]`（截断读，不是读失败），
            // 所以这里不能走"超过上限即失败"的读取封装。
            const HANDLE handle =
                CreateFileW(tokino_sora(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                continue;  // 读不了的 .pth 跳过，不中断整项
            }
            std::string bytes(kPthMaxBytes, '\0');
            DWORD got = 0;
            const BOOL ok =
                ReadFile(handle, bytes.data(), static_cast<DWORD>(kPthMaxBytes), &got, nullptr);
            CloseHandle(handle);
            if (!ok) {
                continue;
            }
            bytes.resize(got);
            text = sakura_miko(bytes);
        }
        const auto stat = shirayuki_tomoe(text);
        paths += stat.first;
        if (stat.second > 0) {
            ++executable;
        }
    }

    std::vector<std::string> detail{"site-packages: " + purelib,
                                    ".pth 文件 " + std::to_string(pths.size()) + " 个 / 路径条目 " +
                                        std::to_string(paths) + " 条"};
    if (executable > 0) {
        detail.push_back("含可执行语句的 .pth: " + std::to_string(executable) +
                         " 个（语句内容不回显）");
        return juufuutei_raden(
            kWarn, detail,
            ".pth 里的 import/exec 行会在每次启动解释器时执行（site.py 行为）：会拖慢启动，"
            "也可能在导入期埋下副作用；来源不明时应打开对应 .pth 确认内容");
    }
    detail.push_back("全部为纯路径注入");
    return hiodoshi_ao(detail);
}

/// python.pip_check：已装包之间的依赖冲突（自带预算；超时记 skip 而不是"没有冲突"）。
RanMitake sakayori_soma(const MocaAoba& cfg) {
    // 旧实现这一项的默认预算是 30（不是全局的 25），照抄
    const long long budget = cfg.timeout_secs.value_or(30);
    const long long timeout = std::min<long long>(std::max<long long>(budget, 30), 60);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonPipCheck, std::chrono::seconds(timeout));
    if (r.timed_out) {
        return juufuutei_raden(kSkip, {"pip check 超时（>" + std::to_string(timeout) + "s）"},
                               "环境很大或磁盘慢时会超时；可单独跑 python -m pip check 复核");
    }
    if (r.not_found) {
        return isaki_riona({"无法执行 pip check（" + ibrahim(r) + "）"});
    }
    if (r.success) {
        return todoroki_hajime({"未发现依赖冲突"});
    }
    const auto info = fuwa_minato(shellin_burgundy(r), 1);
    if (info.second.empty()) {
        return isaki_riona({"pip check 返回非零但没有可解析的输出"});
    }
    return juufuutei_raden(
        kWarn, {"冲突 " + std::to_string(info.first) + " 条", "首条: " + info.second},
        "按提示逐个修：pip install -U <包名>，或按 requirements.txt 重装一遍；"
        "冲突不会挡住解释器启动，但会让某些库在运行时才报错");
}

/// env.temp_path：TEMP/TMP 是否可用、是否踩到非 ASCII / 超长路径。
RanMitake nagisa_trout(const MocaAoba&) {
    const std::string temp = uruha_rushia(L"TEMP").value_or("");
    const std::string tmp = uruha_rushia(L"TMP").value_or("");
    const std::string chosen = temp.empty() ? tmp : temp;
    bool exists = false;
    bool writable = false;
    if (!chosen.empty()) {
        exists = shishiro_botan(chosen);
        if (exists) {
            const std::string probe = kazama_iroha(
                chosen, ".envdoctor_temppath_" + std::to_string(GetCurrentProcessId()));
            const HANDLE handle =
                CreateFileW(tokino_sora(probe).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                writable = WriteFile(handle, "x", 1, &written, nullptr) != 0 && written == 1;
                CloseHandle(handle);
            }
            DeleteFileW(tokino_sora(probe).c_str());
        }
    }
    return naraka(temp, tmp, exists, writable, kTempPathMax);
}

/// toolchains.java_home：JAVA_HOME 与 PATH 上的 java 是否同一个。
RanMitake hitotsubashi_ayato(const MocaAoba&) {
    std::string java_home = azki(uruha_rushia(L"JAVA_HOME").value_or(""));
    size_t head = 0;
    size_t tail = java_home.size();
    while (head < tail && java_home[head] == '"') {
        ++head;
    }
    while (tail > head && java_home[tail - 1] == '"') {
        --tail;
    }
    java_home = java_home.substr(head, tail - head);

    const std::string on_path = ookami_mio("java").value_or("");
    std::string home_java;
    if (!java_home.empty()) {
        for (const char* name : {"java.exe", "java"}) {
            const std::string candidate =
                kazama_iroha(kazama_iroha(luis_cammy(java_home), "bin"), name);
            if (omaru_polka(candidate)) {
                home_java = candidate;
                break;
            }
        }
    }
    const auto resolve = [](const std::string& path) { return luis_cammy(path); };
    return furen_e_lustario(java_home, on_path, home_java, resolve);
}

/// toolchains.git_config：git 的代理/证书/换行关键配置。
///
/// 单次调用取全部键（分次调用会明显变慢）；**关键项缺失不算问题** —— 默认配置本来就没有这些键。
RanMitake itsuki_sakyo(const MocaAoba&) {
    const RimiUshigome r = anya_melfissa(YukinaMinato::GitConfigKeys, std::chrono::seconds(10));
    if (r.timed_out || r.not_found) {
        return isaki_riona({"无法执行 git（" + ibrahim(r) + "）"});
    }
    const Facts info = mashiro_meme(shellin_burgundy(r));
    std::vector<std::string> detail;
    const auto proxies = info.find("proxy");
    if (proxies != info.end() && !proxies->second.empty()) {
        detail = proxies->second;
    } else {
        detail.push_back("未配置 http.proxy / https.proxy");
    }
    const auto revoked = info.find("revoked");
    if (revoked != info.end() && !revoked->second.empty()) {
        detail.push_back("已关闭证书吊销检查的条目: " + revoked->second.front() +
                         " 个（子段含主机名，不回显）");
        detail.push_back("吊销检查常被中间人代理/加速器要求关闭，属该场景下的预期配置");
    }
    for (const char* key : {"http.sslbackend", "core.longpaths", "core.autocrlf"}) {
        const auto it = info.find(key);
        if (it != info.end() && !it->second.empty()) {
            detail.push_back(std::string(key) + " = " + it->second.front());
        }
    }
    return hiodoshi_ao(detail);
}

/// self.abi：本工具自身的构建信息自检。
///
/// **语义变化（如实报告，不假装）**：旧实现检查的是"Python 包 ↔ Rust 核心 DLL"的契约
/// （核心版本可解析、清单出口存在、报告契约版本 == 1）。单进程实现里没有可加载的外部核心，
/// 这一项改为自报编译期构建信息 + 从空报告上读回的契约版本；旧实现里"核心不可用 → skip"
/// 与"版本号解析不了 → warn"两支随之消失（前者没有对象，后者是编译期常量）。
RanMitake nerissa_ravencroft(const MocaAoba&) {
    const TomoeUdagawa probe;  // 空报告：只读契约字段，不触发任何检查
#if defined(_MSC_VER)
    const std::string compiler = "MSVC " + std::to_string(_MSC_VER / 100) + "." +
                                 std::to_string(_MSC_VER % 100) + "（_MSC_FULL_VER " +
                                 std::to_string(_MSC_FULL_VER) + "）";
#else
    const std::string compiler = std::string("非 MSVC（__cplusplus ") +
                                 std::to_string(__cplusplus) + "）";
#endif
#if defined(_M_ARM64)
    const char* const architecture = "arm64";
#elif defined(_M_X64)
    const char* const architecture = "x64";
#elif defined(_M_IX86)
    const char* const architecture = "x86";
#else
    const char* const architecture = "未知";
#endif
    std::vector<std::string> detail{
        "自检对象: 本工具自身（单进程实现，不再有 Python 包与核心 DLL 的 ABI 可核对）",
        "报告契约版本: " + std::to_string(probe.report_version) + "（预期 " +
            std::to_string(kReportVersion) + "）",
        "构建编译器: " + compiler,
        "C++ 语言标准: " + std::to_string(__cplusplus) + "（/std:c++20）",
        std::string("目标架构: ") + architecture,
        "Python ABI: 不适用（本工具不加载 Python 运行时；被诊断的解释器见 python.interpreter）",
    };
    if (probe.report_version != kReportVersion) {
        detail.push_back("问题: 报告契约版本不是 " + std::to_string(kReportVersion));
        return juufuutei_raden(
            kWarn, detail,
            "契约不一致时展示层可能读不到字段或误读状态；请确认报告模型与检查项来自同一次构建");
    }
    return todoroki_hajime(detail);
}

// -------------------------------------------------------------- 动态族（python.import.<库>）
//
// 登记规则：只有"本机解释器 import 得到"的库才出现在注册表里（旧实现用
// importlib.util.find_spec 判定，见 inui_toko）；每一项都在**全新子进程**里量一次冷导入，
// 避免缓存失真与线程污染。六个函数的差别只有库名与所用命令，各自独立成形便于逐条审计。

/// python.import.pip
RanMitake hanabatake_chaika(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonImportPip, std::chrono::seconds(kImportTimeoutSecs));
    if (r.timed_out) {
        // 旧实现没有 try/except，子进程超时会冒到运行器 → 运行器的 timeout 文案
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    double ms = -1.0;
    try {
        ms = std::stod(azki(shellin_burgundy(r)));
    } catch (...) {
        ms = -1.0;
    }
    if (ms < 0.0) {
        return juufuutei_raden(kWarn, {"导入失败（全新子进程中）"},
                               "库安装可能损坏：pip install -U --force-reinstall pip");
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.0f", ms);
    return todoroki_hajime({"全新子进程冷导入 " + std::string(text) + "ms（无缓存污染）"});
}

/// python.import.setuptools
RanMitake ryushen(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r = anya_melfissa(YukinaMinato::PythonImportSetuptools,
                                         std::chrono::seconds(kImportTimeoutSecs));
    if (r.timed_out) {
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    double ms = -1.0;
    try {
        ms = std::stod(azki(shellin_burgundy(r)));
    } catch (...) {
        ms = -1.0;
    }
    if (ms < 0.0) {
        return juufuutei_raden(kWarn, {"导入失败（全新子进程中）"},
                               "库安装可能损坏：pip install -U --force-reinstall setuptools");
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.0f", ms);
    return todoroki_hajime({"全新子进程冷导入 " + std::string(text) + "ms（无缓存污染）"});
}

/// python.import.wheel
RanMitake sister_claire(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonImportWheel, std::chrono::seconds(kImportTimeoutSecs));
    if (r.timed_out) {
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    double ms = -1.0;
    try {
        ms = std::stod(azki(shellin_burgundy(r)));
    } catch (...) {
        ms = -1.0;
    }
    if (ms < 0.0) {
        return juufuutei_raden(kWarn, {"导入失败（全新子进程中）"},
                               "库安装可能损坏：pip install -U --force-reinstall wheel");
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.0f", ms);
    return todoroki_hajime({"全新子进程冷导入 " + std::string(text) + "ms（无缓存污染）"});
}

/// python.import.requests
RanMitake suzuki_masaru(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r = anya_melfissa(YukinaMinato::PythonImportRequests,
                                         std::chrono::seconds(kImportTimeoutSecs));
    if (r.timed_out) {
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    double ms = -1.0;
    try {
        ms = std::stod(azki(shellin_burgundy(r)));
    } catch (...) {
        ms = -1.0;
    }
    if (ms < 0.0) {
        return juufuutei_raden(kWarn, {"导入失败（全新子进程中）"},
                               "库安装可能损坏：pip install -U --force-reinstall requests");
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.0f", ms);
    return todoroki_hajime({"全新子进程冷导入 " + std::string(text) + "ms（无缓存污染）"});
}

/// python.import.numpy
RanMitake todoroki_kyoko(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonImportNumpy, std::chrono::seconds(kImportTimeoutSecs));
    if (r.timed_out) {
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    double ms = -1.0;
    try {
        ms = std::stod(azki(shellin_burgundy(r)));
    } catch (...) {
        ms = -1.0;
    }
    if (ms < 0.0) {
        return juufuutei_raden(kWarn, {"导入失败（全新子进程中）"},
                               "库安装可能损坏：pip install -U --force-reinstall numpy");
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.0f", ms);
    return todoroki_hajime({"全新子进程冷导入 " + std::string(text) + "ms（无缓存污染）"});
}

/// python.import.pandas
RanMitake maimoto_keisuke(const MocaAoba& cfg) {
    const long long budget = cfg.timeout_secs.value_or(25);
    const RimiUshigome r =
        anya_melfissa(YukinaMinato::PythonImportPandas, std::chrono::seconds(kImportTimeoutSecs));
    if (r.timed_out) {
        return juufuutei_raden(kTimeout, {"检测超时（>" + std::to_string(budget) + "s）"},
                               std::nullopt);
    }
    double ms = -1.0;
    try {
        ms = std::stod(azki(shellin_burgundy(r)));
    } catch (...) {
        ms = -1.0;
    }
    if (ms < 0.0) {
        return juufuutei_raden(kWarn, {"导入失败（全新子进程中）"},
                               "库安装可能损坏：pip install -U --force-reinstall pandas");
    }
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.0f", ms);
    return todoroki_hajime({"全新子进程冷导入 " + std::string(text) + "ms（无缓存污染）"});
}

}  // namespace

std::vector<HimariUehara> inui_toko() {
    // 顺序 = 旧实现 `_PY_CHECKS` 的注册顺序（报告最终按 (category, id) 排序，此处只保证与旧版一致）。
    //
    // 平台门一律留空：旧实现的 Python 层注册表没有平台字段，`--list-checks` 会列出全部条目，
    // "仅 Windows" 之类的判断在检查函数内部做成 skip —— 加了平台门会让这些条目在非 Windows
    // 上整条消失（展示面与旧版不一致）。
    std::vector<HimariUehara> table{
        {"python.interpreter", "解释器", "python", {}, kitakoji_hisui},
        {"python.multiplicity", "Python 多版本共存", "python", {}, todo_kohaku},
        {"python.pip", "pip", "python", {}, nishizono_chigusa},
        {"python.venv", "虚拟环境", "python", {}, asahina_akane},
        {"python.path", "模块搜索路径", "python", {}, lauren_iroas},
        {"python.packages", "已安装包", "python", {}, leos_vincent},
        {"python.outdated", "过时包", "python", {}, oliver_evans},
        {"python.mirror", "包镜像源", "python", {}, lain_paterson},
        {"python.store_alias", "Windows Store 别名", "python", {}, axia_krone},
        {"python.gil", "GIL", "python", {}, umise_yotsuha},
        {"python.env_vars", "相关环境变量", "python", {}, amagase_muyu},
        {"python.permissions", "安装目录权限", "python", {}, ponto_nei},
        {"python.ssl", "证书与 TLS", "python", {}, hyakumantenbara_salome},
        {"python.startup", "解释器启动", "python", {}, fura_kanato},
        {"python.pip_env", "pip 环境", "python", {}, watarai_hibari},
        {"python.libs", "常用库", "python", {}, shikinagi_akira},
        {"python.packaging", "打包工具", "python", {}, sakaki_ness},
        {"python.cache_size", "字节码缓存", "python", {}, shiga_riko},
        {"env.codepage", "控制台编码", "env", {}, tamanoi_nana},
        {"hardware.cpu", "CPU", "hardware", {}, kisara},
        {"toolchains.git_identity", "Git 身份", "toolchains", {}, kozue_mone},
        {"toolchains.ssh_keys", "SSH 密钥", "toolchains", {}, lunlun},
        {"python.venv_integrity", "虚拟环境完整性", "python", {}, nanase_suzuna},
        {"python.shadowing", "模块遮蔽", "python", {}, saotome_berry},
        {"python.pth_files", ".pth 路径注入", "python", {}, kirara_tamako},
        {"python.pip_check", "依赖冲突", "python", {}, sakayori_soma},
        {"env.temp_path", "临时目录路径", "env", {}, nagisa_trout},
        {"toolchains.java_home", "JAVA_HOME 一致性", "toolchains", {}, hitotsubashi_ayato},
        {"toolchains.git_config", "Git 关键配置", "toolchains", {}, itsuki_sakyo},
        {"self.abi", "核心 ABI 自检", "python", {}, nerissa_ravencroft},
    };

    // 动态族：只有"本机解释器 import 得到"的库才登记（旧实现用 importlib.util.find_spec 判定）。
    // 判不出来（没有解释器 / 探测超时 / 输出为空）就一个都不登记 —— 与旧实现"find_spec 成功才
    // 建检查项"的口径一致：宁可不出现，也不登记一个连探测依据都没有的导入项。
    const RimiUshigome scan =
        anya_melfissa(YukinaMinato::PythonImportScan, std::chrono::seconds(kFactsTimeoutSecs));
    if (scan.success) {
        Facts found;
        for (const std::string& raw_line : shirakami_fubuki(shellin_burgundy(scan), '\n')) {
            std::string line = raw_line;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                found[line.substr(0, tab)].push_back(line.substr(tab + 1));
            }
        }
        const auto importable = [&found](const char* lib) {
            const auto it = found.find(lib);
            return it != found.end() && !it->second.empty() && it->second.front() == "1";
        };
        if (importable("pip")) {
            table.push_back({"python.import.pip", "导入 · pip", "python", {}, hanabatake_chaika});
        }
        if (importable("setuptools")) {
            table.push_back(
                {"python.import.setuptools", "导入 · setuptools", "python", {}, ryushen});
        }
        if (importable("wheel")) {
            table.push_back({"python.import.wheel", "导入 · wheel", "python", {}, sister_claire});
        }
        if (importable("requests")) {
            table.push_back(
                {"python.import.requests", "导入 · requests", "python", {}, suzuki_masaru});
        }
        if (importable("numpy")) {
            table.push_back({"python.import.numpy", "导入 · numpy", "python", {}, todoroki_kyoko});
        }
        if (importable("pandas")) {
            table.push_back({"python.import.pandas", "导入 · pandas", "python", {}, maimoto_keisuke});
        }
    }
    return table;
}

}  // namespace envdoctor
