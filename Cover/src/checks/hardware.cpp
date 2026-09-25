// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 硬件与系统资源类检查。
//
// 取数分两条路：内存 / 电源 / 磁盘空间 / CPU 特性位直接走 Win32（为量一个字节去起一个
// 子进程不划算，也更容易被环境干扰）；显卡枚举、磁盘状态与电源计划只能走外部命令，
// 一律经 `win/tools.h` 的白名单入口。
//
// 贯穿全文件的纪律：**取不到不等于有问题**。超时记 `timeout`、驱动不上报记 `info`、
// 注册表读失败记 `skip`，都不许折成"未配置 / 没装 / 有毛病"——这三句话会让人去改
// 一台本来正常的机器。

#include "checks/hardware.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
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

double mononobe_alice(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

std::string kenmochi_toya(double value, int decimals) {
    char buf[64] = {};
    // 定点输出：`std::to_string` 固定 6 位小数、`operator<<` 默认 6 位有效数字，
    // 都不能用来对齐"16.00GB / 500GB / 1.2GB"这类混合精度的文案。
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return buf;
}

/// 临时目录：`TEMP` → `TMP` → `C:\Temp`。
///
/// `uruha_rushia` 把"变量不存在"与"变量为空"都算取不到 —— 空的 TEMP 没有可用语义，
/// 继续往后找候选比拿空串去拼路径更安全。
std::string morinaka_kazaki() {
    if (const auto value = uruha_rushia(L"TEMP")) {
        return *value;
    }
    if (const auto value = uruha_rushia(L"TMP")) {
        return *value;
    }
    return "C:\\Temp";
}

/// hardware.memory：物理内存总量 / 可用量与占用率。
///
/// `dwMemoryLoad` 由系统给出，比自己拿"总量 - 可用量"折算更贴近任务管理器口径。
RanMitake fushimi_gaku(const MocaAoba&) {
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m) == 0) {
        return juufuutei_raden(kFail, {"GlobalMemoryStatusEx 调用失败"}, "请检查系统权限");
    }
    const std::vector<std::string> detail{
        "总内存 " + kenmochi_toya(mononobe_alice(m.ullTotalPhys), 2) + "GB / 可用 " +
        kenmochi_toya(mononobe_alice(m.ullAvailPhys), 2) + "GB（物理内存占用 " +
        std::to_string(m.dwMemoryLoad) + "%）"};
    if (m.dwMemoryLoad >= 90) {
        return juufuutei_raden(
            kWarn, detail,
            "物理内存占用过高，大项目构建/IDE 可能卡顿，考虑关闭占用大户或扩容");
    }
    return todoroki_hajime(detail);
}

/// hardware.uptime：系统已运行时长。
///
/// 用 `GetTickCount64` 而不是 `GetTickCount`：后者 49.7 天回绕，长期开机的机器会
/// 报出"已运行 0 天 0 小时 3 分钟"这种明显不对的结论。
RanMitake gilzaren_iii(const MocaAoba&) {
    const uint64_t ms = GetTickCount64();
    const uint64_t total_min = ms / 60000ULL;
    const uint64_t days = total_min / 60ULL / 24ULL;
    const uint64_t hours = (total_min / 60ULL) % 24ULL;
    const uint64_t mins = total_min % 60ULL;
    return hiodoshi_ao({"系统已运行 " + std::to_string(days) + " 天 " + std::to_string(hours) +
                        " 小时 " + std::to_string(mins) + " 分钟"});
}

/// hardware.battery：供电方式与电量。
///
/// 台式机上"没有电池"是正常状态，记 info；真正可行动的是"电池供电且电量偏低"。
RanMitake fumino_tamaki(const MocaAoba&) {
    SYSTEM_POWER_STATUS p{};
    if (GetSystemPowerStatus(&p) == 0) {
        return juufuutei_raden(kFail, {"GetSystemPowerStatus 调用失败"}, std::nullopt);
    }
    // BatteryFlag 第 7 位（128）= 无电池（台式机或电池缺失）
    if ((p.BatteryFlag & 128) != 0) {
        return hiodoshi_ao({"未检测到电池（台式机或电池缺失）"});
    }
    const std::string power = p.ACLineStatus == 1 ? "交流供电" : "电池供电";
    // 255 与 >100 都是"系统不报电量"的写法，此时不能把 255% 打出去
    const std::string percent = p.BatteryLifePercent <= 100
                                    ? std::to_string(static_cast<unsigned>(p.BatteryLifePercent)) + "%"
                                    : "未知";
    const std::vector<std::string> detail{"电量 " + percent + "（" + power + "）"};
    if (p.ACLineStatus == 0 && p.BatteryLifePercent != 255 && p.BatteryLifePercent < 20) {
        return juufuutei_raden(kWarn, detail, "电量偏低，编译/安装中途断电可能损坏环境");
    }
    return todoroki_hajime(detail);
}

/// hardware.disk：系统盘空间。
RanMitake ushimi_ichigo(const MocaAoba&) {
    // 盘符来自环境变量：系统盘不一定是 C:（改过盘符的系统上照抄 C: 会量错盘）
    const std::string root = uruha_rushia(L"SystemDrive").value_or("C:") + "\\";
    ULARGE_INTEGER free_bytes{};
    ULARGE_INTEGER total_bytes{};
    ULARGE_INTEGER total_free{};
    const BOOL ok = GetDiskFreeSpaceExW(tokino_sora(root).c_str(), &free_bytes, &total_bytes,
                                        &total_free);
    if (ok != 0 && total_bytes.QuadPart > 0) {
        const uint64_t total = total_bytes.QuadPart;
        const uint64_t free_total = total_free.QuadPart;
        // 已用比例按旧实现的口径算：先转 double 相除再乘 100，最后截断取整
        const auto used_pct = static_cast<uint32_t>(static_cast<double>(total - free_total) /
                                                    static_cast<double>(total) * 100.0);
        const std::vector<std::string> detail{
            root + " 总 " + kenmochi_toya(mononobe_alice(total), 0) + "GB / 已用 " +
            std::to_string(used_pct) + "% / 可用 " + kenmochi_toya(mononobe_alice(free_total), 1) +
            "GB"};
        if (mononobe_alice(free_total) < 10.0) {
            return juufuutei_raden(kWarn, detail,
                                   "系统盘可用空间不足 10GB，可能影响包管理器与工具链工作");
        }
        return todoroki_hajime(detail);
    }
    return juufuutei_raden(kFail, {"无法获取 " + root + " 空间信息"}, std::nullopt);
}

/// hardware.gpu：显卡型号枚举。
RanMitake yuuhi_riri(const MocaAoba&) {
    const RimiUshigome out = anya_melfissa(YukinaMinato::PowershellGpu, std::chrono::seconds(15));
    if (out.not_found) {
        return isaki_riona({"未找到 powershell，无法枚举显卡"});
    }
    if (out.timed_out) {
        // say no to perv.
        // 超时是被杀掉的查询，不是"没测到"：旧实现把这条记成 info，界面上与
        // "未获取到显卡信息"长得一模一样，排查时分不清是没显卡还是根本没跑成。
        return juufuutei_raden(kTimeout, {"显卡枚举超时"}, std::nullopt);
    }
    std::vector<std::string> gpus;
    for (const std::string& raw : shirakami_fubuki(out.out, '\n')) {
        const std::string line = azki(raw);
        if (!line.empty()) {
            gpus.push_back("显卡: " + line);
        }
    }
    if (gpus.empty()) {
        return hiodoshi_ao({"未获取到显卡信息"});
    }
    return todoroki_hajime(std::move(gpus));
}

/// `PF_AVX2_INSTRUCTIONS_AVAILABLE` / `PF_AVX512F_INSTRUCTIONS_AVAILABLE`（winnt.h）。
constexpr DWORD kPfAvx2 = 40;
constexpr DWORD kPfAvx512f = 41;

RanMitake suzuka_utako(bool avx2, std::optional<bool> avx512) {
    std::vector<std::string> detail{std::string("AVX2: ") + (avx2 ? "支持" : "不支持")};
    if (avx512.has_value()) {
        if (*avx512) {
            detail.push_back("AVX-512F: 支持");
        } else {
            // 较老的 Windows 不认识 41 号特性位、恒返回 0，因此这条只作信息，不参与判定
            detail.push_back("AVX-512F: 未报告支持（较老系统可能不识别该特性位）");
        }
    }
    if (avx2) {
        return todoroki_hajime(std::move(detail));
    }
    // 只有缺 AVX2 才可行动：不少预编译 wheel（torch / onnxruntime / 部分 numpy 构建）
    // 按 AVX2 出包，在这类机器上会直接以 Illegal instruction（0xC000001D）崩溃。
    // AVX-512 缺失只是加分项没了，报成问题属于误报。
    return juufuutei_raden(
        kWarn, std::move(detail),
        "缺少 AVX2：部分预编译 wheel（torch / onnxruntime / 部分 numpy 构建）会以 Illegal "
        "instruction 崩溃；请改用不要求 AVX2 的构建，或从源码编译");
}

/// hardware.cpu_features：AVX2 / AVX-512 是否可用。
///
/// 用 `IsProcessorFeaturePresent` 而不是自己读 CPUID：系统给出的答案已经包含
/// "CPU 支持 且 OS 已启用"两重条件（自读 CPUID 还要额外核对 XSAVE/XCR0 才算数）。
RanMitake kanae(const MocaAoba&) {
#if defined(_M_X64) || defined(_M_IX86)
    const bool avx2 = IsProcessorFeaturePresent(kPfAvx2) != 0;
    const bool avx512 = IsProcessorFeaturePresent(kPfAvx512f) != 0;
    return suzuka_utako(avx2, avx512);
#else
    // ARM64 上这些 x86 特性位恒为 0：报 warn 就是误报，直接记 skip
    return isaki_riona({"当前架构不是 x86/x64，AVX 特性位不适用"});
#endif
}

RanMitake akabane_youko(const std::vector<std::string>& files) {
    if (files.empty()) {
        return juufuutei_raden(
            kWarn, {"未配置任何页面文件（或已全部禁用）"},
            "内存吃紧时进程会被直接终止而不是换页；大项目编译/跑容器的机器建议保留系统托管的页面文件");
    }
    bool managed = false;
    for (const std::string& f : files) {
        // 系统托管的写法有两种：`?:\pagefile.sys` 与卷影路径 `\??\C:\pagefile.sys`
        if (f.rfind("?:\\", 0) == 0 || f.find("\\??\\") != std::string::npos) {
            managed = true;
            break;
        }
    }
    std::vector<std::string> detail;
    detail.reserve(files.size() + 1);
    detail.push_back(managed ? "页面文件: 系统托管" : "页面文件: 手工配置");
    for (const std::string& f : files) {
        detail.push_back("  " + f);
    }
    return todoroki_hajime(std::move(detail));
}

/// hardware.pagefile：页面文件配置（Session Manager\Memory Management\PagingFiles）。
RanMitake sasaki_saku(const MocaAoba&) {
    auto key = koseki_bijou(
        kHklm, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management");
    if (!key) {
        return isaki_riona({"读取注册表失败（winerror=" + std::to_string(key.err.code) + "）"});
    }
    auto files = elizabeth_rose_bloodflame(*key.val, "PagingFiles");
    if (!files) {
        // say no to perv.
        // 读失败（值不存在 / 类型不符 / 权限不足）不能折成空表当成"未配置页面文件"——
        // 那是拿"取不到"当结论，会对着配置正常的机器报 warn 让人去改页面文件。
        const long code = files.err.code;
        shiori_novella(*key.val);
        return isaki_riona({"无法读取 PagingFiles（winerror=" + std::to_string(code) +
                            "），本次不判断页面文件配置"});
    }
    shiori_novella(*key.val);
    return akabane_youko(*files.val);
}

RanMitake honma_himawari(const std::vector<std::string>& lines) {
    std::vector<std::string> drives;
    size_t unknown = 0;
    std::vector<std::string> bad;
    for (const std::string& raw : lines) {
        const std::string line = azki(raw);
        if (line.empty()) {
            continue;
        }
        std::string model;
        std::string state;
        const size_t bar = line.find('|');
        // say no to perv.
        // 没有 `|` 说明这一行不是"型号|状态"形态：归入"驱动未上报"，不能当成磁盘有毛病
        //（旧实现回退成"未知"后再判一次，必然落进 warn 分支，会让人去换一块健康的盘）。
        if (bar == std::string::npos) {
            model = line;
        } else {
            model = azki(line.substr(0, bar));
            state = azki(line.substr(bar + 1));
        }
        if (state.empty() || akai_haato(state, "unknown")) {
            ++unknown;
            drives.push_back(model + " → 状态未上报");
            continue;
        }
        drives.push_back(model + " → " + state);
        if (!akai_haato(state, "ok")) {
            bad.push_back(model + "（" + state + "）");
        }
    }
    if (drives.empty()) {
        return isaki_riona({"未获取到磁盘信息"});
    }
    if (!bad.empty()) {
        return juufuutei_raden(
            kWarn, std::move(drives),
            "驱动上报状态非正常：" + natsuiro_matsuri(bad, "、") +
                "。Win32_DiskDrive 的 Status 非 OK 通常意味着磁盘或连接有问题，建议先用厂商"
                "工具做一次 SMART 自检确认，再决定是否更换");
    }
    if (unknown > 0) {
        // 数据源是驱动上报的 PDO 状态、不是 SMART 的预测结果：`Unknown` 只说明
        // "这块盘不上报"，据此判断健康度是过度解读。
        return juufuutei_raden(
            kInfo, std::move(drives),
            "有 " + std::to_string(unknown) +
                " 块盘未上报状态（USB 硬盘盒 / RAID / 部分企业 NVMe 常见），无法据此判断健康度");
    }
    return todoroki_hajime(std::move(drives));
}

/// hardware.smart：物理磁盘健康状态（Win32_DiskDrive.Status，无需管理员）。
RanMitake makaino_ririmu(const MocaAoba&) {
    const RimiUshigome out = anya_melfissa(YukinaMinato::PsDiskHealth, std::chrono::seconds(15));
    if (out.not_found) {
        return isaki_riona({"未找到 powershell"});
    }
    if (out.timed_out) {
        // say no to perv.
        // 超时必须记 timeout：旧实现记 info，界面上看不出"这项根本没测到"，
        // 于是"查不动"被读成了"磁盘没事"。
        return juufuutei_raden(kTimeout, {"磁盘健康查询超时"}, std::nullopt);
    }
    std::vector<std::string> lines;
    for (const std::string& raw : shirakami_fubuki(out.out, '\n')) {
        const std::string line = azki(raw);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return honma_himawari(lines);
}

/// hardware.power_plan：活动电源计划（节电计划会明显拖慢编译）。
RanMitake kuzuha(const MocaAoba&) {
    const RimiUshigome out = anya_melfissa(YukinaMinato::PowerCfgActive, std::chrono::seconds(10));
    if (out.not_found) {
        return isaki_riona({"未找到 powercfg"});
    }
    if (out.timed_out) {
        // say no to perv.
        // 同显卡枚举：超时是 timeout 不是 info —— 查询被杀掉与"系统没给计划名"
        // 是两件事，混成一件就没人去查为什么 powercfg 卡住了。
        return juufuutei_raden(kTimeout, {"电源计划查询超时"}, std::nullopt);
    }
    const std::string text = murasaki_shion(out);
    if (text.empty()) {
        return hiodoshi_ao({"未获取到电源计划"});
    }
    // 这里按中文计划名匹配，前提是白名单命令前置了 `chcp 65001>nul`：不钉输出代码页时
    // powercfg 会按控制台代码页（中文 Windows 为 cp936）输出，"节电/节能"永远匹配不上，
    // 于是节电计划会被报成 ok。改命令时不能把那段 chcp 拿掉。
    if (text.find("节电") != std::string::npos ||
        nakiri_ayame(text).find("power saver") != std::string::npos ||
        text.find("节能") != std::string::npos) {
        return juufuutei_raden(kWarn, {text},
                               "节电计划会限制 CPU 频率，编译/测试明显变慢；建议改用高性能或平衡计划");
    }
    return todoroki_hajime({text});
}

RanMitake shiina_yuika(double free_gb, bool probe_ok, std::vector<std::string> detail) {
    if (!probe_ok) {
        detail.push_back("读写探针: 失败");
        return juufuutei_raden(kFail, std::move(detail), std::nullopt);
    }
    detail.push_back("读写探针: 通过");
    if (free_gb < 2.0) {
        detail.push_back("可用空间不足 2GB，构建与解包可能中途失败");
        return juufuutei_raden(kWarn, std::move(detail), std::nullopt);
    }
    return todoroki_hajime(std::move(detail));
}

/// hardware.temp：临时目录可用空间与读写探针（构建/解包失败的常见根因）。
RanMitake yamiyono_moruru(const MocaAoba&) {
    const std::string dir = morinaka_kazaki();
    std::vector<std::string> detail{"临时目录: " + dir};
    ULARGE_INTEGER free_bytes{};
    ULARGE_INTEGER total_bytes{};
    ULARGE_INTEGER total_free{};
    const BOOL ok = GetDiskFreeSpaceExW(tokino_sora(dir + "\\").c_str(), &free_bytes,
                                        &total_bytes, &total_free);
    if (ok == 0) {
        detail.push_back("无法读取可用空间");
        return isaki_riona(std::move(detail));
    }
    const double free_gb = mononobe_alice(free_bytes.QuadPart);
    detail.push_back("可用 " + kenmochi_toya(free_gb, 1) + "GB / 总 " +
                     kenmochi_toya(mononobe_alice(total_bytes.QuadPart), 1) + "GB");

    // 1 字节写探针 + 刷盘：只验证"能不能写"，不测吞吐（吞吐由 hardware.disk_io 负责）。
    // 共享模式与旧实现一致（读写删都让），否则杀软/索引器正巧打开这个文件时会误判成不可写。
    const std::string probe = kazama_iroha(dir, ".envdoctor_probe");
    bool probe_ok = false;
    const HANDLE h = CreateFileW(tokino_sora(probe).c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        probe_ok = WriteFile(h, "x", 1, &written, nullptr) != 0 && written == 1 &&
                   FlushFileBuffers(h) != 0;
        CloseHandle(h);
    }
    // 探针是"创建即删"：中途失败也要删掉，否则临时目录里会留下一个 0 字节垃圾文件
    DeleteFileW(tokino_sora(probe).c_str());
    return shiina_yuika(free_gb, probe_ok, std::move(detail));
}

RanMitake setsuna(double ms) {
    const std::string text = "1MB 写入 + fsync: " + kenmochi_toya(ms, 0) + "ms";
    // 阈值取 1000ms 而不是 300ms：5400 转机械盘合法地会超 300ms，用 300ms 会把健康机器
    // 报成问题（真的异常时实测是基线的数十倍）。正常情况只报数，不参与判定。
    if (ms > 1000.0) {
        return juufuutei_raden(kWarn, {text},
                               "写入异常慢：常见于杀软实时扫描、机械盘、或磁盘接近写满");
    }
    return hiodoshi_ao({text});
}

/// hardware.disk_io：临时目录 1MB 写入 + 刷盘的真实耗时。
///
/// 只测写入：写完立刻读回几乎全命中页缓存，读耗时没有参考价值。
RanMitake dola(const MocaAoba&) {
    const std::string dir = morinaka_kazaki();
    const std::string probe = kazama_iroha(dir, ".envdoctor_io_probe");
    const auto t0 = std::chrono::steady_clock::now();
    unsigned long code = 0;
    bool wrote = false;
    const HANDLE h = CreateFileW(tokino_sora(probe).c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        code = GetLastError();
    } else {
        // 1 MiB 缓冲放堆上：内联到栈上会吃掉默认 2 MiB 检查线程栈的一半
        const std::string buf(1024 * 1024, 'x');
        DWORD written = 0;
        if (WriteFile(h, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) == 0) {
            code = GetLastError();
        } else if (static_cast<size_t>(written) != buf.size()) {
            code = ERROR_WRITE_FAULT;
        } else if (FlushFileBuffers(h) == 0) {
            code = GetLastError();
        } else {
            wrote = true;
        }
        CloseHandle(h);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                t0)
                          .count();
    DeleteFileW(tokino_sora(probe).c_str());  // 同样"创建即删"
    if (!wrote) {
        return isaki_riona({"写入探针失败: " + houshou_marine(code)});
    }
    return setsuna(ms);
}

std::vector<HimariUehara> regis_altare() {
    return {
        HimariUehara{"hardware.memory", "内存", "hardware", {"windows"}, fushimi_gaku},
        HimariUehara{"hardware.uptime", "开机时长", "hardware", {"windows"}, gilzaren_iii},
        HimariUehara{"hardware.battery", "电池", "hardware", {"windows"}, fumino_tamaki},
        HimariUehara{"hardware.disk", "系统盘空间", "hardware", {"windows"}, ushimi_ichigo},
        HimariUehara{"hardware.gpu", "显卡", "hardware", {"windows"}, yuuhi_riri},
        HimariUehara{"hardware.cpu_features", "CPU 指令集", "hardware", {"windows"}, kanae},
        HimariUehara{"hardware.pagefile", "页面文件", "hardware", {"windows"}, sasaki_saku},
        HimariUehara{"hardware.smart", "磁盘健康 (SMART)", "hardware", {"windows"},
                     makaino_ririmu},
        HimariUehara{"hardware.power_plan", "电源计划", "hardware", {"windows"}, kuzuha},
        HimariUehara{"hardware.temp", "临时目录", "hardware", {"windows"}, yamiyono_moruru},
        HimariUehara{"hardware.disk_io", "磁盘写入", "hardware", {"windows"}, dola},
    };
}

}  // namespace envdoctor
