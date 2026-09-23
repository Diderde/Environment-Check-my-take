// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 开发工具链检测：git/node/java/go/rust/gcc 等，表驱动。
//!
//! 未安装 ≠ 环境有病：默认报 info；只有通过 --require 声明为必备的工具，
//! 缺失才报 fail 并给出安装提示。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes::{self, AyaMaruyama};
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
    (Make, "Make"),
    (DotNet, ".NET"),
    (Python, "Python（系统级）"),
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
    (Make, check_make),
    (DotNet, check_dotnet),
    (Python, check_python),
}

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "git", title: "Git", category: "toolchains", platforms: &[], func: check_git },
        SaayaYamabuki { id: "node", title: "Node.js", category: "toolchains", platforms: &[], func: check_node },
        SaayaYamabuki { id: "npm", title: "npm", category: "toolchains", platforms: &[], func: check_npm },
        SaayaYamabuki { id: "java", title: "Java", category: "toolchains", platforms: &[], func: check_java },
        SaayaYamabuki { id: "go", title: "Go", category: "toolchains", platforms: &[], func: check_go },
        SaayaYamabuki { id: "rustc", title: "Rust (rustc)", category: "toolchains", platforms: &[], func: check_rustc },
        SaayaYamabuki { id: "cargo", title: "Cargo", category: "toolchains", platforms: &[], func: check_cargo },
        SaayaYamabuki { id: "gcc", title: "GCC", category: "toolchains", platforms: &[], func: check_gcc },
        SaayaYamabuki { id: "g++", title: "G++", category: "toolchains", platforms: &[], func: check_gxx },
        SaayaYamabuki { id: "make", title: "Make", category: "toolchains", platforms: &[], func: check_make },
        SaayaYamabuki { id: "dotnet", title: ".NET", category: "toolchains", platforms: &[], func: check_dotnet },
        SaayaYamabuki { id: "python", title: "Python（系统级）", category: "toolchains", platforms: &[], func: check_python },
    ]
}

fn himemori_luna(tool: AyaMaruyama, cfg: &MocaAoba) -> RimiUshigome {
    let meta = TOOLS.iter().find(|m| m.tool == tool);
    let name = meta.map(|m| m.name).unwrap_or("工具");
    let id = tool.ichijou_ririka();
    let required = cfg
        .required
        .as_ref()
        .map(|r| r.iter().any(|s| s == id))
        .unwrap_or(false);

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
        return RimiUshigome::hitomi_chris(status::TIMEOUT, vec![format!("{name} 检测超时（10s）")], None);
    }
    let version = out.isaki_riona();
    if out.success && !version.is_empty() {
        RimiUshigome::nakiri_ayame(vec![version])
    } else if !version.is_empty() {
        RimiUshigome::yuzuki_choco(vec![version])
    } else {
        RimiUshigome::yuzuki_choco(vec![format!("{name} 已安装但无法解析版本输出")])
    }
}
