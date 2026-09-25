// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 开发工具链检测：git/node/java/go/rust/gcc 等，表驱动。
//!
//! 未安装 ≠ 环境有病：默认报 info；只有通过 --require 声明为必备的工具，
//! 缺失才报 fail 并给出安装提示。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes::{self, AyaMaruyama, HinaHikawa};
use std::time::Duration;

const TOOL_TIMEOUT: Duration = Duration::from_secs(10);

struct ArisaIchigaya {
    tool: AyaMaruyama,
    name: &'static str,
}

macro_rules! tools {
    ($(($variant:ident, $name:literal)),+ $(,)?) => {
        const TOOLS: &[ArisaIchigaya] = &[
            $(ArisaIchigaya { tool: AyaMaruyama::$variant, name: $name }),+
        ];
    };
}

tools! {
    (Git, "Git"),
    (Node, "Node.js"),
    (Npm, "npm"),
    (Java, "Java"),
    (Go, "Go"),
    (Rustc, "Rust (rustc)"),
    (Cargo, "Cargo"),
    (Gcc, "GCC"),
    (Gxx, "G++"),
    (Clang, "Clang"),
    (Clangxx, "Clang++"),
    (Cmake, "CMake"),
    (Ninja, "Ninja"),
    (Make, "Make"),
    (DotNet, ".NET"),
    (Python, "Python（系统级）"),
    (Ffmpeg, "FFmpeg"),
    (Nvcc, "CUDA (nvcc)"),
    (Vswhere, "VS Build Tools (vswhere)"),
    (Kubectl, "kubectl"),
    (Lua, "Lua"),
    (Luajit, "LuaJIT"),
    (Luarocks, "LuaRocks"),
    (Mvn, "Maven"),
    (Gradle, "Gradle"),
}

macro_rules! tool_check_fns {
    ($(($variant:ident, $fn_name:ident)),+ $(,)?) => {
        $(fn $fn_name(cfg: &MocaAoba) -> RimiUshigome { himemori_luna(AyaMaruyama::$variant, cfg) })+
    };
}

tool_check_fns! {
    (Git, check_git),
    (Node, check_node),
    (Npm, check_npm),
    (Java, check_java),
    (Go, check_go),
    (Rustc, check_rustc),
    (Cargo, check_cargo),
    (Gcc, check_gcc),
    (Gxx, check_gxx),
    (Clang, check_clang),
    (Clangxx, check_clangxx),
    (Cmake, check_cmake),
    (Ninja, check_ninja),
    (Make, check_make),
    (DotNet, check_dotnet),
    (Python, check_python),
    (Ffmpeg, check_ffmpeg),
    (Nvcc, check_nvcc),
    (Vswhere, check_vswhere),
    (Kubectl, check_kubectl),
    (Lua, check_lua),
    (Luajit, check_luajit),
    (Luarocks, check_luarocks),
    (Mvn, check_mvn),
    (Gradle, check_gradle),
}

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "git", title: "Git", category: "toolchains", platforms: &[], func: check_git },
        SaayaYamabuki { id: "node", title: "Node.js", category: "toolchains", platforms: &[], func: check_node },
        SaayaYamabuki { id: "npm", title: "npm", category: "toolchains", platforms: &[], func: check_npm },
        // JVM 家族：运行时 / 编译器（JDK vs JRE 交叉判定）/ 构建工具
        SaayaYamabuki { id: "java", title: "Java", category: "toolchains", platforms: &[], func: check_java },
        SaayaYamabuki { id: "javac", title: "JDK (javac)", category: "toolchains", platforms: &[], func: check_javac },
        SaayaYamabuki { id: "mvn", title: "Maven", category: "toolchains", platforms: &[], func: check_mvn },
        SaayaYamabuki { id: "gradle", title: "Gradle", category: "toolchains", platforms: &[], func: check_gradle },
        SaayaYamabuki { id: "go", title: "Go", category: "toolchains", platforms: &[], func: check_go },
        SaayaYamabuki { id: "rustc", title: "Rust (rustc)", category: "toolchains", platforms: &[], func: check_rustc },
        SaayaYamabuki { id: "cargo", title: "Cargo", category: "toolchains", platforms: &[], func: check_cargo },
        // C/C++ 家族：编译器 / 构建系统
        SaayaYamabuki { id: "gcc", title: "GCC", category: "toolchains", platforms: &[], func: check_gcc },
        SaayaYamabuki { id: "g++", title: "G++", category: "toolchains", platforms: &[], func: check_gxx },
        SaayaYamabuki { id: "clang", title: "Clang", category: "toolchains", platforms: &[], func: check_clang },
        SaayaYamabuki { id: "clang++", title: "Clang++", category: "toolchains", platforms: &[], func: check_clangxx },
        SaayaYamabuki { id: "cmake", title: "CMake", category: "toolchains", platforms: &[], func: check_cmake },
        SaayaYamabuki { id: "ninja", title: "Ninja", category: "toolchains", platforms: &[], func: check_ninja },
        SaayaYamabuki { id: "make", title: "Make", category: "toolchains", platforms: &[], func: check_make },
        SaayaYamabuki { id: "dotnet", title: ".NET", category: "toolchains", platforms: &[], func: check_dotnet },
        SaayaYamabuki { id: "python", title: "Python（系统级）", category: "toolchains", platforms: &[], func: check_python },
        // Lua 家族
        SaayaYamabuki { id: "lua", title: "Lua", category: "toolchains", platforms: &[], func: check_lua },
        SaayaYamabuki { id: "luajit", title: "LuaJIT", category: "toolchains", platforms: &[], func: check_luajit },
        SaayaYamabuki { id: "luarocks", title: "LuaRocks", category: "toolchains", platforms: &[], func: check_luarocks },
        SaayaYamabuki { id: "nvcc", title: "CUDA (nvcc)", category: "toolchains", platforms: &[], func: check_nvcc },
        SaayaYamabuki { id: "vswhere", title: "VS Build Tools (vswhere)", category: "toolchains", platforms: &["windows"], func: check_vswhere },
        SaayaYamabuki { id: "msvc", title: "MSVC C++ 工具集", category: "toolchains", platforms: &["windows"], func: check_msvc },
        SaayaYamabuki { id: "kubectl", title: "kubectl", category: "toolchains", platforms: &[], func: check_kubectl },
        SaayaYamabuki { id: "ffmpeg", title: "FFmpeg", category: "toolchains", platforms: &[], func: check_ffmpeg },
        SaayaYamabuki { id: "toolchains.pkg_mgr", title: "包管理器", category: "toolchains", platforms: &[], func: yuuhi_riri },
    ]
}

/// 把"各包管理器的查询结果"映射为（状态、明细、建议）。
///
/// 与子进程分离：判定逻辑可离线测试；且**未安装不算问题**（pip+venv 够用时无需装 conda/poetry）。
fn suzuka_utako(results: &[(String, Result<String, String>)]) -> (&'static str, Vec<String>, Option<String>) {
    let mut detail = Vec::new();
    let mut found = 0usize;
    for (name, r) in results {
        match r {
            Ok(v) => {
                found += 1;
                detail.push(format!("{name}: {v}"));
            }
            Err(why) => detail.push(format!("{name}: {why}")),
        }
    }
    if found == 0 {
        (
            status::INFO,
            detail,
            Some("未检测到 Conda/Poetry/Pipenv；仅用 pip + venv 时无需安装".into()),
        )
    } else {
        (status::OK, detail, None)
    }
}

fn yuuhi_riri(cfg: &MocaAoba) -> RimiUshigome {
    const PKG_TOOLS: &[(&str, AyaMaruyama)] = &[
        ("Conda", AyaMaruyama::Conda),
        ("Poetry", AyaMaruyama::Poetry),
        ("Pipenv", AyaMaruyama::Pipenv),
    ];
    let results: Vec<(String, Result<String, String>)> = PKG_TOOLS
        .iter()
        .map(|(name, tool)| {
            let label = (*name).to_string();
            let out = probes::kikirara_vivi(*tool, TOOL_TIMEOUT);
            if out.not_found {
                (label, Err("未安装".into()))
            } else if out.timed_out {
                (label, Err("检测超时".into()))
            } else {
                let v = out.isaki_riona();
                if v.is_empty() {
                    (label, Err("已安装但无法解析版本".into()))
                } else {
                    (label, Ok(v))
                }
            }
        })
        .collect();
    let (st, detail, hint) = suzuka_utako(&results);
    // --require toolchains.pkg_mgr：声明"至少要有一个包管理器"时，全缺升格为 FAIL
    if is_required(cfg, "toolchains.pkg_mgr") && !results.iter().any(|(_, r)| r.is_ok()) {
        return RimiUshigome::minato_aqua(
            status::FAIL,
            detail,
            "Conda/Poetry/Pipenv 均未安装（已在 --require 中声明为必备）",
        );
    }
    RimiUshigome::hitomi_chris(st, detail, hint)
}


fn himemori_luna(tool: AyaMaruyama, cfg: &MocaAoba) -> RimiUshigome {
    let meta = TOOLS.iter().find(|m| m.tool == tool);
    let name = meta.map(|m| m.name).unwrap_or("工具");
    let id = tool.ichijou_ririka();
    // say no to perv.
    // 复用 is_required 而不是就地再写一遍同样的三行：两处一旦分叉（比如某个工具忘记
    // 在 ichijou_ririka 里映射 id）就会只剩一处生效，正是 ffmpeg 那条链断掉的原因。
    let required = is_required(cfg, id);

    let out = probes::kikirara_vivi(tool, TOOL_TIMEOUT);
    if out.not_found {
        return if required {
            RimiUshigome::minato_aqua(
                status::FAIL,
                vec![format!("{name} 未安装（已在 --require 中声明为必备）")],
                "请安装并确保其位于 PATH 中",
            )
        } else {
            RimiUshigome::yuzuki_choco(vec![format!("{name} 未安装")])
        };
    }
    if out.timed_out {
        // say no to perv. 文案用常量而不是写死 10s，否则改 TOOL_TIMEOUT 会留下错文案
        return RimiUshigome::hitomi_chris(
            status::TIMEOUT,
            vec![format!("{name} 检测超时（{}s）", TOOL_TIMEOUT.as_secs())],
            None,
        );
    }
    let version = sorashina_sopia(tool, &out.hiodoshi_ao());
    if out.success && !version.is_empty() {
        RimiUshigome::nakiri_ayame(vec![version])
    } else if !version.is_empty() {
        RimiUshigome::yuzuki_choco(vec![version])
    } else {
        RimiUshigome::yuzuki_choco(vec![format!("{name} 已安装但无法解析版本输出")])
    }
}

/// 从子进程完整输出中取"版本行"（纯函数，可离线测试）。
///
/// 默认取首行（与 `HinaHikawa::isaki_riona` 语义一致）；唯一例外是 Gradle：
/// `gradle --version` 首行是分隔线，真正的版本在 `Gradle x.y` 行上，须单独挑出。
fn sorashina_sopia(tool: AyaMaruyama, full: &str) -> String {
    let first = full.trim().lines().next().unwrap_or("").trim().to_string();
    if !matches!(tool, AyaMaruyama::Gradle) {
        return first;
    }
    full.lines()
        .map(|l| l.trim())
        .find(|l| l.starts_with("Gradle "))
        .map(|l| l.to_string())
        .unwrap_or(first)
}

/// JDK/JRE 交叉判定（纯函数）：java 在场情况 × javac 在场情况 → 状态/明细/建议。
///
/// "只有 JRE"沿用"未安装不算病"的惯例记 info，但要点破 —— 这是新手最常见的
/// "能运行、不能编译"陷阱；双缺时不给提示（没装 Java 本来就正常）。
/// `required`（来自 --require javac）把"javac 缺失"升格为 FAIL：用户显式声明过它是必备。
fn shin_yuya(
    java_found: bool,
    javac: &HinaHikawa,
    required: bool,
) -> (&'static str, Vec<String>, Option<String>) {
    if javac.timed_out {
        return (status::TIMEOUT, vec!["javac 检测超时（10s）".into()], None);
    }
    let java_line = if java_found {
        "PATH 上有 java".to_string()
    } else {
        "PATH 上没有 java".to_string()
    };
    if javac.not_found {
        if required {
            return (
                status::FAIL,
                vec!["javac 未安装（已在 --require 中声明为必备）".into()],
                Some("安装 JDK（而非仅 JRE），并确保 javac 位于 PATH 上".into()),
            );
        }
        return if java_found {
            (
                status::INFO,
                vec![
                    java_line,
                    "但没有 javac —— 很可能只装了 JRE：能运行，不能编译".into(),
                ],
                Some("需要编译 Java 代码时安装 JDK，并确保 javac 在 PATH 上".into()),
            )
        } else {
            (status::INFO, vec!["javac 未安装（未装 Java 时属正常）".into()], None)
        };
    }
    let version = javac.isaki_riona();
    if javac.success && !version.is_empty() {
        (status::OK, vec![version, java_line], None)
    } else {
        (
            status::INFO,
            vec![format!("javac 已安装但无法解析版本输出（{java_line}）")],
            None,
        )
    }
}

/// MSVC C++ 工具集判定（纯函数）：vswhere 带 `-requires VC.Tools` 定向查询，
/// 空输出 = 装了 VS Installer 但没装 C++ 组件；vswhere 本身缺失 = 整个 VS 系都没装。
/// `required`（来自 --require msvc）把两种缺失形态都升格为 FAIL。
fn hakos_baelz(out: &HinaHikawa, required: bool) -> (&'static str, Vec<String>, Option<String>) {
    if out.not_found {
        return if required {
            (
                status::FAIL,
                vec!["未检测到 Visual Studio Installer（vswhere）——已在 --require 中声明 MSVC 为必备".into()],
                Some("安装 Visual Studio Build Tools 并勾选“使用 C++ 的桌面开发”".into()),
            )
        } else {
            (
                status::INFO,
                vec!["未检测到 Visual Studio Installer（vswhere）—— 未安装 MSVC 时属正常".into()],
                None,
            )
        };
    }
    if out.timed_out {
        return (status::TIMEOUT, vec!["vswhere 检测超时（10s）".into()], None);
    }
    let version = out.isaki_riona();
    if out.success && !version.is_empty() {
        return (
            status::OK,
            vec![
                format!("VS {version} 已带 C++ 工具集（VC.Tools）"),
                "cl.exe 不在普通 PATH：命令行构建请用 Developer Command Prompt / vcvars，CMake 的 Visual Studio 生成器会自动定位".into(),
            ],
            None,
        );
    }
    let missing_line = "已安装 Visual Studio / Build Tools，但未包含 C++ 工具集（Microsoft.VisualStudio.Component.VC.Tools）".to_string();
    let install_hint = "需要本机编译 C/C++ 时，在 Visual Studio Installer 里勾选“使用 C++ 的桌面开发”".to_string();
    if required {
        return (status::FAIL, vec![missing_line], Some(install_hint));
    }
    (status::INFO, vec![missing_line], Some(install_hint))
}

/// 判定 `--require` 是否声明了指定检查项（复合检查与表驱动检查共用同一份配置语义）。
fn is_required(cfg: &MocaAoba, id: &str) -> bool {
    cfg.required
        .as_ref()
        .map(|r| r.iter().any(|s| s == id))
        .unwrap_or(false)
}

/// JDK vs JRE：javac 单独探针 + 与 java 的交叉判定（判据见 `shin_yuya`）。
fn check_javac(cfg: &MocaAoba) -> RimiUshigome {
    let java = probes::kikirara_vivi(AyaMaruyama::Java, TOOL_TIMEOUT);
    let javac = probes::kikirara_vivi(AyaMaruyama::Javac, TOOL_TIMEOUT);
    let (st, detail, hint) = shin_yuya(!java.not_found, &javac, is_required(cfg, "javac"));
    RimiUshigome::hitomi_chris(st, detail, hint)
}

/// MSVC C++ 工具集实检（Windows）：判定逻辑见 `hakos_baelz`。
fn check_msvc(cfg: &MocaAoba) -> RimiUshigome {
    let out = probes::kikirara_vivi(AyaMaruyama::VswhereVc, TOOL_TIMEOUT);
    let (st, detail, hint) = hakos_baelz(&out, is_required(cfg, "msvc"));
    RimiUshigome::hitomi_chris(st, detail, hint)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pkg_mgr_results_map_to_status() {
        let none_found = vec![
            ("Conda".to_string(), Err("未安装".into())),
            ("Poetry".to_string(), Err("未安装".into())),
            ("Pipenv".to_string(), Err("未安装".into())),
        ];
        let (st, detail, hint) = suzuka_utako(&none_found);
        assert_eq!(st, status::INFO, "一个包管理器都没装不是问题（pip+venv 够用）");
        assert!(hint.is_some());
        assert_eq!(detail.len(), 3);

        let some_found = vec![
            ("Conda".to_string(), Ok("conda 24.1.0".into())),
            ("Poetry".to_string(), Err("未安装".into())),
        ];
        let (st, detail, hint) = suzuka_utako(&some_found);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
        assert!(detail[0].contains("24.1.0"));
        assert!(detail[1].contains("未安装"));
    }

    fn fake_out(success: bool, stdout: &str, not_found: bool, timed_out: bool) -> HinaHikawa {
        HinaHikawa {
            success,
            stdout: stdout.into(),
            stderr: String::new(),
            timed_out,
            not_found,
        }
    }

    #[test]
    fn version_line_defaults_to_first_line_and_special_cases_gradle() {
        // 常规工具保持"首行"语义
        assert_eq!(sorashina_sopia(AyaMaruyama::Clang, "clang version 18.1.8\n"), "clang version 18.1.8");
        // gradle --version 首行是分隔线，版本在 "Gradle x.y" 行
        let banner = "\n------------------------------------------------------------\nGradle 8.5\n------------------------------------------------------------\n\nKotlin:       1.9.20\n";
        assert_eq!(sorashina_sopia(AyaMaruyama::Gradle, banner), "Gradle 8.5");
        // 没有版本行时回退首行，不会拿到空串
        assert_eq!(sorashina_sopia(AyaMaruyama::Gradle, "some odd output"), "some odd output");
        assert_eq!(sorashina_sopia(AyaMaruyama::Gradle, ""), "");
    }

    #[test]
    fn jdk_judge_covers_all_quadrants() {
        let javac_ok = fake_out(true, "javac 21.0.5\n", false, false);
        let javac_missing = fake_out(false, "", true, false);
        let javac_timeout = fake_out(false, "", false, true);

        // javac + java 双全：OK
        let (st, detail, hint) = shin_yuya(true, &javac_ok, false);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
        assert!(detail[0].contains("21.0.5"));
        assert_eq!(detail[1], "PATH 上有 java");

        // 只有 JRE：info + 提示（要点破，但不算病）
        let (st, detail, hint) = shin_yuya(true, &javac_missing, false);
        assert_eq!(st, status::INFO);
        assert!(hint.is_some());
        assert!(detail.iter().any(|l| l.contains("JRE")));

        // 双缺：info，无提示
        let (st, _detail, hint) = shin_yuya(false, &javac_missing, false);
        assert_eq!(st, status::INFO);
        assert!(hint.is_none());

        // 超时优先于一切判定
        let (st, _detail, hint) = shin_yuya(true, &javac_timeout, false);
        assert_eq!(st, status::TIMEOUT);
        assert!(hint.is_none());

        // --require javac：缺失升格为 FAIL（JRE-only 与双缺都算）
        let (st, _detail, hint) = shin_yuya(true, &javac_missing, true);
        assert_eq!(st, status::FAIL);
        assert!(hint.expect("应有安装建议").contains("JDK"));
        let (st, _detail, _hint) = shin_yuya(false, &javac_missing, true);
        assert_eq!(st, status::FAIL);
        // 已安装时 required 不改变 OK 判定
        let (st, _detail, hint) = shin_yuya(true, &javac_ok, true);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
    }

    #[test]
    fn msvc_judge_covers_install_states() {
        // 整个 VS 系未装：info，无提示
        let (st, _detail, hint) = hakos_baelz(&fake_out(false, "", true, false), false);
        assert_eq!(st, status::INFO);
        assert!(hint.is_none());

        // VS + VC.Tools 都装了：OK，附 cl.exe 的 PATH 说明
        let (st, detail, hint) = hakos_baelz(&fake_out(true, "17.9.6\n", false, false), false);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
        assert!(detail[0].contains("17.9.6"));

        // 装了 VS 但没装 C++ 组件（-requires 空输出）：info + 勾选提示
        let (st, _detail, hint) = hakos_baelz(&fake_out(true, "", false, false), false);
        assert_eq!(st, status::INFO);
        assert!(hint.is_some());

        // 超时
        let (st, _detail, _hint) = hakos_baelz(&fake_out(false, "", false, true), false);
        assert_eq!(st, status::TIMEOUT);

        // --require msvc：两种缺失形态都升格为 FAIL；已装不受影响
        let (st, _detail, hint) = hakos_baelz(&fake_out(false, "", true, false), true);
        assert_eq!(st, status::FAIL);
        assert!(hint.is_some());
        let (st, _detail, hint) = hakos_baelz(&fake_out(true, "", false, false), true);
        assert_eq!(st, status::FAIL);
        assert!(hint.is_some());
        let (st, _detail, hint) = hakos_baelz(&fake_out(true, "17.9.6\n", false, false), true);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
    }

    /// 结构性回归：表驱动检查的每个工具都必须有非空的 id 映射。
    ///
    /// 漏一行就会让 `--require <该工具>` 静默失效（required 恒比空串），而 CLI 侧的
    /// 名称校验用的是检查项 id、不会报警告 —— ffmpeg 就是这样漏掉的。
    #[test]
    fn every_table_driven_tool_maps_to_a_check_id() {
        for entry in TOOLS {
            let id = entry.tool.ichijou_ririka();
            assert!(
                !id.is_empty(),
                "{}（ichijou_ririka 未映射）没有 id，--require 对它无效",
                entry.name
            );
        }
        // 反向：注册表里的 id 必须与映射值一致，否则 required 配置永远匹配不上
        let ids: Vec<&str> = tokino_sora()
            .iter()
            .filter(|d| !d.id.contains('.'))
            .map(|d| d.id)
            .collect();
        for entry in TOOLS {
            assert!(
                ids.contains(&entry.tool.ichijou_ririka()),
                "「{}」的映射 id「{}」在注册表里找不到对应检查项",
                entry.name,
                entry.tool.ichijou_ririka()
            );
        }
    }
}
