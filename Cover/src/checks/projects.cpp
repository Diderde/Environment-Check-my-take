// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// projects.inventory / projects.health / projects.deps：本地项目清点、仓库健康度、
// 跨项目依赖。
//
// 与环境类检查的分界：这一组诊断的是**工作区**，所以扫描范围必须由调用方显式给出
// （`--scan-root`，可重复）；不给就记 skip —— 未配置不是问题，而全盘扫描实测不可行
// （一个根 8s 预算、20 万条目上限这三道闸门就是为此设的）。
//
// 三项共享同一份快照：逐仓库取数要走文件系统、还要为每个仓库起一个 `git status` 子进程
// （实测 30~130ms），各查一遍就是三倍代价。取数集中在 `levi_elipha`，判定与解析
// 全是纯函数。快照按"轮"存活：本轮该来的检查都取过之后立刻丢弃，因此界面里连点两次
// "运行"不会拿到上一轮的仓库状态。
//
// 隐私约定（详见头文件）：报告里只出现仓库编号；远端只报条数，地址从不进入报告。

#include "checks/projects.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "base/encoding.h"
#include "base/fs.h"
#include "base/status.h"
#include "base/string_util.h"
#include "win/tools.h"

namespace envdoctor {
namespace {

/// 一轮扫描的三道闸门：时间预算、仓库数上限、目录条目上限。缺一个都会让"扫一个根"
/// 变成"扫到天亮"（旧实现留的实测结论：40 万条目上限下 17s 就截断且没走完）。
constexpr double kBudgetSeconds = 8.0;
/// 单个仓库 `git status` 的超时上限（旧实现的 `_PROJECTS_GIT_TIMEOUT`）：实测每个
/// git 子进程 30~130ms，6s 是给"磁盘卡住/仓库巨大"留的余量；实际还会被剩余预算压住。
constexpr double kGitTimeoutSeconds = 6.0;
constexpr size_t kMaxRepos = 200;
constexpr size_t kWalkMaxEntries = 200'000;
constexpr int kWalkMaxDepth = 3;
constexpr size_t kManifestMaxBytes = 256 * 1024;
constexpr size_t kSample = 5;              // 明细里每类最多列几个编号
constexpr size_t kDetailMaxLines = 7;      // 明细行数上限（样本 5 + 首行 + 截断说明）
constexpr double kLockStaleSeconds = 180;  // 残留 index.lock 超过这个时长才算"卡住"

/// 没给扫描根时的说明（三项共用一句，未配置不是问题）。
constexpr const char* kNoRootsDetail = "未指定扫描根（--scan-root 可重复；默认不扫描本地项目）";

/// 一轮里会来取快照的检查项数。类别过滤按**类别**做，三项同属 `projects`，
/// 要么都跑要么都不跑，所以这个数是确定的。
constexpr int kProjectsConsumers = 3;

/// 还算"同一轮"的取数窗口：同一轮里三项是背靠背派发的（线程启动抖动只有毫秒级），
/// 一秒足够宽；而上一轮残留的快照（那一轮被取消、只跑了一两项）不会喂给下一次运行。
constexpr double kRoundWindowSeconds = 1.0;

/// 本轮快照槽：三项检查共享一次扫描。
///
/// 生命周期按"轮"走 —— 槽在第一个来取数的检查里建立，本轮该来的检查都取走之后立刻丢弃
/// （`remaining` 归零），于是下一轮必然是重新扫描。引擎不给检查项"轮开始"的钩子，
/// 换代只能由取数次数判定；"取数窗口"则兜住"只跑了一两项的那一轮"。
struct {
    std::mutex mu;
    std::condition_variable cv;
    bool ready = false;     // 槽里有一份可以复用的本轮快照
    bool scanning = false;  // 有消费者正在扫描
    int remaining = 0;      // 本轮还会来取这份快照的检查项数
    unsigned long long epoch = 0;
    std::vector<std::string> key_roots;
    std::chrono::steady_clock::time_point stamp{};
    std::vector<sukoya_kana> repos;
    size_t candidates = 0;
    bool truncated = false;
    double seconds = 0.0;
} g_snapshot;

/// 限深度发现 git 仓库。
///
/// `.git` 是目录（普通仓库）**或**文件（worktree / 子模块里它写着 `gitdir: …`）都算 ——
/// 只判目录会整批漏掉 worktree。编号规则：按调用方给定的根顺序、根内按路径字典序，
/// 因此编号可复现（报告里只有编号，编号稳定才谈得上"再跑一次能对上"）。
std::vector<std::string> nui_sociere(const std::vector<std::string>& roots,
                                     const std::chrono::steady_clock::time_point deadline,
                                     bool* truncated) {
    // 不值得进入的目录：产物、依赖缓存、虚拟环境。它们既不含仓库，量又极大。
    static const std::set<std::string> kSkipDirs{
        "node_modules", "__pycache__",  ".venv",         "venv",       "target",
        "dist",         "build",        ".mimosa",       ".idea",      ".vscode",
        "site-packages", ".mypy_cache", ".pytest_cache", "$RECYCLE.BIN",
        "System Volume Information"};

    std::vector<std::string> found;
    std::vector<std::string> seen;  // 小写路径；一轮的仓库数量级在百以内，线性查足够
    size_t scanned = 0;
    bool cut = false;
    for (const std::string& root : roots) {
        if (!shishiro_botan(root)) {
            continue;  // 给的根不是目录：跳过（根是调用方给的，可能只是打错了）
        }
        std::vector<std::string> per_root;
        std::vector<std::pair<std::string, int>> stack{{root, 0}};
        while (!stack.empty()) {
            const std::string dir = stack.back().first;
            const int depth = stack.back().second;
            stack.pop_back();
            const auto names = mano_aloe(dir);
            if (!names) {
                continue;  // 读不到这个目录（权限/被删）：跳过它，别把整轮带停
            }
            for (const std::string& name : *names.val) {
                ++scanned;
                if (scanned > kWalkMaxEntries || std::chrono::steady_clock::now() > deadline) {
                    cut = true;
                    break;
                }
                const std::string full = kazama_iroha(dir, name);
                if (name == ".git" && (shishiro_botan(full) || omaru_polka(full))) {
                    const std::string key = nakiri_ayame(dir);
                    if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
                        seen.push_back(key);
                        per_root.push_back(dir);  // 仓库根 = `.git` 的父目录
                    }
                    continue;
                }
                if (depth >= kWalkMaxDepth || kSkipDirs.count(name) != 0) {
                    continue;
                }
                if (!shishiro_botan(full)) {
                    continue;  // 普通文件、以及指向目录的链接，都不下钻
                }
                stack.emplace_back(full, depth + 1);
            }
            if (cut) {
                break;
            }
        }
        std::sort(per_root.begin(), per_root.end(),
                  [](const std::string& a, const std::string& b) {
                      return nakiri_ayame(a) < nakiri_ayame(b);
                  });
        found.insert(found.end(), per_root.begin(), per_root.end());
        if (cut) {
            break;
        }
    }
    *truncated = cut;
    return found;
}

}  // namespace

std::vector<std::string> mayuzumi_kai(const MocaAoba& cfg) {
    std::vector<std::string> roots;
    for (const std::string& raw : cfg.scan_roots) {
        const std::string trimmed = azki(raw);
        if (!trimmed.empty()) {
            roots.push_back(trimmed);
        }
    }
    return roots;
}

int aiba_uiha(const std::string& config_text) {
    // 等价于旧实现的正则 `^\[remote\s+"`（多行）：行首的 `[remote` + 至少一个空白 + 引号。
    // 只数条数：地址里可能带凭据（`https://user:token@host/…`），一律不解析、不回显。
    int count = 0;
    bool at_line_start = true;
    for (size_t i = 0; i < config_text.size(); ++i) {
        if (at_line_start && config_text.compare(i, 7, "[remote") == 0) {
            size_t j = i + 7;
            const size_t ws = j;
            while (j < config_text.size() && (config_text[j] == ' ' || config_text[j] == '\t')) {
                ++j;
            }
            if (j > ws && j < config_text.size() && config_text[j] == '"') {
                ++count;
            }
        }
        at_line_start = config_text[i] == '\n';
    }
    return count;
}

std::string amamiya_kokoro(const std::string& repo) {
    const std::string dot_git = kazama_iroha(repo, ".git");
    if (!omaru_polka(dot_git)) {
        return dot_git;  // 普通仓库（`.git` 是目录）；取不到也按这个路径去试，读不到就是读不到
    }
    const auto raw = la_darknesss(dot_git, 4096);
    if (raw) {
        const std::string line = azki(*raw.val);
        if (line.size() >= 7 && akai_haato(line.substr(0, 7), "gitdir:")) {
            const std::string target = azki(line.substr(7));
            // Windows 口径的"绝对路径"：盘符 + 根分隔符。`\x`（驱动器相对）与 `/c/foo`
            // （git-bash 形态）都不算绝对 —— 与旧实现的 `Path.is_absolute()` 同一判定。
            const bool absolute = target.size() >= 3 &&
                                  std::isalpha(static_cast<unsigned char>(target[0])) != 0 &&
                                  target[1] == ':' && (target[2] == '\\' || target[2] == '/');
            if (!target.empty()) {
                return absolute ? target : kazama_iroha(repo, target);
            }
        }
    }
    return dot_git;
}

sukoya_kana hayama_marin(const std::string& repo, std::chrono::milliseconds timeout) {
    sukoya_kana out;
    out.path = repo;
    const std::string git_dir = amamiya_kokoro(repo);

    // HEAD 分支 / detached 直接读 `.git` 下的小文件，比再起几个 git 子进程便宜得多
    // （实测每个 git 子进程 30~130ms）。
    if (const auto head_raw = la_darknesss(kazama_iroha(git_dir, "HEAD"), 4096)) {
        const std::string head = azki(*head_raw.val);
        if (head.rfind("ref:", 0) == 0) {
            const size_t slash = head.rfind('/');
            out.branch = azki(head.substr(slash == std::string::npos ? 0 : slash + 1));
        } else if (head.size() >= 40) {
            out.detached = true;  // HEAD 直接指向提交：之后的提交会游离
        }
    }
    out.shallow = omaru_polka(kazama_iroha(git_dir, "shallow"));

    // 未完成的 git 操作：标记文件 → 操作名，顺序即优先级。
    static const std::vector<std::pair<const char*, const char*>> kOperations{
        {"MERGE_HEAD", "merge"},   {"rebase-merge", "rebase"}, {"rebase-apply", "rebase"},
        {"CHERRY_PICK_HEAD", "cherry-pick"}, {"REVERT_HEAD", "revert"}, {"BISECT_LOG", "bisect"}};
    for (const auto& [marker, name] : kOperations) {
        if (momosuzu_nene(kazama_iroha(git_dir, marker))) {
            out.operation = name;
            break;
        }
    }

    // 残留锁的"存在时长"。两端都取文件时钟：系统时钟可能被校时，混用两个时钟会算出
    // 荒谬的年龄（负值或几十年），而这里的判定阈值是 3 分钟。
    const std::string lock = kazama_iroha(git_dir, "index.lock");
    if (momosuzu_nene(lock)) {
        std::error_code ec;
        const auto stamp =
            std::filesystem::last_write_time(std::filesystem::path(tokino_sora(lock)), ec);
        if (!ec) {
            out.lock_age = std::chrono::duration<double>(
                               std::filesystem::file_time_type::clock::now() - stamp)
                               .count();
        }
        // 取不到时长就保持"没取到"：既不列进明细，也不折成"这把锁是新的"。
    }

    // `.git/config`：只数远端条数（地址可能带凭据，连解析都不做）。
    if (const auto cfg_text = la_darknesss(kazama_iroha(git_dir, "config"), kManifestMaxBytes)) {
        out.remotes = aiba_uiha(*cfg_text.val);
    }

    // 清单只看仓库根那一层（不限层级时实测 build.gradle 会数出 137 个）。
    static const std::set<std::string> kManifestNames{"requirements.txt", "pyproject.toml",
                                                      "package.json", "go.mod", "Cargo.toml"};
    if (const auto names = mano_aloe(repo)) {
        for (const std::string& name : *names.val) {
            if (kManifestNames.count(name) != 0) {
                out.manifests.push_back(name);
            }
        }
    }

    // 未提交改动只能靠 `git status --porcelain -z` 数出来（别的信息都能从 `.git` 下的小文件
    // 直接读）。仓库路径走 `yaguruma_rine` 的**工作目录**通道：参数表里仍然只有编译期字面量，
    // 路径不进命令行。命令本身带 `--no-optional-locks`，所以不会顺手刷新 index（那是写操作）。
    const RimiUshigome status = yaguruma_rine(YukinaMinato::GitStatusPorcelain, repo, timeout);
    if (status.success) {
        // `-z` 的每条记录以 NUL 结尾：**重命名/复制**占两个字段（新路径 + 原路径），
        // 按字段数数会把一次重命名计成两条改动 —— 那是旧实现专门踩过并修掉的坑。
        int count = 0;
        const std::vector<std::string> fields = shirakami_fubuki(status.out, '\0');
        for (size_t i = 0; i < fields.size(); ++i) {
            const std::string& field = fields[i];
            if (field.empty()) {
                continue;
            }
            ++count;
            if (field.size() >= 2 && (field[0] == 'R' || field[1] == 'R' || field[0] == 'C' ||
                                      field[1] == 'C')) {
                ++i;  // 跳过紧跟的原路径字段
            }
        }
        out.dirty = count;
    } else if (status.timed_out) {
        char secs[32] = {};
        std::snprintf(secs, sizeof(secs), "%g", std::chrono::duration<double>(timeout).count());
        out.error = "status 超时（>" + std::string(secs) + "s）";
    } else if (status.not_found) {
        out.error = "无法执行 git（FileNotFoundError）";
    } else {
        // 退出码非 0：取 stderr 的第一行（截到 120 字符），连一行都没有时用固定文案。
        // 这段文字只用来判"这次查询失败了"，**从不进入报告**。
        std::string first;
        for (const std::string& raw : shirakami_fubuki(status.err, '\n')) {
            const std::string line = azki(raw);
            if (!line.empty()) {
                first = line.substr(0, 120);
                break;
            }
        }
        out.error = first.empty() ? "git status 返回非零" : first;
    }
    return out;
}

RanMitake hakase_fuyuki(const std::vector<sukoya_kana>& repos, bool truncated) {
    if (repos.empty()) {
        return hiodoshi_ao({"未在给定扫描根下发现 git 仓库（或扫描已达预算上限）"});
    }
    const size_t n = repos.size();
    std::vector<size_t> dirty;
    std::vector<size_t> detached;
    std::vector<size_t> no_remote;
    std::vector<size_t> shallow;
    std::vector<size_t> broken;
    std::vector<size_t> locked;
    std::vector<size_t> errored;
    bool dirty_unexplained = false;
    for (size_t i = 0; i < n; ++i) {
        const sukoya_kana& r = repos[i];
        const size_t no = i + 1;
        if (!r.dirty.has_value()) {
            // 查询失败的仓库本来就没有条数 —— 旧实现也只把它们记进"查询失败"，不记进
            // "未提交改动"的计数里。真正没交代的是"既没条数、也没有失败说明"。
            if (r.error.empty()) {
                dirty_unexplained = true;
            }
        } else if (*r.dirty > 0) {
            dirty.push_back(no);
        }
        if (r.detached) {
            detached.push_back(no);
        }
        if (r.remotes == 0) {
            no_remote.push_back(no);
        }
        if (r.shallow) {
            shallow.push_back(no);
        }
        if (!r.operation.empty()) {
            broken.push_back(no);
        }
        if (r.lock_age.value_or(0.0) > kLockStaleSeconds) {
            locked.push_back(no);
        }
        if (!r.error.empty()) {
            errored.push_back(no);
        }
    }

    // 编号样本：`#1 #2 …（共 N）`。超过样本数时报总数，否则"有几个"会被样本长度骗了。
    const auto sample_tag = [](const std::vector<size_t>& ids) {
        std::string head;
        for (size_t i = 0; i < ids.size() && i < kSample; ++i) {
            if (!head.empty()) {
                head += " ";
            }
            head += "#" + std::to_string(ids[i]);
        }
        if (ids.size() > kSample) {
            head += " …（共 " + std::to_string(ids.size()) + "）";
        }
        return head;
    };

    std::string head = "仓库 " + std::to_string(n) + " 个：未提交改动 ";
    if (dirty_unexplained) {
        // say no to perv. —— 旧实现这里永远是一个数字。真出现"既没条数、也没有失败说明"
        // （取不到，且没人解释为什么取不到）时，写 0 就是把"没查"说成"没有改动" ——
        // 宁可明说未判断，也不给一个看起来很确定的假数字。
        head += "未判断（取不到，且没有失败说明）";
    } else {
        head += std::to_string(dirty.size());
    }
    head += "、无远端 " + std::to_string(no_remote.size()) + "、浅克隆 " +
            std::to_string(shallow.size());
    if (!errored.empty()) {
        head += "、查询失败 " + std::to_string(errored.size());
    }

    std::vector<std::string> detail{head};
    // 只有会挡路或会丢东西的状态才报 warn：残留 index.lock（所有 git 命令会被拒）、
    // 未完成的 merge/rebase（历史停在中途）、detached HEAD（提交会游离）。未提交改动、
    // 无远端、浅克隆都是正常工作状态，只记 info。
    const auto label = [&detail, &sample_tag](const char* name, const std::vector<size_t>& ids) {
        if (!ids.empty()) {
            detail.push_back("  " + std::string(name) + ": " + sample_tag(ids));
        }
    };
    label("未完成的 git 操作", broken);
    label("残留 index.lock", locked);
    label("detached HEAD", detached);
    label("未提交改动", dirty);
    label("无远端", no_remote);
    label("浅克隆", shallow);
    label("查询失败", errored);
    if (truncated) {
        detail.push_back("  已达预算上限，结果只覆盖前 " + std::to_string(n) + " 个仓库（为下界）");
    }
    if (detail.size() > kDetailMaxLines) {
        detail.resize(kDetailMaxLines);
    }
    if (!broken.empty() || !locked.empty() || !detached.empty()) {
        return juufuutei_raden(
            kWarn, detail,
            "编号按扫描根顺序 + 根内路径字典序。未完成的操作与残留锁会让后续 git 命令失败或丢提交："
            "先进该仓库跑 git status 看提示；锁超过 3 分钟，确认没有 git 进程残留后再删 index.lock");
    }
    return hiodoshi_ao(detail);
}

std::vector<std::string> eli_conifer(const std::string& text, bool* unparsed) {
    std::vector<std::string> out;
    *unparsed = false;
    constexpr size_t kNone = std::string::npos;
    const auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    const auto skip_ws = [&text, is_ws](size_t i) {
        while (i < text.size() && is_ws(text[i])) {
            ++i;
        }
        return i;
    };

    // 读一个 JSON 字符串字面量：返回 (解码后的内容, 结束位置)；kNone = 这里不是合法字符串。
    const auto read_string = [&text, kNone](size_t i) -> std::pair<std::string, size_t> {
        if (i >= text.size() || text[i] != '"') {
            return {std::string(), kNone};
        }
        std::string value;
        size_t j = i + 1;
        while (j < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[j]);
            if (c == '"') {
                return {value, j + 1};
            }
            if (c != '\\') {
                if (c < 0x20) {
                    return {std::string(), kNone};  // 控制字符必须转义
                }
                value.push_back(text[j]);
                ++j;
                continue;
            }
            if (j + 1 >= text.size()) {
                return {std::string(), kNone};
            }
            const char esc = text[j + 1];
            if (esc != 'u') {
                switch (esc) {
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    case '/': value.push_back('/'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    default: return {std::string(), kNone};
                }
                j += 2;
                continue;
            }
            const auto hex4 = [&text](size_t at) {
                long code = 0;
                for (size_t k = 0; k < 4; ++k) {
                    if (at + k >= text.size()) {
                        return -1L;
                    }
                    const char h = text[at + k];
                    code <<= 4;
                    if (h >= '0' && h <= '9') {
                        code |= (h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        code |= (h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        code |= (h - 'A' + 10);
                    } else {
                        return -1L;
                    }
                }
                return code;
            };
            long code = hex4(j + 2);
            if (code < 0) {
                return {std::string(), kNone};
            }
            size_t step = 6;
            if (code >= 0xD800 && code <= 0xDBFF && j + 11 < text.size() && text[j + 6] == '\\' &&
                text[j + 7] == 'u') {
                const long low = hex4(j + 8);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    step = 12;
                }
            }
            if (code < 0x80) {
                value.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                value.push_back(static_cast<char>(0xC0 | (code >> 6)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                value.push_back(static_cast<char>(0xE0 | (code >> 12)));
                value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                value.push_back(static_cast<char>(0xF0 | (code >> 18)));
                value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            j += step;
        }
        return {std::string(), kNone};
    };

    // 跳过一段值（对象/数组按括号配平，字符串里的括号不算数），返回结束位置。
    const auto skip_value = [&](auto&& self, size_t i) -> size_t {
        i = skip_ws(i);
        if (i >= text.size()) {
            return kNone;
        }
        const char c = text[i];
        if (c == '"') {
            return read_string(i).second;
        }
        if (c == '{' || c == '[') {
            const char close = (c == '{') ? '}' : ']';
            size_t j = i + 1;
            for (;;) {
                j = skip_ws(j);
                if (j >= text.size()) {
                    return kNone;
                }
                if (text[j] == close) {
                    return j + 1;
                }
                if (text[j] == ',') {
                    ++j;
                    continue;
                }
                if (close == '}' && text[j] == '"') {
                    const size_t key_end = read_string(j).second;
                    if (key_end == kNone) {
                        return kNone;
                    }
                    j = skip_ws(key_end);
                    if (j >= text.size() || text[j] != ':') {
                        return kNone;
                    }
                    j = self(self, j + 1);
                    if (j == kNone) {
                        return kNone;
                    }
                    continue;
                }
                if (close == ']') {
                    j = self(self, j);
                    if (j == kNone) {
                        return kNone;
                    }
                    continue;
                }
                return kNone;
            }
        }
        // 数字 / true / false / null：吃到分隔符为止
        size_t j = i;
        while (j < text.size() && !is_ws(text[j]) && text[j] != ',' && text[j] != '}' &&
               text[j] != ']') {
            ++j;
        }
        const std::string token = text.substr(i, j - i);
        if (token.empty()) {
            return kNone;
        }
        if (token != "true" && token != "false" && token != "null") {
            for (char ch : token) {
                if (std::string("-+0123456789.eE").find(ch) == std::string::npos) {
                    return kNone;
                }
            }
        }
        return j;
    };

    // 顶层必须是对象。解析失败一律什么都不报 —— 旧实现走 json.loads，失败同样是空结果，
    // 而不是靠猜补出一条半成品依赖。
    size_t i = skip_ws(0);
    if (i >= text.size() || text[i] != '{') {
        *unparsed = true;
        return out;
    }
    std::vector<std::pair<std::string, size_t>> blocks;  // (键, 值的 '{' 位置)
    size_t j = skip_ws(i + 1);
    for (;;) {
        j = skip_ws(j);
        if (j >= text.size()) {
            *unparsed = true;
            return {};
        }
        if (text[j] == '}') {
            break;
        }
        if (text[j] == ',') {
            ++j;
            continue;
        }
        const auto [key, key_end] = read_string(j);
        if (key_end == kNone) {
            *unparsed = true;
            return {};
        }
        j = skip_ws(key_end);
        if (j >= text.size() || text[j] != ':') {
            *unparsed = true;
            return {};
        }
        j = skip_ws(j + 1);
        if (j < text.size() && text[j] == '{' &&
            (key == "dependencies" || key == "devDependencies")) {
            blocks.emplace_back(key, j);
        }
        const size_t value_end = skip_value(skip_value, j);
        if (value_end == kNone) {
            *unparsed = true;
            return {};
        }
        j = value_end;
    }

    // 旧实现按 `("dependencies", "devDependencies")` 的**键顺序**取块（与它们在文件里的
    // 先后无关），块内保持文件顺序；同一个键出现两次时 `data.get` 拿到的是最后一个。
    for (const char* wanted : {"dependencies", "devDependencies"}) {
        size_t at = kNone;
        for (const auto& [key, pos] : blocks) {
            if (key == wanted) {
                at = pos;
            }
        }
        if (at == kNone) {
            continue;
        }
        size_t k = skip_ws(at + 1);
        for (;;) {
            k = skip_ws(k);
            if (k >= text.size()) {
                *unparsed = true;
                return {};
            }
            if (text[k] == '}') {
                break;
            }
            if (text[k] == ',') {
                ++k;
                continue;
            }
            const auto [pkg, pkg_end] = read_string(k);
            if (pkg_end == kNone) {
                *unparsed = true;
                return {};
            }
            k = skip_ws(pkg_end);
            if (k >= text.size() || text[k] != ':') {
                *unparsed = true;
                return {};
            }
            k = skip_ws(k + 1);
            if (k < text.size() && text[k] == '"') {
                const auto [ver, ver_end] = read_string(k);
                if (ver_end == kNone) {
                    *unparsed = true;
                    return {};
                }
                out.push_back(pkg + " " + ver);
                k = ver_end;
                continue;
            }
            // 非字符串值：旧实现只把名字交出去（`str(pkg)`），版本留空
            out.push_back(pkg);
            const size_t value_end = skip_value(skip_value, k);
            if (value_end == kNone) {
                *unparsed = true;
                return {};
            }
            k = value_end;
        }
    }
    return out;
}

std::vector<std::string> ratna_petit(const std::string& manifest, const std::string& text,
                                     bool* unparsed) {
    std::vector<std::string> out;
    *unparsed = false;
    const bool pyproject = (manifest == "pyproject.toml");
    const bool cargo = (manifest == "Cargo.toml");
    if (!pyproject && !cargo) {
        return out;
    }
    constexpr size_t kNone = std::string::npos;

    // 行内注释：`#` 只有在字符串外才是注释。
    const auto strip_comment = [](const std::string& line) {
        char quote = '\0';
        for (size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (quote != '\0') {
                if (quote == '"' && c == '\\') {
                    ++i;
                    continue;
                }
                if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '#') {
                return azki(line.substr(0, i));
            }
        }
        return azki(line);
    };
    // `=` 的位置（字符串里的不算）。
    const auto assign_pos = [](const std::string& line) {
        char quote = '\0';
        for (size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '=') {
                return i;
            }
        }
        return kNone;
    };
    // 方括号是否配平（字符串里的不算）：多行数组要靠它判断读到哪一行。
    const auto balanced = [](const std::string& value) {
        int depth = 0;
        int braces = 0;
        char quote = '\0';
        for (size_t i = 0; i < value.size(); ++i) {
            const char c = value[i];
            if (quote != '\0') {
                if (quote == '"' && c == '\\') {
                    ++i;
                    continue;
                }
                if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            } else if (c == '{') {
                ++braces;
            } else if (c == '}') {
                --braces;
            }
        }
        return depth <= 0 && braces <= 0 && quote == '\0';
    };
    // 键名解引号（`"serde" = "1"` 与 `serde = "1"` 等价）。
    const auto unquote = [](const std::string& key) {
        if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'') &&
            key.back() == key.front()) {
            return key.substr(1, key.size() - 2);
        }
        return key;
    };
    // 读一个 TOML 字符串：返回 (内容, 结束位置)；坏掉时置 `*bad`。
    const auto read_string = [kNone](const std::string& s, size_t at,
                                     bool* bad) -> std::pair<std::string, size_t> {
        *bad = false;
        const char quote = s[at];
        const bool basic = (quote == '"');
        std::string value;
        size_t i = at + 1;
        while (i < s.size()) {
            const char c = s[i];
            if (c == quote) {
                return {value, i + 1};
            }
            if (basic && c == '\\') {
                if (i + 1 >= s.size()) {
                    break;
                }
                const char esc = s[i + 1];
                switch (esc) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    // `\uXXXX` 之类的转义本层不解：与其回显一个被啃过的名字，不如说读不下来
                    default: *bad = true; return {std::string(), kNone};
                }
                i += 2;
                continue;
            }
            value.push_back(c);
            ++i;
        }
        *bad = true;
        return {std::string(), kNone};
    };
    // 数组里的字符串元素：只收字符串（旧实现 `isinstance(item, str)` 才收录），
    // 别的东西（内联表、数字、嵌套数组）与旧实现一样跳过。
    const auto array_items = [&read_string, kNone](const std::string& value, bool* bad) {
        std::vector<std::string> items;
        size_t i = 1;  // 跳过 '['
        while (i < value.size()) {
            while (i < value.size() && (value[i] == ' ' || value[i] == '\t' || value[i] == ',')) {
                ++i;
            }
            if (i >= value.size() || value[i] == ']') {
                break;
            }
            if (value[i] == '"' || value[i] == '\'') {
                bool broken = false;
                const auto [item, end] = read_string(value, i, &broken);
                if (broken) {
                    *bad = true;
                    return items;
                }
                items.push_back(item);
                i = end;
                continue;
            }
            int depth = 0;  // 非字符串元素：整块跳过去
            while (i < value.size()) {
                const char c = value[i];
                if (c == '"' || c == '\'') {
                    bool broken = false;
                    const auto part = read_string(value, i, &broken);
                    if (broken) {
                        *bad = true;
                        return items;
                    }
                    i = part.second;
                    continue;
                }
                if (c == '[' || c == '{') {
                    ++depth;
                } else if (c == ']' || c == '}') {
                    if (depth == 0) {
                        break;
                    }
                    --depth;
                } else if (c == ',' && depth == 0) {
                    break;
                }
                ++i;
            }
        }
        return items;
    };
    // Cargo 的三个依赖表：本体（键值行）与子表（`[dependencies.serde]`，键名就是包名）。
    const auto cargo_section = [](const std::string& table) -> std::optional<std::string> {
        for (const char* name : {"dependencies", "dev-dependencies", "build-dependencies"}) {
            const std::string section(name);
            if (table == section) {
                return std::string();
            }
            if (table.rfind(section + ".", 0) == 0) {
                return table.substr(section.size() + 1);
            }
        }
        return std::nullopt;
    };

    std::string table;
    size_t i = 0;
    while (i < text.size()) {
        const size_t eol = text.find('\n', i);
        const std::string raw = text.substr(i, (eol == kNone ? text.size() : eol) - i);
        i = (eol == kNone) ? text.size() : eol + 1;
        const std::string line = strip_comment(raw);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            if (line.size() >= 2 && line.back() == ']') {
                table = azki(line.substr(1, line.size() - 2));
                if (cargo) {
                    const auto rest = cargo_section(table);
                    // 子表形态：`[dependencies.serde]` 在旧实现里读到的是一个表（非字符串）
                    // → 只算包名。再多一层点的（`[dependencies.serde.features]`）不算新包。
                    if (rest && !rest->empty() && rest->find('.') == kNone) {
                        out.push_back(unquote(azki(*rest)));
                    }
                }
            }
            // 畸形的表头行：不在依赖表里，不影响结论，也不置 unparsed
            continue;
        }
        const bool relevant =
            pyproject ? (table == "project" || table == "project.optional-dependencies")
                      : cargo_section(table).has_value();
        const size_t eq = assign_pos(line);
        if (eq == kNone) {
            if (relevant) {
                *unparsed = true;
            }
            continue;
        }
        const std::string key = unquote(azki(line.substr(0, eq)));
        std::string value = azki(line.substr(eq + 1));
        if (value.rfind("\"\"\"", 0) == 0 || value.rfind("'''", 0) == 0) {
            // 多行字符串：与依赖无关，但要把它的续行整段吃掉，免得续行被当成畸形行
            const std::string fence = value.substr(0, 3);
            while (value.find(fence, 3) == kNone && i < text.size()) {
                const size_t next = text.find('\n', i);
                value += "\n" + text.substr(i, (next == kNone ? text.size() : next) - i);
                i = (next == kNone) ? text.size() : next + 1;
            }
            continue;
        }
        if (!value.empty() && value.front() == '[' && !balanced(value)) {
            // 数组可以跨行：一直读到方括号配上（TOML 允许数组换行）。
            while (i < text.size()) {
                const size_t next = text.find('\n', i);
                value += " " + strip_comment(text.substr(i, (next == kNone ? text.size() : next) - i));
                i = (next == kNone) ? text.size() : next + 1;
                if (balanced(value)) {
                    break;
                }
            }
            if (!balanced(value)) {
                if (relevant) {
                    *unparsed = true;
                }
                continue;
            }
        } else if (!value.empty() && value.front() == '{' && !balanced(value)) {
            // 内联表跨行不是合法 TOML（旧实现交给 tomllib 会整份清单解析失败）。这里不去猜
            // 它写了什么，但要把续行整段吃掉 —— 否则续行里的 `features = [...]` 会被当成一个
            // 凭空多出来的依赖。这份清单记成"读不下来"，其余的键值照常读。
            while (i < text.size() && !balanced(value)) {
                const size_t next = text.find('\n', i);
                value += " " + strip_comment(text.substr(i, (next == kNone ? text.size() : next) - i));
                i = (next == kNone) ? text.size() : next + 1;
            }
            if (relevant) {
                *unparsed = true;
            }
            continue;
        }

        if (pyproject) {
            const bool project_deps = (table == "project" && key == "dependencies");
            const bool optional_deps = (table == "project.optional-dependencies");
            if (!project_deps && !optional_deps) {
                continue;
            }
            if (value.empty() || value.front() != '[') {
                // say no to perv. —— 旧实现这里对非数组值走的是 `list(project["dependencies"])`：
                // `dependencies = "requests"` 会被拆成 r/e/q/u/e/s/t/s 这些单字符"包名"进报告，
                // 正好违反它自己那条"解析不出来的行直接跳过，宁可不报"。这里记成读不下来。
                *unparsed = true;
                continue;
            }
            bool bad = false;
            for (const std::string& item : array_items(value, &bad)) {
                out.push_back(item);
            }
            if (bad) {
                *unparsed = true;
            }
            continue;
        }

        // Cargo.toml：只有三个依赖表**本体**的键值行算依赖（子表已在上面的表头处理过）。
        const auto rest = cargo_section(table);
        if (!rest || !rest->empty()) {
            continue;
        }
        if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
            bool bad = false;
            const auto [spec, end] = read_string(value, 0, &bad);
            (void)end;
            if (bad) {
                *unparsed = true;
                continue;
            }
            out.push_back(spec.empty() ? key : key + " " + spec);
        } else {
            // 内联表 / 数组 / 数字：旧实现读到的是非字符串，只把名字交出去
            out.push_back(key);
        }
    }
    return out;
}

std::vector<ManifestDep> yorumi_rena(const std::string& manifest, const std::string& text,
                                     bool* unparsed) {
    std::vector<ManifestDep> deps;
    *unparsed = false;

    // 一条声明 → (包名, 版本约束原文, 精确钉死的版本)。解析不出来的行直接跳过 ——
    // 宁可不报，也不把解析残渣当依赖名回显。
    const auto add = [&deps](const std::string& spec_text, const std::string& ecosystem) {
        const std::string line = azki(spec_text);
        // 包名字符集 `[A-Za-z0-9._@/-]`（与旧实现的正则同款），其后是版本约束。
        const auto dep_char = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '.' || c == '_' || c == '@' || c == '/' || c == '-';
        };
        size_t i = 0;
        while (i < line.size() && dep_char(line[i])) {
            ++i;
        }
        if (i == 0) {
            return;  // 第一个字符就不在包名字符集里：这行不是声明
        }
        const std::string pkg = line.substr(0, i);
        size_t j = i;
        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])) != 0) {
            ++j;  // `\s*` 连换行一起吃掉
        }
        std::string spec = line.substr(j);
        if (spec.find('\n') != std::string::npos) {
            return;  // 残段跨行：旧正则匹配不上，整条作废
        }
        // `http` 前缀这条不是摆设：npm 里真有 `http-proxy` 这类包名，旧实现把它们一并跳过。
        if (pkg.rfind("-", 0) == 0 || pkg.rfind("http", 0) == 0 || pkg.rfind("git+", 0) == 0) {
            return;
        }
        if (!spec.empty() && spec.front() == '[') {
            // PEP 508 的 extras：uvicorn[standard]==0.30.0
            const size_t close = spec.find(']');
            spec = (close == std::string::npos) ? std::string() : azki(spec.substr(close + 1));
        }
        while (!spec.empty() && spec.back() == ',') {
            spec.pop_back();
        }

        // `\d+\.\d+\.\d+` 的整串匹配（npm 的 `1.2.3` 与 go 的 `v1.2.3` 都用它）。
        const auto numeric_triple = [](const std::string& s, size_t at) {
            const auto group_end = [&s](size_t start) {
                size_t end = start;
                while (end < s.size() && s[end] >= '0' && s[end] <= '9') {
                    ++end;
                }
                return end;
            };
            const size_t g1 = group_end(at);
            if (g1 == at || g1 >= s.size() || s[g1] != '.') {
                return false;
            }
            const size_t g2 = group_end(g1 + 1);
            if (g2 == g1 + 1 || g2 >= s.size() || s[g2] != '.') {
                return false;
            }
            const size_t g3 = group_end(g2 + 1);
            return g3 > g2 + 1 && g3 == s.size();
        };
        std::string pin;
        if (ecosystem == "python") {
            // `==[0-9][\w.]*`：只有 `==` 加数字开头才算精确钉死（`>=`、`~=` 都不是）
            if (spec.size() >= 3 && spec.rfind("==", 0) == 0 && spec[2] >= '0' && spec[2] <= '9') {
                bool ok = true;
                for (size_t k = 2; k < spec.size(); ++k) {
                    const char c = spec[k];
                    const bool word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                                      (c >= 'A' && c <= 'Z') || c == '_' || c == '.';
                    if (!word) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    pin = spec.substr(2);
                }
            }
        } else if (ecosystem == "npm") {
            if (numeric_triple(spec, 0)) {
                pin = spec;
            }
        } else if (ecosystem == "go") {
            if (!spec.empty() && spec.front() == 'v' && numeric_triple(spec, 1)) {
                pin = spec;
            }
        }
        // Cargo 的裸版本号本身是 caret 语义，旧实现也不把它当精确钉死 —— 这里同样不判。
        deps.emplace_back(pkg, spec, pin);
    };

    if (manifest == "requirements.txt") {
        for (const std::string& raw : shirakami_fubuki(text, '\n')) {
            const size_t hash = raw.find('#');
            const std::string line = azki(hash == std::string::npos ? raw : raw.substr(0, hash));
            if (line.empty() || line.front() == '-' || line.rfind("http", 0) == 0 ||
                line.rfind("git+", 0) == 0 || line.find("://") != std::string::npos) {
                continue;  // `-r` / `-e` / URL / VCS：没有可比的版本，跳过
            }
            const size_t semi = line.find(';');
            add(semi == std::string::npos ? line : line.substr(0, semi), "python");
        }
    } else if (manifest == "package.json") {
        bool bad = false;
        const std::vector<std::string> specs = eli_conifer(text, &bad);
        for (const std::string& spec : specs) {
            add(spec, "npm");
        }
        *unparsed = bad;
    } else if (manifest == "go.mod") {
        bool in_block = false;
        for (const std::string& raw : shirakami_fubuki(text, '\n')) {
            const size_t cut = raw.find("//");
            const std::string line = azki(cut == std::string::npos ? raw : raw.substr(0, cut));
            if (line.rfind("require (", 0) == 0) {
                in_block = true;
                continue;
            }
            if (in_block && line == ")") {
                in_block = false;
                continue;
            }
            if (line.rfind("require ", 0) == 0) {
                add(line.substr(8), "go");
            } else if (in_block) {
                add(line, "go");
            }
        }
    } else if (manifest == "pyproject.toml" || manifest == "Cargo.toml") {
        bool bad = false;
        const std::vector<std::string> specs = ratna_petit(manifest, text, &bad);
        const std::string ecosystem = (manifest == "pyproject.toml") ? "python" : "cargo";
        for (const std::string& spec : specs) {
            add(spec, ecosystem);
        }
        *unparsed = bad;
    }
    return deps;
}

RanMitake kagami_hayato(const std::vector<ProjectDep>& entries, size_t repo_count,
                        size_t manifests, size_t unreadable, size_t unparsed) {
    // 只对**精确钉死**的版本判互斥：范围约束之间是否相容需要真正的求解器（Cargo 的裸
    // 版本号本身也是 caret 语义），这里不做 —— 宁可不报，也不报错的。
    std::map<std::string, std::map<int, std::string>> pins;  // 包 → (仓库编号 → 精确版本)
    std::map<std::string, std::set<int>> counts;             // 包 → 声明它的仓库集合
    size_t total = 0;
    size_t pinned = 0;
    for (const auto& [idx, pkg, pin] : entries) {
        ++total;
        counts[pkg].insert(idx);
        if (!pin.empty()) {
            // setdefault：同一个仓库在多个清单里钉同一个包时，以先解析到的那条为准
            if (pins[pkg].emplace(idx, pin).second) {
                ++pinned;
            }
        }
    }

    // "有清单，但一份都没解析出依赖"与"根本没有受支持的清单文件"是两件事，旧实现都报
    // 同一句话。在读不到 / 本层读不下来时补一句说明，免得那句话变成取不到当结论。
    // say no to perv. —— 见上：旧实现在这两种情况给的是同一句"均无受支持的根清单文件"。
    const auto note = [unreadable, unparsed] {
        std::vector<std::string> parts;
        if (unreadable > 0) {
            parts.push_back(std::to_string(unreadable) + " 份读取失败");
        }
        if (unparsed > 0) {
            parts.push_back(std::to_string(unparsed) + " 份未能解析");
        }
        return natsuiro_matsuri(parts, "、");
    };

    if (manifests == 0) {
        std::vector<std::string> detail{
            "仓库 " + std::to_string(repo_count) +
            " 个，均无受支持的根清单文件"
            "（requirements.txt / pyproject.toml / package.json / go.mod / Cargo.toml）"};
        const std::string notes = note();
        if (!notes.empty()) {
            detail.push_back("另有 " + notes);
        }
        return hiodoshi_ao(detail);
    }

    std::string head = "已解析清单 " + std::to_string(manifests) + " 份 / 依赖条目 " +
                       std::to_string(total) + " 条";
    const std::string notes = note();
    if (!notes.empty()) {
        head += "（" + notes + "）";
    }
    std::vector<std::string> detail{head};

    // `shared` 给出跨项目共用最多的包：环境视角下那才是要紧的（共用越多，版本分裂越容易踩）。
    std::vector<std::pair<std::string, size_t>> shared;
    for (const auto& [pkg, idxs] : counts) {
        if (idxs.size() > 1) {
            shared.emplace_back(pkg, idxs.size());
        }
    }
    std::sort(shared.begin(), shared.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });
    if (!shared.empty()) {
        std::vector<std::string> parts;
        for (size_t i = 0; i < shared.size() && i < 6; ++i) {
            parts.push_back(shared[i].first + "（" + std::to_string(shared[i].second) +
                            " 个项目）");
        }
        detail.push_back("跨项目共用最多: " + natsuiro_matsuri(parts, "、"));
    }

    std::vector<std::pair<std::string, std::vector<std::pair<int, std::string>>>> conflicts;
    for (const auto& [pkg, by_repo] : pins) {
        std::set<std::string> versions;
        for (const auto& [idx, pin] : by_repo) {
            versions.insert(pin);
        }
        if (versions.size() > 1) {
            conflicts.emplace_back(
                pkg, std::vector<std::pair<int, std::string>>(by_repo.begin(), by_repo.end()));
        }
    }
    std::sort(conflicts.begin(), conflicts.end(), [](const auto& a, const auto& b) {
        if (a.second.size() != b.second.size()) {
            return a.second.size() > b.second.size();
        }
        return a.first < b.first;
    });

    if (!conflicts.empty()) {
        detail.push_back("精确版本互斥 " + std::to_string(conflicts.size()) +
                         " 项（同一时间只可能装一个版本）:");
        for (size_t i = 0; i < conflicts.size() && i < kSample; ++i) {
            std::vector<std::string> parts;
            for (const auto& [idx, pin] : conflicts[i].second) {
                parts.push_back("#" + std::to_string(idx) + "=" + pin);
            }
            detail.push_back("  " + conflicts[i].first + ": " + natsuiro_matsuri(parts, "、"));
        }
        if (detail.size() > kDetailMaxLines) {
            detail.resize(kDetailMaxLines);
        }
        return juufuutei_raden(kWarn, detail,
                              "互斥的包在不同项目里钉了不同版本：切换项目时容易装错。"
                              "要么统一版本，要么让每个项目用独立 venv / node_modules 隔离");
    }
    detail.push_back("未发现精确版本互斥（精确钉版本 " + std::to_string(pinned) + " 条）");
    return hiodoshi_ao(detail);
}

std::tuple<std::vector<sukoya_kana>, std::vector<std::string>, size_t, bool, double> levi_elipha(
    const MocaAoba& cfg) {
    const std::vector<std::string> roots = mayuzumi_kai(cfg);
    if (roots.empty()) {
        // 没给扫描根：不扫描，也不碰快照槽（未配置不是问题，更不该顺手去扫主目录）
        return {std::vector<sukoya_kana>{}, std::vector<std::string>{}, 0, false, 0.0};
    }

    std::unique_lock<std::mutex> lock(g_snapshot.mu);
    for (;;) {
        const bool same_round =
            g_snapshot.ready && g_snapshot.key_roots == roots &&
            std::chrono::steady_clock::now() - g_snapshot.stamp <
                std::chrono::milliseconds(static_cast<long long>(kRoundWindowSeconds * 1000));
        if (same_round && g_snapshot.remaining > 0) {
            --g_snapshot.remaining;
            auto copy = std::make_tuple(g_snapshot.repos, g_snapshot.key_roots,
                                        g_snapshot.candidates, g_snapshot.truncated,
                                        g_snapshot.seconds);
            if (g_snapshot.remaining == 0) {
                // 本轮该来的都取走了：立刻丢弃 —— 跨轮清空不必等谁来通知"新的一轮开始了"
                g_snapshot.ready = false;
                g_snapshot.repos.clear();
            }
            return copy;
        }
        if (g_snapshot.scanning && g_snapshot.key_roots == roots) {
            g_snapshot.cv.wait(lock);  // 同一轮：等前面那位把快照扫出来，别重复扫
            continue;
        }

        // 这一轮由我来扫。
        const unsigned long long epoch = g_snapshot.epoch;
        g_snapshot.scanning = true;
        g_snapshot.key_roots = roots;
        lock.unlock();

        const auto t0 = std::chrono::steady_clock::now();
        const auto deadline =
            t0 + std::chrono::milliseconds(static_cast<long long>(kBudgetSeconds * 1000.0));
        bool truncated = false;
        const std::vector<std::string> found = nui_sociere(roots, deadline, &truncated);
        const size_t candidates = found.size();
        std::vector<sukoya_kana> repos;
        for (size_t i = 0; i < found.size() && i < kMaxRepos; ++i) {
            const double left = std::chrono::duration<double>(deadline -
                                                              std::chrono::steady_clock::now())
                                    .count();
            if (left <= 0) {
                truncated = true;
                break;
            }
            // 单次 `git status` 的超时也要被剩余预算压住：否则一个超慢的仓库能在本轮
            // 8s 预算之外再吃 6s。下限 1s 是旧实现给的余量（实测单仓库 30~130ms）。
            const double per_repo = std::max(1.0, std::min(kGitTimeoutSeconds, left));
            repos.push_back(hayama_marin(
                found[i],
                std::chrono::milliseconds(static_cast<long long>(per_repo * 1000.0))));
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        lock.lock();
        g_snapshot.scanning = false;
        g_snapshot.stamp = std::chrono::steady_clock::now();
        if (g_snapshot.epoch == epoch && g_snapshot.key_roots == roots) {
            // 两个条件都满足才落地：期间没被作废过，且槽的键还是我这一份扫描的键 ——
            // 否则（作废过、或另一组扫描根已经接手）这份数据不能冒充"本轮快照"。
            g_snapshot.ready = true;
            g_snapshot.remaining = kProjectsConsumers - 1;
            g_snapshot.repos = repos;
            g_snapshot.candidates = candidates;
            g_snapshot.truncated = truncated;
            g_snapshot.seconds = seconds;
        }
        g_snapshot.cv.notify_all();
        return {repos, roots, candidates, truncated, seconds};
    }
}

void ars_almal() {
    const std::lock_guard<std::mutex> lock(g_snapshot.mu);
    ++g_snapshot.epoch;  // 在途的那份扫描回来时不会再落地
    g_snapshot.ready = false;
    g_snapshot.remaining = 0;
    g_snapshot.repos.clear();
    g_snapshot.cv.notify_all();
}

RanMitake yukishiro_mahiro(const MocaAoba& cfg) {
    auto [repos, roots, candidates, truncated, seconds] = levi_elipha(cfg);
    if (roots.empty()) {
        return isaki_riona({kNoRootsDetail});
    }
    size_t with_remote = 0;
    for (const sukoya_kana& r : repos) {
        if (r.remotes > 0) {
            ++with_remote;
        }
    }
    std::vector<std::string> detail{
        "扫描根 " + std::to_string(roots.size()) + " 个 / 发现仓库 " +
            std::to_string(repos.size()) + " 个",
        "有远端 " + std::to_string(with_remote) + " 个 / 无远端 " +
            std::to_string(repos.size() - with_remote) + " 个"};
    if (candidates > repos.size()) {
        detail.push_back("另有 " + std::to_string(candidates - repos.size()) +
                         " 个仓库未纳入（可达上限/预算）");
    }
    // 耗时按一位小数。已经撞上闸门时明说这是下界 —— 数字看着小，不代表扫完了。
    const auto one_decimal = [](double value) {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "%.1f", value);
        return std::string(buf);
    };
    detail.push_back("扫描耗时 " + one_decimal(seconds) + "s" +
                     (truncated ? "（已达上限，为下界）" : ""));
    return hiodoshi_ao(detail);
}

RanMitake suzuhara_lulu(const MocaAoba& cfg) {
    auto [repos, roots, candidates, truncated, seconds] = levi_elipha(cfg);
    (void)candidates;
    (void)seconds;
    if (roots.empty()) {
        return isaki_riona({kNoRootsDetail});
    }
    return hakase_fuyuki(repos, truncated);
}

RanMitake ex_albio(const MocaAoba& cfg) {
    auto [repos, roots, candidates, truncated, seconds] = levi_elipha(cfg);
    (void)candidates;
    (void)truncated;
    (void)seconds;
    if (roots.empty()) {
        return isaki_riona({kNoRootsDetail});
    }
    std::vector<ProjectDep> entries;
    size_t manifests = 0;
    size_t unreadable = 0;
    size_t unparsed = 0;
    for (size_t i = 0; i < repos.size(); ++i) {
        for (const std::string& name : repos[i].manifests) {
            const auto raw = la_darknesss(kazama_iroha(repos[i].path, name), kManifestMaxBytes);
            if (!raw) {
                ++unreadable;  // 读不到就是读不到：不折成"这个项目没有依赖"
                continue;
            }
            bool bad = false;
            const std::vector<ManifestDep> deps = yorumi_rena(name, *raw.val, &bad);
            if (bad) {
                ++unparsed;
            }
            if (!deps.empty()) {
                ++manifests;
            }
            for (const auto& [pkg, spec, pin] : deps) {
                (void)spec;  // 版本约束原文只服务于解析本身，报告里出现的是精确版本
                entries.emplace_back(static_cast<int>(i + 1), pkg, pin);
            }
        }
    }
    return kagami_hayato(entries, repos.size(), manifests, unreadable, unparsed);
}

std::vector<HimariUehara> ange_katrina() {
    return {
        HimariUehara{"projects.inventory", "本地项目清点", "projects", {}, yukishiro_mahiro},
        HimariUehara{"projects.health", "项目仓库健康度", "projects", {}, suzuhara_lulu},
        HimariUehara{"projects.deps", "跨项目依赖", "projects", {}, ex_albio},
    };
}

}  // namespace envdoctor
