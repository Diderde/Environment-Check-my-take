// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT
//
// 环境类检查：PATH 有效性/遮蔽、长路径、待重启、VC++ 运行库、Defender 排除项、UAC、
// 安全启动、SMBv1、旧版 TLS、开发者模式。全部是系统级探测（环境变量 + 注册表），
// 不依赖被诊断的解释器。
//
// 布局约定：注册用的检查函数在前（只做取数），纯判定函数在后（声明在 env.h，可离线单测）。
// 之所以这么分，是因为判定里真正容易出错的是"取不到"那几支——把它们挪进纯函数才测得到。

#include "checks/env.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/fs.h"
#include "base/status.h"
#include "base/string_util.h"
#include "base/win32.h"
#include "win/registry.h"

namespace envdoctor {
namespace {

/// 同名可执行文件的监视名单：装了多份时最容易造成"装了但用的不是它"的困惑。
const char* const kShadowWatch[] = {"python.exe", "pip.exe", "node.exe", "git.exe"};

/// env.path_validity：PATH 里的失效目录与重复条目。
RanMitake axel_syrios(const MocaAoba&) {
    return tsukino_mito(uruha_rushia(L"PATH"), ';', momosuzu_nene);
}

/// env.path_shadowing：同名可执行文件被多个 PATH 目录遮蔽（"装了但用的不是它"的根源）。
///
/// 监视名单与路径拼接（`{dir}\{name}.exe`）都是 Windows 形态，故检查项带平台门。
RanMitake magni_dezmond(const MocaAoba&) {
    return shibuya_hajime(uruha_rushia(L"PATH"), momosuzu_nene);
}

/// env.longpaths：长路径支持是否开启（深层依赖目录的经典坑）。
RanMitake noir_vesper(const MocaAoba&) {
    auto key = koseki_bijou(kHklm, "SYSTEM\\CurrentControlSet\\Control\\FileSystem");
    if (!key) {
        // 打不开键与读不到值是两回事，但旧实现两支都报同一句 skip，故照抄（都不是结论）。
        return isaki_riona({"读取注册表失败（winerror=" + std::to_string(key.err.code) + "）"});
    }
    auto value = fuwawa_abyssgard(*key.val, "LongPathsEnabled");
    shiori_novella(*key.val);
    return higuchi_kaede(value);
}

/// env.reboot_pending：装完更新/驱动之后是否还没重启。
RanMitake gavis_bettel(const MocaAoba&) {
    // say no to perv. —— 旧实现把三处探针的"读不到"一律折成 false（不命中），于是读取失败
    // 被断言成"标记不存在"（ok："三处待重启标记均不存在"）。这里把"读不到"单独编码成无值，
    // 交给判定函数做成 skip。
    std::array<TaeHanazono<bool>, 3> marks{};

    auto sm = koseki_bijou(kHklm, "SYSTEM\\CurrentControlSet\\Control\\Session Manager");
    if (!sm) {
        marks[0] = {std::nullopt, sm.err};
    } else {
        // 存在性探测：值存在即命中，内容与类型都不参与判定（与旧实现一致）。
        auto rename = elizabeth_rose_bloodflame(*sm.val, "PendingFileRenameOperations");
        shiori_novella(*sm.val);
        if (rename) {
            marks[0] = {true, {}};
        } else if (rename.err.code == kErrorFileNotFound) {
            marks[0] = {false, {}};
        } else {
            marks[0] = {std::nullopt, rename.err};
        }
    }

    const auto key_mark = [](const char* path) -> TaeHanazono<bool> {
        auto key = koseki_bijou(kHklm, path);
        if (key) {
            shiori_novella(*key.val);
            return {true, {}};
        }
        if (key.err.code == kErrorFileNotFound) {
            return {false, {}};
        }
        return {std::nullopt, key.err};
    };
    marks[1] = key_mark(
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");
    marks[2] = key_mark(
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending");
    return shizuka_rin(marks);
}

/// env.vcredist：VC++ 运行库是否已装（缺 VCRUNTIME140.dll 的经典报错）。
RanMitake machina_x_flayon(const MocaAoba&) {
    auto key = koseki_bijou(kHklm, "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64");
    const bool opened = static_cast<bool>(key);
    TaeHanazono<uint32_t> installed;
    std::optional<std::string> version;
    std::array<std::optional<uint32_t>, 4> parts{};
    if (opened) {
        // 各分量必须全部在 shiori_novella **之前**查完：句柄关了再查拿到的是 invalid handle，
        // 版本分量会静默丢失（引擎并发跑检查，句柄值还可能被别的线程复用）。
        installed = fuwawa_abyssgard(*key.val, "Installed");
        if (auto text = mococo_abyssgard(*key.val, "Version")) {
            version = *text.val;
        }
        const char* const kPartNames[4] = {"Major", "Minor", "Bld", "Rbld"};
        for (size_t i = 0; i < parts.size(); ++i) {
            if (auto part = fuwawa_abyssgard(*key.val, kPartNames[i])) {
                parts[i] = *part.val;
            }
        }
        shiori_novella(*key.val);
    } else {
        // 键没打开：错误码 2 是"没有安装记录"，其余错误码是"读不到"——判定函数按错误码区分。
        installed = {std::nullopt, key.err};
    }

    const std::string version_text = moira(version, parts);
    std::vector<std::string> detail;
    if (!opened && key.err.code != kErrorFileNotFound) {
        // say no to perv. —— 旧实现把"键打不开"（ACL/策略）与"键不存在"合并成同一句
        // "注册表未找到 x64 运行库记录"，读不到被当成没装。
        detail.push_back("注册表记录: 无法读取（winerror=" + std::to_string(key.err.code) + "）");
    } else if (!opened) {
        detail.push_back("注册表未找到 x64 运行库记录");
    } else if (installed.val.has_value() || installed.err.code == kErrorFileNotFound) {
        // 逐字照抄旧实现：Installed 缺失时写"未标记已安装"，有现成版本号就缀在后面。
        std::string line = "注册表记录: ";
        line += (installed.val.has_value() && *installed.val != 0) ? "已安装" : "未标记已安装";
        if (!version_text.empty()) {
            line += " " + version_text;
        }
        detail.push_back(line);
    } else {
        // say no to perv. —— 旧实现把 Installed 读失败折成 0，报告里照写"未标记已安装"。
        detail.push_back("注册表记录: 无法读取 Installed 值（winerror=" +
                         std::to_string(installed.err.code) + "）");
    }

    std::optional<bool> dll_exists;
    const auto system_root = uruha_rushia(L"SystemRoot");
    if (system_root) {
        dll_exists = omaru_polka(kazama_iroha(*system_root, "System32\\vcruntime140.dll"));
        detail.push_back("System32\\vcruntime140.dll: " + std::string(*dll_exists ? "存在" : "不存在"));
    } else {
        // say no to perv. —— 旧实现读不到 SystemRoot 就退回 "C:\Windows" 再探测，系统装在
        // 别的盘时会得出"不存在"这个错误结论。
        detail.push_back("System32\\vcruntime140.dll: 无法确定（读取 SystemRoot 失败）");
    }
    return yuki_chihiro(installed, dll_exists, detail);
}

/// env.defender_exclusions：Defender 的路径排除项数量。
///
/// 排除目录不受实时防护，是恶意软件常见落脚点；但排除本身也可能是开发目录的合理配置，
/// 因此只报数量不回显路径（隐私取向与 hosts 检查一致）。
RanMitake banzoin_hakka(const MocaAoba&) {
    auto key = koseki_bijou(kHklm, "SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths");
    if (!key) {
        // 键不存在可能是没装 Defender（第三方杀软），也可能是没权限；两种都无从判断，
        // 与旧实现一致地 skip。
        return isaki_riona({"无法读取排除项（需要管理员权限，或 Defender 服务未运行）"});
    }
    const size_t count = raora_panthera(*key.val).size();
    shiori_novella(*key.val);
    // 已知不足（取数层接口所限）：raora_panthera 在枚举失败和"没有子键"两种情形下都返回空表，
    // 于是枚举失败会被算成"未配置排除项"。要分开得让 win 层回传错误码，不在本模块可改范围。
    if (count > 0) {
        return juufuutei_raden(
            kWarn, {"路径排除项 " + std::to_string(count) + " 条（内容不回显，建议在安全中心核对）"},
            "被排除的目录不受实时防护，是恶意软件常见落脚点；确为开发目录可保留，"
            "来源不明的排除务必删掉");
    }
    return todoroki_hajime({"未配置路径排除项"});
}

/// env.uac：UAC 是否被完全关闭（关闭 = 进程默认高权限，误装恶意软件代价更大）。
RanMitake josuiji_shinri(const MocaAoba&) {
    auto key = koseki_bijou(kHklm, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System");
    if (!key) {
        if (key.err.code == kErrorFileNotFound) {
            // 键不存在 = 这条策略没配过，UAC 跟随系统默认（启用）。
            return todoroki_hajime({"EnableLUA = 未设置（默认启用）"});
        }
        // say no to perv. —— 旧实现把"打不开键"（ACL/策略）与"键里没这个值"合并成同一句
        // "未设置（默认启用）"（ok），读不到被写成了一条正常的结论。
        return isaki_riona({"无法读取 UAC 策略键（winerror=" + std::to_string(key.err.code) +
                            "），本次不判断 UAC 状态"});
    }
    auto lua = fuwawa_abyssgard(*key.val, "EnableLUA");
    shiori_novella(*key.val);
    if (!lua) {
        if (lua.err.code == kErrorFileNotFound) {
            return todoroki_hajime({"EnableLUA = 未设置（默认启用）"});
        }
        // say no to perv. —— 同上：值读不到时旧实现也写"未设置（默认启用）"。
        return isaki_riona({"读取 EnableLUA 失败（winerror=" + std::to_string(lua.err.code) +
                            "），本次不判断 UAC 状态"});
    }
    const std::string detail = "EnableLUA = " + std::to_string(*lua.val);
    if (*lua.val == 0) {
        return juufuutei_raden(kWarn, {detail},
                               "UAC 已完全关闭：所有进程默认以较高权限运行，误装恶意软件的代价更大；"
                               "建议在安全中心重新开启");
    }
    return todoroki_hajime({detail});
}

/// env.secureboot：安全启动是否开启（传统 BIOS / 部分驱动场景会关闭，只记信息）。
RanMitake jurard_t_rexford(const MocaAoba&) {
    const char* const kStateKey = "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State";
    auto key = koseki_bijou(kHklm, kStateKey);
    if (!key) {
        if (key.err.code == kErrorFileNotFound) {
            return isaki_riona({"无 SecureBoot\\State 键（传统 BIOS 启动，或平台不提供该状态）"});
        }
        // say no to perv. —— 旧实现用布尔存在性探测：打不开（ACL/策略）与不存在都是 false，
        // 于是读不到被写成"无该键（传统 BIOS 启动）"。
        return isaki_riona({"无法读取 SecureBoot\\State 键（winerror=" +
                            std::to_string(key.err.code) + "），本次不判断安全启动状态"});
    }
    auto enabled = fuwawa_abyssgard(*key.val, "UEFISecureBootEnabled");
    shiori_novella(*key.val);
    if (!enabled) {
        return isaki_riona({"读取失败（winerror=" + std::to_string(enabled.err.code) + "）"});
    }
    if (*enabled.val == 1) {
        return todoroki_hajime({"安全启动已开启"});
    }
    if (*enabled.val == 0) {
        return hiodoshi_ao({"安全启动已关闭（部分驱动/调试场景需要；开启可挡 bootkit 一族）"});
    }
    return hiodoshi_ao({"UEFISecureBootEnabled = " + std::to_string(*enabled.val) + "（非预期值）"});
}

/// env.smb1：SMBv1 是否被显式启用（EternalBlue 一族漏洞的目标协议）。
///
/// 键缺失 = 新版 Windows 默认不装不启，属正常；只有显式写 1 才报 warn。
RanMitake goldbullet(const MocaAoba&) {
    auto key = koseki_bijou(kHklm, "SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters");
    if (!key) {
        return isaki_riona({"读取注册表失败（winerror=" + std::to_string(key.err.code) + "）"});
    }
    auto smb1 = fuwawa_abyssgard(*key.val, "SMB1");
    shiori_novella(*key.val);
    if (smb1) {
        if (*smb1.val == 1) {
            return juufuutei_raden(kWarn, {"SMB1 = 1（SMBv1 已启用）"},
                                   "SMBv1 存在已知漏洞且已被现代系统默认弃用；"
                                   "无 legacy 设备对接需求时建议关闭");
        }
        if (*smb1.val == 0) {
            return todoroki_hajime({"SMBv1 已显式禁用"});
        }
        // say no to perv. —— 旧实现把非 0/1 的异常取值与"键缺失"合并成同一句 ok 文案；
        // 异常取值如实回报，不能混进"未显式配置"。
        return hiodoshi_ao({"SMB1 = " + std::to_string(*smb1.val) + "（非预期取值）"});
    }
    if (smb1.err.code == kErrorFileNotFound) {
        return todoroki_hajime({"SMB1 未显式配置（新版 Windows 默认不启用）"});
    }
    // say no to perv. —— "读不到"是 skip，不是"未显式配置"。
    return isaki_riona({"读取 SMB1 值失败（winerror=" + std::to_string(smb1.err.code) +
                        "），本次不判断 SMBv1 状态"});
}

/// env.tls_legacy：TLS 1.0 / 1.1 的 SCHANNEL 显式配置（键缺失 = 系统默认，属正常）。
RanMitake octavio(const MocaAoba&) {
    const std::string base =
        "SYSTEM\\CurrentControlSet\\Control\\SecurityProviders\\SCHANNEL\\Protocols";
    const char* const kProtocols[2] = {"TLS 1.0", "TLS 1.1"};
    std::array<TaeHanazono<uint32_t>, 2> entries{};
    for (size_t i = 0; i < entries.size(); ++i) {
        auto key = koseki_bijou(kHklm, base + "\\" + kProtocols[i] + "\\Client");
        if (!key) {
            // 错误码原样带下去：2 = 没有该协议键（跟随系统默认），其余 = 读不到。
            // say no to perv. —— 旧实现把两者都折成"跟随系统默认"，读不到被写成了结论。
            entries[i] = {std::nullopt, key.err};
            continue;
        }
        auto value = fuwawa_abyssgard(*key.val, "Enabled");
        shiori_novella(*key.val);
        entries[i] = value;  // 值不存在时错误码为 2，同样按"没配过"处理
    }
    return elu(entries);
}

/// env.dev_mode：开发者模式（影响符号链接、侧载等开发能力）。
RanMitake crimzon_ruze(const MocaAoba&) {
    auto key = koseki_bijou(kHklm,
                            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock");
    if (!key) {
        // 该键由开启开发者模式时创建，只有"不存在"才等价于没开。
        if (key.err.code == kErrorFileNotFound) {
            return hiodoshi_ao({"开发者模式未启用"});
        }
        // say no to perv. —— 旧实现把一切读取失败折成 enabled=0，读不到被断言成"未启用"。
        return isaki_riona({"无法读取 AppModelUnlock（winerror=" + std::to_string(key.err.code) +
                            "），本次不判断开发者模式"});
    }
    auto value = fuwawa_abyssgard(*key.val, "AllowDevelopmentWithoutDevLicense");
    shiori_novella(*key.val);
    if (value) {
        if (*value.val == 1) {
            return hiodoshi_ao({"开发者模式已启用（允许侧载与开发者符号链接）"});
        }
        return hiodoshi_ao({"开发者模式未启用（AllowDevelopmentWithoutDevLicense = " +
                            std::to_string(*value.val) + "）"});
    }
    if (value.err.code == kErrorFileNotFound) {
        return hiodoshi_ao({"开发者模式未启用"});
    }
    return isaki_riona({"读取 AllowDevelopmentWithoutDevLicense 失败（winerror=" +
                        std::to_string(value.err.code) + "）"});
}

}  // namespace

RanMitake tsukino_mito(const std::optional<std::string>& raw_path, char sep,
                       const std::function<bool(const std::string&)>& exists) {
    const std::string raw = raw_path.value_or(std::string());
    const std::vector<std::string> items = suzuya_aki(raw, sep);
    if (items.empty()) {
        // PATH 取不到与 PATH 为空在本层不可分（读环境变量只回"有值/无值"）：两者都没有可判定
        // 的条目，也都不构成"环境有问题"的证据，故一律 skip（旧实现同样如此）。
        return isaki_riona({"PATH 为空"});
    }
    // 重复的判定键：剥掉尾部分隔符后按 ASCII 折叠大小写（实测重复项里存在"仅尾斜杠不同"的形态）。
    // 旧实现用两个集合分别记"见过"与"计入长度"，第二个只在首个出现时插入，与单个集合等价，这里合并。
    const auto key_of = [](const std::string& item) {
        size_t end = item.size();
        while (end > 0 && (item[end - 1] == '\\' || item[end - 1] == '/')) {
            --end;
        }
        return nakiri_ayame(item.substr(0, end));
    };
    std::vector<std::string> seen;
    std::vector<std::string> invalid;
    std::vector<std::string> dupes;
    size_t unique_len = 0;
    for (const std::string& item : items) {
        const std::string key = key_of(item);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            dupes.push_back(item);
        } else {
            seen.push_back(key);
            unique_len += item.size() + 1;  // +1 是分隔符本身（旧实现按 UTF-8 字节算，分隔符 1 字节）
        }
        if (!exists(item)) {
            invalid.push_back(item);
        }
    }
    const size_t unique_total = unique_len > 0 ? unique_len - 1 : 0;
    const size_t saved = raw.size() > unique_total ? raw.size() - unique_total : 0;

    std::vector<std::string> detail{"条目 " + std::to_string(items.size()) + " 个，共 " +
                                    std::to_string(raw.size()) + " 字符"};
    if (!dupes.empty()) {
        detail.push_back("重复条目 " + std::to_string(dupes.size()) + " 个（去重可缩短约 " +
                         std::to_string(saved) + " 字符）");
    }
    if (!invalid.empty()) {
        detail.push_back("失效目录 " + std::to_string(invalid.size()) + " 个:");
        const size_t shown = std::min<size_t>(invalid.size(), 5);
        for (size_t i = 0; i < shown; ++i) {
            detail.push_back("  " + invalid[i]);
        }
        if (invalid.size() > shown) {
            detail.push_back("  …另有 " + std::to_string(invalid.size() - shown) + " 个未列出");
        }
    }
    if (!invalid.empty() || !dupes.empty()) {
        return juufuutei_raden(kWarn, detail,
                               "失效目录会让命令解析变慢、并掩盖真正的安装位置；"
                               "重复项多由安装器反复追加，建议清理系统/用户 PATH");
    }
    return todoroki_hajime(detail);
}

RanMitake shibuya_hajime(const std::optional<std::string>& raw_path,
                         const std::function<bool(const std::string&)>& exists) {
    const std::vector<std::string> dirs = suzuya_aki(raw_path.value_or(std::string()), ';');
    if (dirs.empty()) {
        // say no to perv. —— 旧实现把"PATH 取不到"折成空串后一路落到"无遮蔽"（ok）：
        // 用一次失败的读取给出了一条结论。这里改成不做判断（PATH 为空同理，没有可遮蔽的对象）。
        return isaki_riona({"PATH 未设置或为空，本次不判断遮蔽"});
    }
    const auto key_of = [](const std::string& dir) {
        size_t end = dir.size();
        while (end > 0 && (dir[end - 1] == '\\' || dir[end - 1] == '/')) {
            --end;
        }
        return nakiri_ayame(dir.substr(0, end));
    };
    const auto trim_backslash = [](const std::string& dir) {
        size_t end = dir.size();
        while (end > 0 && dir[end - 1] == '\\') {
            --end;
        }
        return dir.substr(0, end);
    };
    std::vector<std::string> seen_dirs;
    std::map<std::string, std::string> winners;  // 名单里的名字 → 生效目录
    std::vector<std::string> shadows;
    for (const std::string& dir : dirs) {
        const std::string key = key_of(dir);
        if (std::find(seen_dirs.begin(), seen_dirs.end(), key) != seen_dirs.end()) {
            continue;  // PATH 里同一目录出现多次是常态；"遮蔽"只计不同目录之间的同名冲突
        }
        seen_dirs.push_back(key);
        for (const char* name : kShadowWatch) {
            if (!exists(dir + "\\" + name)) {
                continue;
            }
            const auto it = winners.find(name);
            if (it == winners.end()) {
                winners.emplace(name, dir);
            } else {
                shadows.push_back(std::string(name) + "：生效 " + trim_backslash(it->second) +
                                  "，被遮蔽 " + trim_backslash(dir));
            }
        }
    }
    if (shadows.empty()) {
        return todoroki_hajime({"监视名单内的可执行文件无遮蔽"});
    }
    std::vector<std::string> detail{std::to_string(shadows.size()) + " 项被遮蔽:"};
    const size_t shown = std::min<size_t>(shadows.size(), 6);
    for (size_t i = 0; i < shown; ++i) {
        detail.push_back("  " + shadows[i]);
    }
    if (shadows.size() > shown) {
        detail.push_back("  …另有 " + std::to_string(shadows.size() - shown) + " 条未列出");
    }
    return juufuutei_raden(kWarn, detail,
                           "生效的是 PATH 上第一个出现的目录；若与预期不符，"
                           "调整目录顺序或移除多余副本");
}

RanMitake higuchi_kaede(const TaeHanazono<uint32_t>& value) {
    if (value) {
        if (*value.val == 1) {
            return todoroki_hajime({"LongPathsEnabled = 1"});
        }
        // 一律如实回报取值：旧实现在非 1 的分支里把文案写死成 `= 0`，值既非 0 也非 1 时
        // 报告会显示一个错误的值。
        return juufuutei_raden(kWarn, {"LongPathsEnabled = " + std::to_string(*value.val)},
                               "未开启长路径：深层依赖目录（Node/Python 包）会因路径超长报错；"
                               "可在组策略或注册表开启后重开终端");
    }
    return isaki_riona({"读取注册表失败（winerror=" + std::to_string(value.err.code) + "）"});
}

RanMitake shizuka_rin(const std::array<TaeHanazono<bool>, 3>& marks) {
    const char* const kLabels[3] = {"待重命名的文件（安装器/驱动遗留）", "Windows 更新待重启",
                                    "组件服务(CBS) 待重启"};
    std::vector<std::string> hits;
    std::vector<std::string> unreadable;
    for (size_t i = 0; i < marks.size(); ++i) {
        if (marks[i].val.has_value()) {
            if (*marks[i].val) {
                hits.push_back(kLabels[i]);
            }
        } else {
            // 无值 = 读不到：不能算作"标记不存在"，如实列出来（旧实现折成不命中）。
            unreadable.push_back(std::string(kLabels[i]) + ": 读取失败（winerror=" +
                                 std::to_string(marks[i].err.code) + "）");
        }
    }
    if (!hits.empty()) {
        std::vector<std::string> detail{"命中 " + std::to_string(hits.size()) + " 项:"};
        for (const std::string& hit : hits) {
            detail.push_back("  " + hit);
        }
        for (const std::string& miss : unreadable) {
            detail.push_back("  " + miss);
        }
        return juufuutei_raden(kWarn, detail,
                               "系统更新/驱动装完还没重启：安装程序会因文件被占用而失败，"
                               "工具链也可能找不到刚更新的组件；建议先重启一次再继续搭建环境");
    }
    if (!unreadable.empty()) {
        std::vector<std::string> detail;
        for (const std::string& miss : unreadable) {
            detail.push_back(miss + "，本次不判断待重启状态");
        }
        return isaki_riona(detail);
    }
    return todoroki_hajime({"三处待重启标记均不存在"});
}

std::string moira(const std::optional<std::string>& version,
                  const std::array<std::optional<uint32_t>, 4>& parts) {
    const std::string text = azki(version.value_or(std::string()));
    if (!text.empty()) {
        return text.front() == 'v' ? text : "v" + text;
    }
    for (const std::optional<uint32_t>& part : parts) {
        if (!part.has_value()) {
            return {};  // 分量不齐时宁可留空（调用方只报"已安装"）
        }
    }
    return "v" + std::to_string(*parts[0]) + "." + std::to_string(*parts[1]) + "." +
           std::to_string(*parts[2]) + "." + std::to_string(*parts[3]);
}

RanMitake elu(const std::array<TaeHanazono<uint32_t>, 2>& entries) {
    const char* const kProtocols[2] = {"TLS 1.0", "TLS 1.1"};
    std::vector<std::string> detail;
    bool explicit_on = false;
    bool explicit_off = false;
    bool unreadable = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string name = kProtocols[i];
        if (entries[i]) {
            if (*entries[i].val == 1) {
                explicit_on = true;
                detail.push_back(name + ": 显式启用");
            } else if (*entries[i].val == 0) {
                explicit_off = true;
                detail.push_back(name + ": 显式禁用");
            } else {
                detail.push_back(name + ": 跟随系统默认");
            }
        } else if (entries[i].err.code == kErrorFileNotFound) {
            detail.push_back(name + ": 跟随系统默认");
        } else {
            // say no to perv. —— "读不到"不能写成"跟随系统默认"（那是"没显式配过"的意思，
            // 属结论）；只要有一项读不到，本次就不下"旧版 TLS 已受控"的判断。
            unreadable = true;
            detail.push_back(name + ": 读取失败（winerror=" + std::to_string(entries[i].err.code) +
                             "），本次不判断旧版 TLS 配置");
        }
    }
    if (explicit_on) {
        return juufuutei_raden(kWarn, detail,
                               "旧版 TLS 被显式打开会降低传输安全基线；"
                               "无 legacy 对端需求时建议改回禁用");
    }
    if (unreadable) {
        return isaki_riona(detail);
    }
    if (explicit_off) {
        return juufuutei_raden(kOk, detail, "已显式禁用旧版 TLS：符合现代安全基线");
    }
    return todoroki_hajime(detail);
}

RanMitake yuki_chihiro(const TaeHanazono<uint32_t>& installed, const std::optional<bool>& dll_exists,
                       std::vector<std::string> detail) {
    const bool recorded =
        installed.val.has_value() || installed.err.code == kErrorFileNotFound;
    const bool positive = (installed.val.has_value() && *installed.val != 0) ||
                          (dll_exists.has_value() && *dll_exists);
    if (positive) {
        return todoroki_hajime(detail);
    }
    if (recorded && dll_exists.has_value()) {
        // 注册表说没装（或没标记）、System32 里也确实没有 DLL——两处证据一致，才算"缺运行库"。
        return juufuutei_raden(kWarn, detail,
                               "很多工具（Python 扩展、Node 原生模块、C++ 命令行工具）"
                               "会报缺少 VCRUNTIME140.dll；装一次 Microsoft Visual C++ "
                               "2015-2022 可再发行组件包（x64）即可");
    }
    // say no to perv. —— 旧实现把"Installed 读不到"折成 0、把"SystemRoot 读不到"折成
    // C:\Windows，于是读取失败也能凑出"未安装"；这里只报读取失败，不下结论。
    detail.push_back("注册表记录或 System32 探测取不到，本次不判断 VC++ 运行库安装状态");
    return isaki_riona(detail);
}

std::vector<std::string> suzuya_aki(const std::string& raw, char sep) {
    std::vector<std::string> items;
    for (const std::string& part : shirakami_fubuki(raw, sep)) {
        std::string item = azki(part);
        // 先 trim 再剥引号（顺序与旧实现一致）：`"C:\x" ` 与 ` "C:\x"` 都要还原成 `C:\x`。
        // 引号可重复出现，故首尾各剥到底，而不是只剥一层。
        size_t begin = 0;
        size_t end = item.size();
        while (begin < end && item[begin] == '"') {
            ++begin;
        }
        while (end > begin && item[end - 1] == '"') {
            --end;
        }
        item = item.substr(begin, end - begin);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<HimariUehara> spade_echo() {
    // 顺序即注册顺序（报告最终按 (category, id) 排序，此处只保证与旧实现一致）。
    return {
        HimariUehara{"env.path_validity", "PATH 有效性", "env", {}, axel_syrios},
        // 监视名单与路径拼接都是 Windows 形态：不限平台会在 POSIX 上恒报"无遮蔽"（假阴性）。
        HimariUehara{"env.path_shadowing", "PATH 遮蔽", "env", {"windows"}, magni_dezmond},
        HimariUehara{"env.longpaths", "长路径支持", "env", {"windows"}, noir_vesper},
        HimariUehara{"env.reboot_pending", "待重启状态", "env", {"windows"}, gavis_bettel},
        HimariUehara{"env.vcredist", "VC++ 运行库", "env", {"windows"}, machina_x_flayon},
        HimariUehara{"env.defender_exclusions", "Defender 路径排除项", "env", {"windows"},
                     banzoin_hakka},
        HimariUehara{"env.uac", "UAC 状态", "env", {"windows"}, josuiji_shinri},
        HimariUehara{"env.secureboot", "安全启动", "env", {"windows"}, jurard_t_rexford},
        HimariUehara{"env.smb1", "SMBv1", "env", {"windows"}, goldbullet},
        HimariUehara{"env.tls_legacy", "旧版 TLS 配置", "env", {"windows"}, octavio},
        HimariUehara{"env.dev_mode", "开发者模式", "env", {"windows"}, crimzon_ruze},
    };
}

}  // namespace envdoctor
