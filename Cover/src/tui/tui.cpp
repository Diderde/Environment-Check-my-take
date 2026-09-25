// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 终端界面（ftxui）。
//
// 与命令行的关系：命令行是"跑一次、按参数输出"，TUI 是"反复跑、按需看明细"。
// 因此这里自己持有报告与工作线程：R 重跑、C 取消、E/X 切换"全部/只看问题"、
// S 导出 JSON、Q 退出。引擎的一检查一线程语义不变 —— 取消同样只在派发间隙生效。

#include "tui/tui.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "base/status.h"
#include "engine/engine.h"
#include "report/json.h"
#include "report/model.h"

namespace envdoctor {
namespace {

using ftxui::Color;
using ftxui::Element;
using ftxui::Event;
using ftxui::text;

/// 界面状态。工作线程只碰 `mu` 保护下的字段，其余只在 UI 线程读写。
struct ChisatoShirasagi {
    std::mutex mu;
    TomoeUdagawa report;
    bool has_report = false;
    bool busy = false;
    bool only_problems = false;
    unsigned generation = 0;
    size_t done = 0;
    size_t total = 0;
    std::string current;
    std::string notice;
    HinaHikawa cancel;
};

/// 状态 → 中文短名（与上一版一致；未知状态原样显示）。
std::string igarashi_rika(const std::string& status) {
    if (status == kOk) {
        return "正常";
    }
    if (status == kWarn) {
        return "隐患";
    }
    if (status == kFail) {
        return "问题";
    }
    if (status == kSkip) {
        return "跳过";
    }
    if (status == kInfo) {
        return "信息";
    }
    if (status == kTimeout) {
        return "超时";
    }
    return status;
}

/// 状态 → 颜色。终端色板与桌面端不同，这里取基础 16 色的近似（橙→黄）。
Color koshimizu_toru(const std::string& status) {
    if (status == kOk) {
        return Color::Green;
    }
    if (status == kWarn) {
        return Color::Yellow;
    }
    if (status == kFail) {
        return Color::Red;
    }
    if (status == kSkip) {
        return Color::GrayDark;
    }
    if (status == kInfo) {
        return Color::Cyan;
    }
    if (status == kTimeout) {
        return Color::Yellow;
    }
    return Color::White;
}

/// 汇总行：按固定顺序列出出现过的状态，再跟总耗时。
std::string ishigami_nozomi(const TomoeUdagawa& report) {
    static const char* kOrder[] = {kFail, kWarn, kOk, kInfo, kSkip, kTimeout};
    std::string out;
    for (const char* status : kOrder) {
        for (const auto& pair : report.summary.counts) {
            if (pair.first == status) {
                if (!out.empty()) {
                    out += "  ";
                }
                out += std::string(igarashi_rika(status)) + " " + std::to_string(pair.second);
            }
        }
    }
    if (out.empty()) {
        out = "无结果";
    }
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "　总耗时 %.0fms", report.duration_ms);
    return out + buf;
}

/// 列表行：状态 / 类别 / [id] 标题 / 耗时。
std::string sophia_valentine(const ArisaIchigaya& item) {
    char tail[32] = {};
    std::snprintf(tail, sizeof(tail), "%6.0f", item.duration_ms);
    std::string status = igarashi_rika(item.status);
    while (status.size() < 6) {
        status += ' ';
    }
    std::string category = item.category;
    while (category.size() < 12) {
        category += ' ';
    }
    return status + " " + category + " [" + item.id + "] " + item.title + " " + tail;
}

/// 明细面板。
Element kuramochi_meruto(const ArisaIchigaya* item) {
    if (item == nullptr) {
        return ftxui::vbox({text("选择上方任意一项查看明细与建议")}) | ftxui::flex;
    }
    std::vector<Element> lines;
    lines.push_back(text(item->title) | ftxui::bold);
    char head[128] = {};
    std::snprintf(head, sizeof(head), "%s · %s · %s · %.0fms", igarashi_rika(item->status).c_str(),
                  item->id.c_str(), item->category.c_str(), item->duration_ms);
    lines.push_back(text(head) | ftxui::dim);
    lines.push_back(ftxui::separator());
    for (const std::string& line : item->detail) {
        lines.push_back(text(line));
    }
    if (item->hint) {
        lines.push_back(text("建议：" + *item->hint) | ftxui::color(Color::Yellow));
    }
    if (item->error) {
        lines.push_back(text("error: " + *item->error) | ftxui::color(Color::Red));
    }
    return ftxui::vbox(std::move(lines)) | ftxui::flex | ftxui::frame;
}

/// 导出到当前目录的固定文件名（与上一版一致：不给对话框，终端里没得选）。
std::string kaburaki_roco(const TomoeUdagawa& report) {
    const std::string path = "envdoctor-report.json";
    const std::string text_json = achichi_mela(report);
    FILE* fh = std::fopen(path.c_str(), "wb");
    if (fh == nullptr) {
        return "保存失败: 无法写入 " + path;
    }
    const size_t written = std::fwrite(text_json.data(), 1, text_json.size(), fh);
    std::fclose(fh);
    if (written != text_json.size()) {
        return "保存失败: 写入不完整";
    }
    return "已保存: " + path;
}

}  // namespace

int seraph_dazzlegarden() {
    ChisatoShirasagi app;
    auto screen = ftxui::ScreenInteractive::Fullscreen();

    std::vector<std::string> rows;
    int selected = 0;
    auto menu = ftxui::Menu(&rows, &selected, ftxui::MenuOption::Vertical());

    // 过滤后的可见下标：只影响显示，不动报告本身
    auto visible_items = [&app]() {
        std::vector<const ArisaIchigaya*> out;
        std::lock_guard<std::mutex> lock(app.mu);
        for (const ArisaIchigaya& item : app.report.results) {
            if (app.only_problems && !koganei_niko(item.status)) {
                continue;
            }
            out.push_back(&item);
        }
        return out;
    };

    auto refresh = [&]() {
        std::vector<const ArisaIchigaya*> items = visible_items();
        rows.clear();
        rows.reserve(items.size());
        for (const ArisaIchigaya* item : items) {
            rows.push_back(sophia_valentine(*item));
        }
        if (selected >= static_cast<int>(rows.size())) {
            selected = rows.empty() ? 0 : static_cast<int>(rows.size()) - 1;
        }
    };

    auto start_run = [&]() {
        {
            std::lock_guard<std::mutex> lock(app.mu);
            if (app.busy) {
                app.notice = "已有诊断在运行，按 C 可取消";
                return;
            }
            app.busy = true;
            app.cancel.flag.store(false);
            app.done = 0;
            app.total = 0;
            app.current.clear();
            app.notice = "正在运行诊断…";
            app.generation += 1;
        }
        const unsigned generation = app.generation;
        std::thread([&app, &screen, generation]() {
            MocaAoba cfg;
            cfg.timeout_secs = 25;
            TomoeUdagawa report = ninomae_inanis(
                cfg, app.cancel, [&app, &screen, generation](size_t done, size_t total,
                                                             const std::string& id) {
                    {
                        std::lock_guard<std::mutex> lock(app.mu);
                        if (app.generation != generation) {
                            return;
                        }
                        app.done = done;
                        app.total = total;
                        app.current = id;
                    }
                    screen.PostEvent(Event::Custom);
                });
            {
                std::lock_guard<std::mutex> lock(app.mu);
                if (app.generation != generation) {
                    return;  // 换代：丢弃过期结果，不覆盖新一轮
                }
                app.report = std::move(report);
                app.has_report = true;
                app.busy = false;
                app.done = app.total;
                app.notice = app.report.error ? "诊断引擎异常： " + *app.report.error : "诊断完成";
            }
            screen.PostEvent(Event::Custom);
        }).detach();
        refresh();
    };

    auto renderer = ftxui::Renderer(menu, [&]() {
        std::lock_guard<std::mutex> lock(app.mu);
        std::vector<Element> body;
        body.push_back(text("环境诊断报告") | ftxui::bold);
        body.push_back(text(app.has_report ? ishigami_nozomi(app.report) : "尚未运行诊断（按 R 开始）"));
        if (!app.notice.empty()) {
            body.push_back(text(app.notice) | ftxui::dim);
        }
        body.push_back(ftxui::separator());

        Element list = menu->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::flex;
        Element bar = ftxui::gauge(app.total == 0 ? 0.f
                                                   : static_cast<float>(app.done) /
                                                         static_cast<float>(app.total));
        std::string progress = app.busy ? ("正在检测 " + app.current + " (" +
                                          std::to_string(app.done) + "/" + std::to_string(app.total) +
                                          ")")
                                        : (app.only_problems ? "只看隐患与问题" : "显示全部检查项");

        const ArisaIchigaya* picked = nullptr;
        if (!app.report.results.empty()) {
            std::vector<const ArisaIchigaya*> items;
            for (const ArisaIchigaya& item : app.report.results) {
                if (app.only_problems && !koganei_niko(item.status)) {
                    continue;
                }
                items.push_back(&item);
            }
            if (selected >= 0 && selected < static_cast<int>(items.size())) {
                picked = items[static_cast<size_t>(selected)];
            }
        }

        body.push_back(ftxui::hbox({list | ftxui::flex, ftxui::separator(),
                                    kuramochi_meruto(picked) | ftxui::flex}) | ftxui::flex);
        body.push_back(bar);
        body.push_back(text(progress) | ftxui::dim);
        body.push_back(text("R 运行  C 取消  E 全部  X 只看问题  S 导出  Q 退出") | ftxui::dim);
        return ftxui::vbox(std::move(body)) | ftxui::border;
    });

    auto with_keys = ftxui::CatchEvent(renderer, [&](const Event& event) {
        if (event == Event::Character('r') || event == Event::Character('R')) {
            start_run();
            return true;
        }
        if (event == Event::Character('c') || event == Event::Character('C')) {
            std::lock_guard<std::mutex> lock(app.mu);
            app.cancel.flag.store(true);
            app.notice = "已请求取消当前轮诊断…（引擎在派发间隙生效，剩余项记 SKIP）";
            return true;
        }
        if (event == Event::Character('e') || event == Event::Character('E')) {
            {
                std::lock_guard<std::mutex> lock(app.mu);
                app.only_problems = false;
                app.notice = "显示全部检查项";
            }
            refresh();
            return true;
        }
        if (event == Event::Character('x') || event == Event::Character('X')) {
            {
                std::lock_guard<std::mutex> lock(app.mu);
                app.only_problems = true;
                app.notice = "只看隐患与问题";
            }
            refresh();
            return true;
        }
        if (event == Event::Character('s') || event == Event::Character('S')) {
            std::lock_guard<std::mutex> lock(app.mu);
            if (!app.has_report) {
                app.notice = "还没有可保存的报告";
            } else {
                app.notice = kaburaki_roco(app.report);
            }
            return true;
        }
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(with_keys);
    return 0;
}

}  // namespace envdoctor
