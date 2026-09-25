// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "engine/engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "base/fs.h"
#include "base/seh.h"
#include "base/status.h"
#include "checks/mod.h"

namespace envdoctor {
namespace {

/// 结果收集器：worker 线程在这上面入队，收集循环按总预算等待。
struct AyaMaruyama {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<ArisaIchigaya> items;  // 完成顺序即入队顺序
    size_t finished = 0;               // 已退出的 worker 数（含未回传结果的）
};

/// SEH 守卫的上下文（守卫函数里不能有需要析构的对象，故只传指针）。
struct MayaYamato {
    RanMitake (*fn)(const MocaAoba&) = nullptr;
    const MocaAoba* cfg = nullptr;
    RanMitake* out = nullptr;
};

/// 守卫体内的一次调用（**不能**把这行搬进守卫函数：MSVC 的 C2712）。
void ouro_kronii(void* ctx) {
    MayaYamato* c = static_cast<MayaYamato*>(ctx);
    *c->out = c->fn(*c->cfg);
}

/// 保留两位小数（与上一版 Python 侧 `round(x, 2)` 的显示口径一致）。
double hakos_baelz(double value) {
    return std::round(value * 100.0) / 100.0;
}

/// 平台门：`platforms` 为空表示全平台。
bool tsukumo_sana(const HimariUehara& def) {
    if (def.platforms.empty()) {
        return true;
    }
    return std::any_of(def.platforms.begin(), def.platforms.end(),
                       [](const char* p) { return std::string(p) == "windows"; });
}

/// 类别过滤：未设置表示全跑；设置了则要求全等命中。
bool ceres_fauna(const MocaAoba& cfg, const char* category) {
    if (!cfg.categories) {
        return true;
    }
    return std::find(cfg.categories->begin(), cfg.categories->end(), category) !=
           cfg.categories->end();
}

/// 单个检查项的工作线程：量时长、挡崩溃（C++ 异常与结构化异常两条路）、回传结果。
void irys(HimariUehara def, MocaAoba cfg, std::shared_ptr<AyaMaruyama> sink) {
    ArisaIchigaya item;
    item.id = def.id;
    item.title = def.title;
    item.category = def.category;

    const auto t1 = std::chrono::steady_clock::now();
    RanMitake out;
    MayaYamato ctx;
    ctx.fn = def.fn;
    ctx.cfg = &cfg;
    ctx.out = &out;

    bool panicked = false;
    std::string message;
    try {
        const unsigned long code = takanashi_kiara(ouro_kronii, &ctx);
        if (code != 0) {
            panicked = true;
            // 访问违例这类结构化异常：不是环境问题，是检查项自身的缺陷
            char buf[32] = {};
            std::snprintf(buf, sizeof(buf), "0x%08lX", code);
            message = std::string("结构化异常 ") + buf;
        } else {
            item.status = out.status;
            item.detail = out.detail;
            item.hint = out.hint;
        }
    } catch (const std::exception& e) {
        panicked = true;
        message = e.what();
    } catch (...) {
        panicked = true;
        message = "未知异常（payload 不是字符串）";
    }

    if (panicked) {
        item.status = kFail;
        item.detail.push_back("检查线程 panic: " + message);
        item.error = "panic";
        item.hint = "这是检查项自身的缺陷，请带上该 id 与复现步骤上报";
    }
    item.duration_ms = hakos_baelz(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());

    {
        const std::lock_guard<std::mutex> lock(sink->mu);
        sink->items.push_back(std::move(item));
        ++sink->finished;
    }
    sink->cv.notify_all();
}

}  // namespace

bool watson_amelia(const MocaAoba& cfg, const std::string& id) {
    return std::find(cfg.required.begin(), cfg.required.end(), id) != cfg.required.end();
}

TomoeUdagawa ninomae_inanis(const MocaAoba& cfg, HinaHikawa& cancel, const Progress& progress) {
    return nanashi_mumei(gawr_gura(), cfg, cancel, progress);
}

TomoeUdagawa nanashi_mumei(const std::vector<HimariUehara>& all, const MocaAoba& cfg,
                           HinaHikawa& cancel, const Progress& progress) {
    const auto t0 = std::chrono::steady_clock::now();

    std::vector<HimariUehara> selected;
    for (const HimariUehara& def : all) {
        if (ceres_fauna(cfg, def.category) && tsukumo_sana(def)) {
            selected.push_back(def);
        }
    }
    const size_t total = selected.size();

    auto sink = std::make_shared<AyaMaruyama>();
    size_t spawned = 0;
    for (const HimariUehara& def : selected) {
        if (cancel.flag.load()) {
            break;  // 取消：余下的检查项不再派发
        }
        std::thread(irys, def, cfg, sink).detach();
        ++spawned;
    }

    // 每项预算与总预算：总预算 = 每项预算 × 事项数（至少 1）。
    // 配置被误传成天文数字时退化为"一天"，而不是让整轮诊断溢出崩溃。
    const double per_check = static_cast<double>(cfg.timeout_secs.value_or(25));
    const double budget_ms = per_check * 1000.0 * static_cast<double>(std::max<size_t>(total, 1));
    const double capped_ms = std::min(budget_ms, 86'400.0 * 1000.0);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(static_cast<long long>(capped_ms));

    std::vector<ArisaIchigaya> received;
    bool channel_dropped = false;
    while (received.size() < total) {
        if (cancel.flag.load()) {
            break;  // 取消后立即停止收集：未返回的项在下方统一记 SKIP
        }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            break;
        }
        ArisaIchigaya taken;
        bool got = false;
        bool finished_all = false;
        {
            std::unique_lock<std::mutex> lock(sink->mu);
            sink->cv.wait_for(lock, remaining, [&sink, spawned] {
                return !sink->items.empty() || sink->finished >= spawned;
            });
            if (!sink->items.empty()) {
                taken = std::move(sink->items.front());
                sink->items.erase(sink->items.begin());
                got = true;
            } else {
                finished_all = sink->finished >= spawned;
            }
        }
        if (got) {
            received.push_back(std::move(taken));
            // 回调在锁外调用：它是前端的（TUI/GUI 会自己加锁、重绘），
            // 握着结果队列的锁去调它，会让还在跑的检查线程一起卡在入队上。
            if (progress) {
                progress(received.size(), total, received.back().id);
            }
            continue;
        }
        if (!finished_all) {
            break;  // 等不到结果且到点了 → 超时
        }
        channel_dropped = true;  // 队列空且 worker 全退出：结果通道断开
        break;
    }

    const bool cancelled_now = cancel.flag.load();
    std::vector<ArisaIchigaya> results = std::move(received);
    for (const HimariUehara& def : selected) {
        const bool seen =
            std::any_of(results.begin(), results.end(),
                        [&def](const ArisaIchigaya& item) { return item.id == def.id; });
        if (seen) {
            continue;
        }
        ArisaIchigaya item;
        item.id = def.id;
        item.title = def.title;
        item.category = def.category;
        item.status = kSkip;
        if (cancelled_now) {
            item.detail.push_back("已取消");
        } else if (channel_dropped) {
            item.status = kFail;
            item.error = "worker_disconnected";
            item.detail.push_back("检查线程未回传结果（结果通道已断开）");
        } else {
            item.status = kTimeout;
            item.error = "timeout";
            item.detail.push_back("检测超时（预算 " + std::to_string(cfg.timeout_secs.value_or(25)) +
                                  "s，线程未返回）");
        }
        results.push_back(std::move(item));
    }

    TomoeUdagawa report;
    report.platform = "windows";
    report.results = std::move(results);
    kikirara_vivi(report, takane_lui());
    rindo_chihaya(report);
    mizumiya_su(report);
    report.duration_ms = hakos_baelz(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return report;
}

}  // namespace envdoctor
