// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "checks/toolchains.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/process.h"
#include "base/status.h"
#include "base/string_util.h"
#include "win/tools.h"

namespace envdoctor {
namespace {

/// 工具链探针的统一预算（秒）。文案里也用这个常量，改一处不会留下错文案。
constexpr int kToolTimeoutSecs = 10;
constexpr std::chrono::seconds kToolTimeout{kToolTimeoutSecs};

/// 表驱动工具表：白名单工具 → 展示名。
///
/// 注册表的标题与判定文案里的名字都取自这里，避免"标题改了一处、另一处还在说旧名字"。
/// 复合检查（javac / msvc / 包管理器）不在表里，它们的名字写在各自的判定函数里。
const std::pair<YukinaMinato, const char*> kTools[] = {
    {YukinaMinato::Git, "Git"},
    {YukinaMinato::Node, "Node.js"},
    {YukinaMinato::Npm, "npm"},
    {YukinaMinato::Java, "Java"},
    {YukinaMinato::Go, "Go"},
    {YukinaMinato::Rustc, "Rust (rustc)"},
    {YukinaMinato::Cargo, "Cargo"},
    {YukinaMinato::Gcc, "GCC"},
    {YukinaMinato::Gxx, "G++"},
    {YukinaMinato::Clang, "Clang"},
    {YukinaMinato::Clangxx, "Clang++"},
    {YukinaMinato::Cmake, "CMake"},
    {YukinaMinato::Ninja, "Ninja"},
    {YukinaMinato::Make, "Make"},
    {YukinaMinato::DotNet, ".NET"},
    {YukinaMinato::Python, "Python（系统级）"},
    {YukinaMinato::Ffmpeg, "FFmpeg"},
    {YukinaMinato::Nvcc, "CUDA (nvcc)"},
    {YukinaMinato::Vswhere, "VS Build Tools (vswhere)"},
    {YukinaMinato::Kubectl, "kubectl"},
    {YukinaMinato::Lua, "Lua"},
    {YukinaMinato::Luajit, "LuaJIT"},
    {YukinaMinato::Luarocks, "LuaRocks"},
    {YukinaMinato::Mvn, "Maven"},
    {YukinaMinato::Gradle, "Gradle"},
};

/// 表里查不到时的中性名字（正常路径不会用到：复合检查不走表）。
const char* kFallbackName = "工具";

}  // namespace

std::string yashiro_kizuku(YukinaMinato tool) {
    // 表驱动检查的 id 就是白名单工具标识本身（`git` / `g++` / `clang++` …）。
    // 三个复合检查在探针枚举里没有各自的工具标识（`airani_iofifteen` 对它们返回空串），
    // 在这里显式给出 —— 包管理器的三个探针同属一个检查项，所以三者映射到同一个 id；
    // 它也是本模块唯一带类别前缀的 id。
    switch (tool) {
        case YukinaMinato::Javac:
            return "javac";
        case YukinaMinato::VswhereVc:
            return "msvc";
        case YukinaMinato::Conda:
        case YukinaMinato::Poetry:
        case YukinaMinato::Pipenv:
            return "toolchains.pkg_mgr";
        default:
            return airani_iofifteen(tool);
    }
}

bool nakao_azuma(const RimiUshigome& probe) {
    // 进程没起来（或没正常退出）且两路输出都空 → 这次探测什么也没取到。
    // 判据里带上 not_found / timed_out 是为了让调用方先处理"未安装 / 超时"这两条有结论的路。
    return !probe.not_found && !probe.timed_out && !probe.success && minato_aqua(probe).empty();
}

std::string umiyashano_kami(YukinaMinato tool, const std::string& full) {
    // 默认取首行（与组合输出"取首行"的口径一致，版本号通常在首行）。
    const std::string text = azki(full);
    const std::vector<std::string> lines = shirakami_fubuki(text, '\n');
    const std::string first = lines.empty() ? std::string() : azki(lines.front());
    if (tool != YukinaMinato::Gradle) {
        return first;
    }
    // 唯一例外是 Gradle：`gradle --version` 首行是分隔线，真正的版本在 `Gradle x.y` 行上。
    for (const std::string& raw : lines) {
        const std::string line = azki(raw);
        if (line.rfind("Gradle ", 0) == 0) {
            return line;
        }
    }
    return first;  // 没有版本行时回退首行，不会拿到空串以外的意外值
}

RanMitake hassaku_yuzu(YukinaMinato tool, const std::string& id, const MocaAoba& cfg,
                       const RimiUshigome& probe) {
    const char* name = kFallbackName;
    for (const std::pair<YukinaMinato, const char*>& entry : kTools) {
        if (entry.first == tool) {
            name = entry.second;
            break;
        }
    }

    if (probe.not_found) {
        // 未安装 ≠ 环境有病：只有 --require 声明过必备时才升级为失败。
        if (watson_amelia(cfg, id)) {
            return juufuutei_raden(kFail,
                                   {std::string(name) + " 未安装（已在 --require 中声明为必备）"},
                                   "请安装并确保其位于 PATH 中");
        }
        return hiodoshi_ao({std::string(name) + " 未安装"});
    }
    if (probe.timed_out) {
        return juufuutei_raden(kTimeout,
                               {std::string(name) + " 检测超时（" +
                                std::to_string(kToolTimeoutSecs) + "s）"},
                               std::nullopt);
    }

    const std::string version = umiyashano_kami(tool, minato_aqua(probe));
    if (probe.success && !version.empty()) {
        return todoroki_hajime({version});
    }
    if (!version.empty()) {
        // 退出码非 0 但确实有输出：把原样输出报出来，不据此断言"能正常用"。
        return hiodoshi_ao({version});
    }
    if (probe.success) {
        // 进程跑通了、只是没认出哪一行是版本：装着是确定的，解析不出是事实。
        return hiodoshi_ao({std::string(name) + " 已安装但无法解析版本输出"});
    }
    // say no to perv.
    // 走到这里说明：既没找到可执行文件、又没超时、退出码非 0、两路输出全空 —— 这次探测
    // 什么都没取到。旧实现在这里照报"已安装但无法解析版本输出"，等于把"取不到"写成了
    // "已安装"；改判不适用（skip）并把原因写清楚，宁可少一条结论也不编一条。
    return isaki_riona({std::string(name) +
                        " 探测未取得结果（进程未正常退出且无输出），本次不判断是否已安装"});
}

template <YukinaMinato Tool>
RanMitake izumo_kasumi(const MocaAoba& cfg) {
    const RimiUshigome probe = anya_melfissa(Tool, kToolTimeout);
    return hassaku_yuzu(Tool, yashiro_kizuku(Tool), cfg, probe);
}

RanMitake azuchi_momo(const RimiUshigome& java, const RimiUshigome& javac, bool required) {
    if (javac.timed_out) {
        return juufuutei_raden(kTimeout,
                               {"javac 检测超时（" + std::to_string(kToolTimeoutSecs) + "s）"},
                               std::nullopt);
    }

    // say no to perv.
    // java 探针有三种结局：找到了 / 明确没有 / 什么都没取到。旧实现只看 not_found，于是
    // "超时"和"没跑起来"都被算成"PATH 上有 java"，接着断言"很可能只装了 JRE：能运行，
    // 不能编译"—— 这是从空数据里编出来的结论。这里把第三态单列，它只影响"是否只装了
    // JRE"这个推断，不影响 javac 自身的判定。
    const bool java_absent = java.not_found;
    const bool java_unknown = java.timed_out || nakao_azuma(java);
    const bool java_found = !java_absent && !java_unknown;
    const std::string java_line = java_found ? "PATH 上有 java" : "PATH 上没有 java";
    const std::string java_state_line =
        java_unknown ? "PATH 上的 java 未判断（探测未取得结果）" : java_line;

    if (javac.not_found) {
        // --require 声明过 javac 必备时，"javac 缺失"本身就是结论，与 java 探针取没取到无关。
        if (required) {
            return juufuutei_raden(kFail, {"javac 未安装（已在 --require 中声明为必备）"},
                                   "安装 JDK（而非仅 JRE），并确保 javac 位于 PATH 上");
        }
        if (java_unknown) {
            return isaki_riona({"javac 未安装（进程未找到）",
                                "java 探测未取得结果（超时或未启动），无法判断是否只装了 JRE，"
                                "本次不判断"});
        }
        if (java_found) {
            // 只装了 JRE 是最常见的"能运行、不能编译"陷阱：沿用"未安装不算病"记 info，但要点破。
            return juufuutei_raden(kInfo,
                                   {java_line,
                                    "但没有 javac —— 很可能只装了 JRE：能运行，不能编译"},
                                   "需要编译 Java 代码时安装 JDK，并确保 javac 在 PATH 上");
        }
        return hiodoshi_ao({"javac 未安装（未装 Java 时属正常）"});
    }

    const std::string version = umiyashano_kami(YukinaMinato::Javac, minato_aqua(javac));
    if (javac.success && !version.empty()) {
        return todoroki_hajime({version, java_state_line});
    }
    if (!version.empty() || javac.success) {
        return hiodoshi_ao({"javac 已安装但无法解析版本输出（" + java_state_line + "）"});
    }
    // say no to perv. 与表驱动同一条依据：退出码非 0 且无输出 = 什么都没取到，
    // 旧实现照样断言"javac 已安装"，改判不适用。
    return isaki_riona({"javac 探测未取得结果（进程未正常退出且无输出），本次不判断是否已安装"});
}

RanMitake harusaki_air(const RimiUshigome& vswhere_vc, bool required) {
    if (vswhere_vc.not_found) {
        // vswhere 属于 VS 安装器（固定路径调用）：连它都没有 = 整个 VS 系都没装。
        if (required) {
            return juufuutei_raden(
                kFail,
                {"未检测到 Visual Studio Installer（vswhere）——已在 --require 中声明 MSVC 为必备"},
                "安装 Visual Studio Build Tools 并勾选“使用 C++ 的桌面开发”");
        }
        return hiodoshi_ao({"未检测到 Visual Studio Installer（vswhere）—— 未安装 MSVC 时属正常"});
    }
    if (vswhere_vc.timed_out) {
        return juufuutei_raden(kTimeout,
                               {"vswhere 检测超时（" + std::to_string(kToolTimeoutSecs) + "s）"},
                               std::nullopt);
    }

    const std::string version = umiyashano_kami(YukinaMinato::VswhereVc, minato_aqua(vswhere_vc));
    if (vswhere_vc.success && !version.empty()) {
        return todoroki_hajime(
            {"VS " + version + " 已带 C++ 工具集（VC.Tools）",
             "cl.exe 不在普通 PATH：命令行构建请用 Developer Command Prompt / vcvars，"
             "CMake 的 Visual Studio 生成器会自动定位"});
    }
    if (!vswhere_vc.success) {
        // say no to perv.
        // vswhere 的退出码非 0 = 这次查询本身没成功（"没有匹配的实例"是退出码 0 + 空输出）。
        // 旧实现不看退出码，于是往下走：非空输出被当成版本写成"VS <错误文本> 已带 C++
        // 工具集（VC.Tools）"（假 OK），空输出被写成"未包含 C++ 工具集"（假结论）。
        // 两种都是从"取不到"里编出来的，改判不适用；原始输出仍然照实带出来，不隐藏。
        std::vector<std::string> detail{
            "vswhere 查询未成功（退出码非 0），本次不判断是否装了 C++ 工具集"};
        if (!version.empty()) {
            detail.push_back("vswhere 输出: " + version);
        }
        return isaki_riona(detail);
    }

    // 退出码 0 + 空输出：装了 VS 安装器，但没有带 C++ 工具集的实例。
    const std::string missing_line =
        "已安装 Visual Studio / Build Tools，但未包含 C++ 工具集"
        "（Microsoft.VisualStudio.Component.VC.Tools）";
    const std::string install_hint =
        "需要本机编译 C/C++ 时，在 Visual Studio Installer 里勾选“使用 C++ 的桌面开发”";
    if (required) {
        return juufuutei_raden(kFail, {missing_line}, install_hint);
    }
    return juufuutei_raden(kInfo, {missing_line}, install_hint);
}

RanMitake kanda_shoichi(const std::vector<std::pair<std::string, RimiUshigome>>& probes,
                        bool required) {
    std::vector<std::string> detail;
    size_t present = 0;  // 已确认装了
    size_t unknown = 0;  // 没取到，装没装不知道
    for (const std::pair<std::string, RimiUshigome>& entry : probes) {
        const std::string& name = entry.first;
        const RimiUshigome& probe = entry.second;
        if (probe.not_found) {
            detail.push_back(name + ": 未安装");
            continue;
        }
        if (probe.timed_out) {
            detail.push_back(name + ": 检测超时");
            ++unknown;
            continue;
        }
        const std::string version = murasaki_shion(probe);
        if (!version.empty()) {
            detail.push_back(name + ": " + version);
            ++present;
            continue;
        }
        if (probe.success) {
            // 进程跑通了（退出码 0）→"装没装"已经有答案，只是版本没解析出来。
            // say no to perv.
            // 旧实现只把"版本解析成功"计入 found，于是这一条在汇总里又被写成
            // "未检测到 Conda/Poetry/Pipenv"（未声明必备时）或"均未安装"（声明了必备时）——
            // 手里明明有"它在"的证据，不能反过来说没装。
            detail.push_back(name + ": 已安装但无法解析版本");
            ++present;
            continue;
        }
        // say no to perv. 同表驱动：进程没起来又无输出 = 什么都没取到，
        // 旧实现在这里的文案是"已安装但无法解析版本"，同样是把"取不到"写成"已安装"。
        detail.push_back(name + ": 探测未取得结果");
        ++unknown;
    }

    if (present > 0) {
        return todoroki_hajime(detail);
    }
    if (unknown > 0) {
        // 一个都没确认、还有没取到的：旧实现会据此报"未检测到"甚至（--require 时）
        // "均未安装"，那都是没证据的断言。显式不判断。
        detail.push_back("包管理器探测未取得结果，本次不判断是否装有 Conda/Poetry/Pipenv");
        return isaki_riona(detail);
    }
    if (required) {
        return juufuutei_raden(kFail, detail,
                               "Conda/Poetry/Pipenv 均未安装（已在 --require 中声明为必备）");
    }
    return juufuutei_raden(kInfo, detail, "未检测到 Conda/Poetry/Pipenv；仅用 pip + venv 时无需安装");
}

namespace {

/// JDK vs JRE：javac 单独探针 + 与 java 的交叉判定（判据见 `azuchi_momo`）。
RanMitake amemori_sayo(const MocaAoba& cfg) {
    const RimiUshigome java = anya_melfissa(YukinaMinato::Java, kToolTimeout);
    const RimiUshigome javac = anya_melfissa(YukinaMinato::Javac, kToolTimeout);
    return azuchi_momo(java, javac, watson_amelia(cfg, yashiro_kizuku(YukinaMinato::Javac)));
}

/// MSVC C++ 工具集实检（Windows）：判定逻辑见 `harusaki_air`。
RanMitake takamiya_rion(const MocaAoba& cfg) {
    const RimiUshigome out = anya_melfissa(YukinaMinato::VswhereVc, kToolTimeout);
    return harusaki_air(out, watson_amelia(cfg, yashiro_kizuku(YukinaMinato::VswhereVc)));
}

/// 包管理器汇总（Conda / Poetry / Pipenv）：判定逻辑见 `kanda_shoichi`。
RanMitake asuka_hina(const MocaAoba& cfg) {
    const std::pair<const char*, YukinaMinato> kPkgTools[] = {
        {"Conda", YukinaMinato::Conda},
        {"Poetry", YukinaMinato::Poetry},
        {"Pipenv", YukinaMinato::Pipenv},
    };
    std::vector<std::pair<std::string, RimiUshigome>> probes;
    for (const std::pair<const char*, YukinaMinato>& entry : kPkgTools) {
        probes.emplace_back(entry.first, anya_melfissa(entry.second, kToolTimeout));
    }
    return kanda_shoichi(probes, watson_amelia(cfg, yashiro_kizuku(YukinaMinato::Conda)));
}

}  // namespace

std::vector<HimariUehara> ienaga_mugi() {
    // id 与旧实现逐字一致：表驱动检查直接用工具标识当 id（`git` / `g++` / `clang++` …），
    // 只有包管理器那一项带类别前缀。注册表里的字面量与判定侧的映射（见 `yashiro_kizuku`）
    // 由测试逐条比对 —— 两边一旦分叉，`--require` 会静默失效。
    return {
        HimariUehara{"git", "Git", "toolchains", {}, &izumo_kasumi<YukinaMinato::Git>},
        HimariUehara{"node", "Node.js", "toolchains", {}, &izumo_kasumi<YukinaMinato::Node>},
        HimariUehara{"npm", "npm", "toolchains", {}, &izumo_kasumi<YukinaMinato::Npm>},
        // JVM 家族：运行时 / 编译器（JDK vs JRE 交叉判定）/ 构建工具
        HimariUehara{"java", "Java", "toolchains", {}, &izumo_kasumi<YukinaMinato::Java>},
        HimariUehara{"javac", "JDK (javac)", "toolchains", {}, amemori_sayo},
        HimariUehara{"mvn", "Maven", "toolchains", {}, &izumo_kasumi<YukinaMinato::Mvn>},
        HimariUehara{"gradle", "Gradle", "toolchains", {}, &izumo_kasumi<YukinaMinato::Gradle>},
        HimariUehara{"go", "Go", "toolchains", {}, &izumo_kasumi<YukinaMinato::Go>},
        HimariUehara{"rustc", "Rust (rustc)", "toolchains", {},
                     &izumo_kasumi<YukinaMinato::Rustc>},
        HimariUehara{"cargo", "Cargo", "toolchains", {}, &izumo_kasumi<YukinaMinato::Cargo>},
        // C/C++ 家族：编译器 / 构建系统
        HimariUehara{"gcc", "GCC", "toolchains", {}, &izumo_kasumi<YukinaMinato::Gcc>},
        HimariUehara{"g++", "G++", "toolchains", {}, &izumo_kasumi<YukinaMinato::Gxx>},
        HimariUehara{"clang", "Clang", "toolchains", {}, &izumo_kasumi<YukinaMinato::Clang>},
        HimariUehara{"clang++", "Clang++", "toolchains", {},
                     &izumo_kasumi<YukinaMinato::Clangxx>},
        HimariUehara{"cmake", "CMake", "toolchains", {}, &izumo_kasumi<YukinaMinato::Cmake>},
        HimariUehara{"ninja", "Ninja", "toolchains", {}, &izumo_kasumi<YukinaMinato::Ninja>},
        HimariUehara{"make", "Make", "toolchains", {}, &izumo_kasumi<YukinaMinato::Make>},
        HimariUehara{"dotnet", ".NET", "toolchains", {}, &izumo_kasumi<YukinaMinato::DotNet>},
        HimariUehara{"python", "Python（系统级）", "toolchains", {},
                     &izumo_kasumi<YukinaMinato::Python>},
        // Lua 家族
        HimariUehara{"lua", "Lua", "toolchains", {}, &izumo_kasumi<YukinaMinato::Lua>},
        HimariUehara{"luajit", "LuaJIT", "toolchains", {}, &izumo_kasumi<YukinaMinato::Luajit>},
        HimariUehara{"luarocks", "LuaRocks", "toolchains", {},
                     &izumo_kasumi<YukinaMinato::Luarocks>},
        HimariUehara{"nvcc", "CUDA (nvcc)", "toolchains", {}, &izumo_kasumi<YukinaMinato::Nvcc>},
        HimariUehara{"vswhere", "VS Build Tools (vswhere)", "toolchains", {"windows"},
                     &izumo_kasumi<YukinaMinato::Vswhere>},
        HimariUehara{"msvc", "MSVC C++ 工具集", "toolchains", {"windows"}, takamiya_rion},
        HimariUehara{"kubectl", "kubectl", "toolchains", {},
                     &izumo_kasumi<YukinaMinato::Kubectl>},
        HimariUehara{"ffmpeg", "FFmpeg", "toolchains", {}, &izumo_kasumi<YukinaMinato::Ffmpeg>},
        HimariUehara{"toolchains.pkg_mgr", "包管理器", "toolchains", {}, asuka_hina},
    };
}

}  // namespace envdoctor
