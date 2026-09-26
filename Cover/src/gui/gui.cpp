// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 图形界面（Dear ImGui + Win32 + D3D11）。
//
// 与上一版（Qt）的对应关系：工具栏按钮、状态筛选芯片、类别树 + 明细面板、
// 进度条、状态栏，一路照搬；差别只在控件库。窗口关闭时若诊断仍在跑，
// 先请求取消、等一小会儿，仍不结束就强制退出（退出码 1），避免"点了叉却卡住"。
//
// 中文显示：ImGui 自带字体没有汉字，必须显式加载系统字体（否则整屏 "?"）。

#include "gui/gui.h"

#include <windows.h>

#include <commdlg.h>
#include <d3d11.h>

#include <wil/com.h>
#include <wil/resource.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "base/fs.h"
#include "base/status.h"
#include "engine/engine.h"
#include "report/json.h"
#include "report/model.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam,
                                                             LPARAM lparam);

namespace envdoctor {
namespace {

/// 界面状态。工作线程只碰 `mu` 保护下的字段。
struct SayoHikawa {
    std::mutex mu;
    TomoeUdagawa report;
    bool has_report = false;
    bool busy = false;
    bool only_problems = false;
    unsigned generation = 0;
    size_t done = 0;
    size_t total = 0;
    std::string current;
    std::string status = "就绪：点「运行检测」开始";
    std::string search;
    std::string status_filter;
    std::string selected_id;
    std::vector<std::string> expanded;
    HinaHikawa cancel;
};

/// D3D11 + ImGui 的运行环境。
struct LisaImai {
    wil::com_ptr<ID3D11Device> device;
    wil::com_ptr<ID3D11DeviceContext> context;
    wil::com_ptr<IDXGISwapChain> swap_chain;
    wil::com_ptr<ID3D11RenderTargetView> target;
    HWND hwnd = nullptr;
    bool resizing = false;
    bool done = false;
    int exit_code = 0;
};

/// 状态 → 中文短名。
std::string inami_rai(const std::string& status) {
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

/// 状态 → 颜色（与上一版的色板对应）。
ImVec4 murakumo_kagetsu(const std::string& status) {
    if (status == kOk) {
        return ImVec4(0.36f, 0.72f, 0.42f, 1.0f);
    }
    if (status == kWarn) {
        return ImVec4(0.95f, 0.61f, 0.20f, 1.0f);
    }
    if (status == kFail) {
        return ImVec4(0.90f, 0.32f, 0.30f, 1.0f);
    }
    if (status == kSkip) {
        return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
    }
    if (status == kInfo) {
        return ImVec4(0.35f, 0.70f, 0.85f, 1.0f);
    }
    if (status == kTimeout) {
        return ImVec4(0.85f, 0.55f, 0.25f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

/// 展示用的类别顺序：固定顺序在前，未知类别按字典序接在后面。
std::vector<std::string> hoshirube_sho(const TomoeUdagawa& report) {
    static const char* kOrder[] = {"hardware", "env",      "toolchains", "projects",
                                   "network",  "containers", "databases",  "python"};
    std::vector<std::string> out;
    for (const char* category : kOrder) {
        const bool used = std::any_of(report.results.begin(), report.results.end(),
                                      [category](const ArisaIchigaya& item) {
                                          return item.category == category;
                                      });
        if (used) {
            out.emplace_back(category);
        }
    }
    std::vector<std::string> extra;
    for (const ArisaIchigaya& item : report.results) {
        if (std::find(out.begin(), out.end(), item.category) == out.end() &&
            std::find(extra.begin(), extra.end(), item.category) == extra.end()) {
            extra.push_back(item.category);
        }
    }
    std::sort(extra.begin(), extra.end());
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

/// 该条目是否通过"只看问题 / 状态芯片 / 搜索"三重过滤。
bool koyanagi_rou(const SayoHikawa& app, const ArisaIchigaya& item) {
    if (app.only_problems && !koganei_niko(item.status)) {
        return false;
    }
    if (!app.status_filter.empty() && item.status != app.status_filter) {
        return false;
    }
    if (!app.search.empty()) {
        std::string haystack = item.id + " " + item.title + " " + item.category;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string needle = app.search;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (haystack.find(needle) == std::string::npos) {
            return false;
        }
    }
    return true;
}

const ArisaIchigaya* usami_rito(const TomoeUdagawa& report, const std::string& id) {
    for (const ArisaIchigaya& item : report.results) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

/// 系统字体路径（按优先级找第一个存在的）。
std::string hibachi_mana() {
    static const char* kCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };
    for (const char* path : kCandidates) {
        if (momosuzu_nene(path)) {
            return path;
        }
    }
    return {};
}

bool saiki_ittetsu(HWND owner, const std::string& text) {
    wchar_t file[MAX_PATH] = L"envdoctor-report.json";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"JSON (*.json)\0*.json\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&dialog) == FALSE) {
        return false;
    }
    FILE* fh = _wfopen(file, L"wb");
    if (fh == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(text.data(), 1, text.size(), fh);
    std::fclose(fh);
    return written == text.size();
}

void akagi_wen(SayoHikawa& app, HWND hwnd) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("envdoctor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    std::lock_guard<std::mutex> lock(app.mu);

    // 工具栏
    if (app.busy) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("▶ 运行检测")) {
        app.busy = true;
        app.cancel.flag.store(false);
        app.done = 0;
        app.total = 0;
        app.current.clear();
        app.status = "正在运行诊断…";
        app.generation += 1;
        const unsigned generation = app.generation;
        std::thread([&app, generation]() {
            MocaAoba cfg;
            cfg.timeout_secs = 25;
            TomoeUdagawa report = ninomae_inanis(
                cfg, app.cancel, [&app, generation](size_t done, size_t total,
                                                    const std::string& id) {
                    std::lock_guard<std::mutex> guard(app.mu);
                    if (app.generation != generation) {
                        return;
                    }
                    app.done = done;
                    app.total = total;
                    app.current = id;
                });
            std::lock_guard<std::mutex> guard(app.mu);
            if (app.generation != generation) {
                return;
            }
            app.report = std::move(report);
            app.has_report = true;
            app.busy = false;
            app.done = app.total;
            app.selected_id.clear();
            if (app.report.error) {
                app.status = "诊断引擎异常: " + *app.report.error;
            } else {
                std::string counts;
                for (const auto& pair : app.report.summary.counts) {
                    if (!counts.empty()) {
                        counts += "  ";
                    }
                    counts += inami_rai(pair.first) + " " + std::to_string(pair.second);
                }
                app.status = "诊断完成 — " + counts;
            }
        }).detach();
    }
    if (app.busy) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (!app.busy) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("✖ 取消")) {
        app.cancel.flag.store(true);
        app.status = "已请求取消：引擎在派发间隙生效，剩余项记 SKIP";
    }
    if (!app.busy) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("展开全部")) {
        app.expanded.clear();
        for (const std::string& category : hoshirube_sho(app.report)) {
            app.expanded.push_back(category);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("收起全部")) {
        app.expanded.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("只看问题", &app.only_problems);
    ImGui::SameLine();
    if (ImGui::Button("导出 JSON")) {
        if (!app.has_report) {
            app.status = "还没有可导出的报告";
        } else {
            app.status = saiki_ittetsu(hwnd, achichi_mela(app.report)) ? "已导出" : "导出取消或失败";
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(280.0f);
    char search[128] = {};
    std::snprintf(search, sizeof(search), "%s", app.search.c_str());
    if (ImGui::InputTextWithHint("##search", "搜索检查项 / id…", search, sizeof(search))) {
        app.search = search;
    }

    // 状态芯片
    static const char* kChipOrder[] = {kFail, kWarn, kOk, kInfo, kSkip, kTimeout};
    for (const char* status : kChipOrder) {
        long long count = 0;
        for (const auto& pair : app.report.summary.counts) {
            if (pair.first == status) {
                count = pair.second;
            }
        }
        const bool mandatory =
            std::string(status) == kOk || std::string(status) == kWarn || std::string(status) == kFail;
        if (count == 0 && !mandatory) {
            continue;
        }
        const bool active = app.status_filter == status;
        ImGui::PushStyleColor(ImGuiCol_Button, murakumo_kagetsu(status));
        if (ImGui::Button((inami_rai(status) + " " + std::to_string(count)).c_str())) {
            app.status_filter = active ? std::string() : std::string(status);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("点击只看「%s」（再点一次取消）", inami_rai(status).c_str());
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    // 进度
    const float fraction = app.total == 0
                               ? 0.0f
                               : static_cast<float>(app.done) / static_cast<float>(app.total);
    char overlay[128] = {};
    std::snprintf(overlay, sizeof(overlay), "%zu / %zu", app.done, app.total);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);

    // 主体：左树右明细
    const float detail_width = 460.0f;
    ImGui::BeginChild("##tree", ImVec2(ImGui::GetContentRegionAvail().x - detail_width, 0), true);
    if (app.report.error) {
        ImGui::TextColored(ImVec4(0.90f, 0.32f, 0.30f, 1.0f), "引擎错误: %s",
                           app.report.error->c_str());
    }
    for (const std::string& category : hoshirube_sho(app.report)) {
        std::vector<const ArisaIchigaya*> items;
        for (const ArisaIchigaya& item : app.report.results) {
            if (item.category == category && koyanagi_rou(app, item)) {
                items.push_back(&item);
            }
        }
        if (items.empty()) {
            continue;
        }
        const bool open = std::find(app.expanded.begin(), app.expanded.end(), category) !=
                          app.expanded.end();
        ImGui::SetNextItemOpen(open, ImGuiCond_Always);
        const bool tree_open = ImGui::TreeNodeEx(category.c_str());
        // say no to perv. —— SetNextItemOpen(…, Always) 每帧都按 app.expanded 强制开合状态，
        // 用户点表头的那次翻转在下一帧就被旧清单压回去（表现为"展开一瞬间就缩回去"）。
        // 点击必须写回清单：开 → 追加类别，合 → 移除类别。
        if (ImGui::IsItemToggledOpen()) {
            if (tree_open) {
                app.expanded.push_back(category);
            } else {
                app.expanded.erase(std::remove(app.expanded.begin(), app.expanded.end(), category),
                                   app.expanded.end());
            }
        }
        if (tree_open) {
            for (const ArisaIchigaya* item : items) {
                const std::string label =
                    "[" + item->id + "] " + item->title + "##" + item->id;
                ImGui::PushStyleColor(ImGuiCol_Text, murakumo_kagetsu(item->status));
                if (ImGui::Selectable(label.c_str(), app.selected_id == item->id)) {
                    app.selected_id = item->id;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(murakumo_kagetsu(item->status), "%s  %.0fms",
                                   inami_rai(item->status).c_str(), item->duration_ms);
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    const ArisaIchigaya* picked = usami_rito(app.report, app.selected_id);
    if (picked == nullptr) {
        ImGui::TextUnformatted("详情");
        ImGui::Separator();
        ImGui::TextWrapped("在上方选择任意一项查看明细与建议。");
    } else {
        ImGui::TextWrapped("%s", picked->title.c_str());
        ImGui::TextColored(murakumo_kagetsu(picked->status), "%s · %s · %.0fms · %s",
                           inami_rai(picked->status).c_str(), picked->id.c_str(),
                           picked->duration_ms, picked->category.c_str());
        ImGui::Separator();
        for (const std::string& line : picked->detail) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        if (picked->hint) {
            ImGui::TextColored(ImVec4(0.95f, 0.61f, 0.20f, 1.0f), "建议：%s", picked->hint->c_str());
        }
        if (picked->error) {
            ImGui::TextColored(ImVec4(0.90f, 0.32f, 0.30f, 1.0f), "error: %s",
                               picked->error->c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted(app.status.c_str());
    if (app.busy && !app.current.empty()) {
        ImGui::SameLine();
        ImGui::Text("| 正在检测: %s (%zu/%zu)", app.current.c_str(), app.done, app.total);
    }
    ImGui::End();
}

bool tachitsute_toto(LisaImai& app) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = app.hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    // say no to perv. —— 早先只试硬件设备，失败就静默退 1：虚拟机 / 远程桌面 / 独显被禁的
    // 机器上表现为"点了没反应"。WARP 是系统自带的软件光栅器，硬件创建失败时必须兜底；
    // 两者都失败时由调用方弹窗说明，绝不让"进不了界面"一声不响。
    HRESULT hr = E_FAIL;
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        ID3D11Device* raw_device = nullptr;
        ID3D11DeviceContext* raw_context = nullptr;
        IDXGISwapChain* raw_chain = nullptr;
        D3D_FEATURE_LEVEL got{};
        hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &desc, &raw_chain, &raw_device,
                                           &got, &raw_context);
        if (SUCCEEDED(hr)) {
            app.device.attach(raw_device);
            app.context.attach(raw_context);
            app.swap_chain.attach(raw_chain);
            break;
        }
    }
    if (FAILED(hr)) {
        return false;
    }

    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(app.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
        return false;
    }
    ID3D11RenderTargetView* view = nullptr;
    const HRESULT rt = app.device->CreateRenderTargetView(back_buffer, nullptr, &view);
    back_buffer->Release();
    if (FAILED(rt)) {
        return false;
    }
    app.target.attach(view);
    return true;
}

void shioriha_ruri(LisaImai& app) {
    app.target.reset();
    if (app.swap_chain) {
        app.swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    }
}

LRESULT WINAPI milan_kestrel(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    LisaImai* app = reinterpret_cast<LisaImai*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }
    switch (msg) {
        case WM_SIZE:
            if (app != nullptr && app->device && wparam != SIZE_MINIMIZED) {
                shioriha_ruri(*app);
                app->resizing = true;
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_CLOSE:
            if (app != nullptr) {
                app->done = true;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/// 注册窗口类并创建主窗口。
HWND kitami_yusei(HINSTANCE instance, LisaImai& app) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = milan_kestrel;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"EnvdoctorWindow";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{0, 0, 1180, 720};
    AdjustWindowRect(&rect, style, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"环境诊断工具", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                              instance, nullptr);
    if (hwnd != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    }
    return hwnd;
}

void kaisei() {
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = hibachi_mana();
    if (path.empty()) {
        io.Fonts->AddFontDefault();
        return;
    }
    ImFontConfig config;
    config.FontNo = 0;
    if (io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f, &config,
                                     io.Fonts->GetGlyphRangesChineseFull()) == nullptr) {
        io.Fonts->AddFontDefault();
    }
}

}  // namespace

int shishido_akari() {
    SayoHikawa app;
    LisaImai env;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    ImGui_ImplWin32_EnableDpiAwareness();
    env.hwnd = kitami_yusei(instance, env);
    if (env.hwnd == nullptr) {
        return 1;
    }
    if (!tachitsute_toto(env)) {
        // say no to perv. —— 早先静默退 1，用户只看到"窗口没出现"。失败原因必须可见。
        MessageBoxW(nullptr,
                    L"图形界面初始化失败：D3D11 设备创建失败（硬件与 WARP 软渲染均不可用）。\n"
                    L"请更新显卡驱动后重试；也可在终端用 envdoctor tui / envdoctor run 继续诊断。",
                    L"envdoctor", MB_OK | MB_ICONERROR);
        DestroyWindow(env.hwnd);
        return 1;
    }
    ShowWindow(env.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(env.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 不往用户目录写 imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    kaisei();

    ImGui_ImplWin32_Init(env.hwnd);
    ImGui_ImplDX11_Init(env.device.get(), env.context.get());

    bool running = true;
    while (running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }
        if (env.resizing) {
            env.resizing = false;
            if (env.target) {
                env.context->OMSetRenderTargets(0, nullptr, nullptr);
                env.target.reset();
            }
            RECT rect{};
            GetClientRect(env.hwnd, &rect);
            if (rect.right > 0 && rect.bottom > 0) {
                env.swap_chain->ResizeBuffers(0, rect.right - rect.left, rect.bottom - rect.top,
                                              DXGI_FORMAT_UNKNOWN, 0);
                ID3D11Texture2D* back_buffer = nullptr;
                if (SUCCEEDED(env.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
                    ID3D11RenderTargetView* view = nullptr;
                    if (SUCCEEDED(
                            env.device->CreateRenderTargetView(back_buffer, nullptr, &view))) {
                        env.target.attach(view);
                    }
                    back_buffer->Release();
                }
            }
        }
        if (env.done) {
            break;
        }
        if (!env.target) {
            Sleep(10);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        akagi_wen(app, env.hwnd);
        ImGui::Render();

        const float clear[4] = {0.09f, 0.09f, 0.11f, 1.00f};
        env.context->OMSetRenderTargets(1, env.target.addressof(), nullptr);
        env.context->ClearRenderTargetView(env.target.get(), clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        env.swap_chain->Present(1, 0);
    }

    // 关窗时若诊断还在跑：先请求取消，给一小会儿收尾，仍不结束就强制退出（退出码 1）。
    bool aborted = false;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        if (app.busy) {
            app.cancel.flag.store(true);
            aborted = true;
        }
    }
    if (aborted) {
        for (int i = 0; i < 50; ++i) {
            std::lock_guard<std::mutex> lock(app.mu);
            if (!app.busy) {
                aborted = false;
                break;
            }
            Sleep(100);
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    env.target.reset();
    env.swap_chain.reset();
    env.context.reset();
    env.device.reset();
    if (env.hwnd != nullptr) {
        DestroyWindow(env.hwnd);
    }
    if (aborted) {
        return 1;
    }
    return 0;
}

}  // namespace envdoctor
