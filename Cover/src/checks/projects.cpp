// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// projects.inventory / projects.health / projects.deps：本地项目清点、仓库健康度、
// 跨项目依赖。
//
// 与环境类检查的分界：这一组诊断的是**工作区**，所以扫描范围必须由调用方显式给出
// （`--scan-root`，可重复）；不给就记 skip —— 未配置不是问题，而全盘扫描实测不可行
// （一个根 8s 预算、20 万条目上限的闸门就是为此设的）。
//
// 三项共享同一份快照：逐仓库取数要走文件系统（旧实现还要为每个仓库起一个 `git status`
// 子进程，实测 30~130ms），各查一遍就是三倍代价。取数集中在 `levi_elipha`，判定与解析
// 全是纯函数。快照按"轮"存活：本轮该来的检查都取过之后立刻丢弃，因此界面里连点两次
// "运行"不会拿到上一轮的仓库状态。
//
// 隐私约定（详见头文件）：报告里只出现仓库编号；远端只报条数，地址从不进入报告。

#include "checks/projects.h"

#include <algorithm>
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

namespace envdoctor {
namespace {

/// 一轮扫描的三个闸门：时间预算、仓库数上限、目录条目上限。
/// 缺一个都会让"扫一个根"变成"扫到天亮"（旧实现留的实测结论：一个根 25s 预算、
/// 40 万条目上限，17s 就截断且没走完）。
constexpr double kBudgetSeconds = 8.0;
constexpr size_t kMaxRepos = 200;
constexpr size_t kWalkMaxEntries = 200'000;
constexpr int kWalkMaxDepth = 3;
constexpr size_t kManifestMaxBytes = 256 * 1024;
constexpr size_t kSample = 5;              // 明细里每类最多列几个编号
constexpr size_t kDetailMaxLines = 7;      // 明细行数上限（样本 5 + 首行 + 截断说明）
constexpr double kLockStaleSeconds = 180;  // 残留 index.lock 超过这个时长才算"卡住"

/// 一轮里会来取快照的检查项数。类别过滤是按**类别**做的，三项同属 `projects`，
/// 要么都跑要么都不跑，所以这个数是确定的。
constexpr int kProjectsConsumers = 3;

/// 还算"同一轮"的取数窗口：同一轮里三项是背靠背派发的（线程启动抖动是毫秒级），
/// 一秒足够宽；而上一轮残留的快照（例如那一轮被取消，只跑了一两项）不会喂给下一次运行。
constexpr double kRoundWindowSeconds = 1.0;

/// 不值得进入的目录：产物、依赖缓存、虚拟环境。它们既不含仓库，量又极大。
const std::set<std::string>& skip_dirs() {
    static const std::set<std::string> kSkip{
        "node_modules", "__pycache__", ".venv",        "venv",       "target",
        "dist",         "build",       ".mimosa",      ".idea",      ".vscode",
        "site-packages", ".mypy_cache", ".pytest_cache", "$RECYCLE.BIN",
        "System Volume Information"};
    return kSkip;
}

/// 只看仓库根的清单文件：不限层级时实测 build.gradle 会数出 137 个（走进了 vendored/生成树）。
const std::set<std::string>& manifest_names() {
    static const std::set<std::string> kManifests{"requirements.txt", "pyproject.toml",
                                                  "package.json", "go.mod", "Cargo.toml"};
    return kManifests;
}

/// 未完成的 git 操作标记 → 操作名。顺序即优先级（第一个命中的算数）。
const std::vector<std::pair<const char*, const char*>>& operation_markers() {
    static const std::vector<std::pair<const char*, const char*>> kOps{
        {"MERGE_HEAD", "merge"},   {"rebase-merge", "rebase"}, {"rebase-apply", "rebase"},
        {"CHERRY_PICK_HEAD", "cherry-pick"}, {"REVERT_HEAD", "revert"}, {"BISECT_LOG", "bisect"}};
    return kOps;
}

/// 定点小数（扫描耗时按一位小数报，与旧实现同口径）。
/// `std::to_string` 固定六位小数、`operator<<` 默认六位有效数字，都不能用来对齐 "1.3s"。
/// printf 的定点输出受区域设置影响，但本程序从不调用 `setlocale`，小数点稳定是 '.'。
std::string one_decimal(double value) {
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return buf;
}

/// 仓库编号样本：`#1 #2 …（共 N）`；超过样本数时给出总数（编号本身才需要复现）。
std::string sample_tag(const std::vector<size_t>& ids) {
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
                                  std::isalpha(static_cast<unsigned char>(target[0])) &&
                                  target[1] == ':' && (target[2] == '\\' || target[2] == '/');
            if (!target.empty()) {
                return absolute ? target : kazama_iroha(repo, target);
            }
        }
    }
    return dot_git;
}

sukoya_kana hayama_marin(const std::string& repo) {
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
            // 40 位十六进制 = HEAD 直接指向提交（提交会游离，不是任何分支）
            out.detached = true;
        }
    }
    out.shallow = omaru_polka(kazama_iroha(git_dir, "shallow"));
    for (const auto& [marker, name] : operation_markers()) {
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
        const auto stamp = std::filesystem::last_write_time(std::filesystem::path(tokino_sora(lock)), ec);
        if (!ec) {
            out.lock_age = std::chrono::duration<double>(
                               std::filesystem::file_time_type::clock::now() - stamp)
                               .count();
        }
        // 取不到时长就保持"没取到"：不列进明细，也不折成"锁是新的"。
    }

    // `.git/config`：只数远端条数（`aiba_uiha` 只做计数，地址不进入内存里的报告路径）。
    if (const auto cfg_text = la_darknesss(kazama_iroha(git_dir, "config"), kManifestMaxBytes)) {
        out.remotes = aiba_uiha(*cfg_text.val);
    }

    // 清单只看仓库根那一层。
    if (const auto names = mano_aloe(repo)) {
        for (const std::string& name : *names.val) {
            if (manifest_names().count(name) != 0) {
                out.manifests.push_back(name);
            }
        }
    }

    // 未提交改动要跑 `git status --porcelain -z`（还得带 `--no-optional-locks`，
    // 否则 git 会顺手刷新 index —— 那是写操作），而这条命令要求把工作目录作为参数传进去。
    // 本层的白名单入口只有固定的字面量命令行，没有"工作目录参数化"的那一条，所以这里
    // 保持"没取到"（`dirty` 为空），判定与文案都按未判断处理。
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
    bool dirty_unknown = false;
    for (size_t i = 0; i < n; ++i) {
        const sukoya_kana& r = repos[i];
        const size_t no = i + 1;
        if (!r.dirty.has_value()) {
            dirty_unknown = true;  // 没取到"有多少改动"：不写 0，也不列编号
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

    std::string head = "仓库 " + std::to_string(n) + " 个：未提交改动 ";
    if (dirty_unknown) {
        // say no to perv. —— 旧实现有 `git status` 撑着，所以这里永远是一个数字；本层没有
        // 那条白名单命令。直接写 0 就是把"没查"说成"没有改动"，那是把取不到当结论 ——
        // 宁可明说未判断，也不给一个看起来很确定的假数字。
        head += "未判断（本层没有只读的 git status 命令）";
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
    const auto label = [&detail](const char* name, const std::vector<size_t>& ids) {
        if (!ids.empty()) {
            detail.push_back("  " + std::string(name) + ": " + sample_tag(ids));
        }
    };
    label("未完成的 git 操作", broken);
    label("残留 index.lock", locked);
    label("detached HEAD", detached);
    if (!dirty_unknown) {
        label("未提交改动", dirty);
    }
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
        return juufuutei_radae_placeholder();
    }
    return hiodoshi_ao(detail);
}

}  // namespace envdoctor
