// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 开发工具链检测：git/node/java/go/rust/gcc 等，表驱动。
//!
//! 未安装 ≠ 环境有病：默认报 info；只有通过 --require 声明为必备的工具，
//! 缺失才报 fail 并给出安装提示。

use super::{CheckDef, CheckOut};
use crate::model::{status, Config};
use crate::probes::{self, Tool};
use std::time::Duration;

const TOOL_TIMEOUT: Duration = Duration::from_secs(10);

struct ToolMeta {
    tool: Tool,
    name: &'static str,
}

macro_rules! tools {
    ($(($variant:ident, $name:literal)),+ $(,)?) => {
        const TOOLS: &[ToolMeta] = &[
            $(ToolMeta { tool: Tool::$variant, name: $name }),+
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
        $(fn $fn_name(cfg: &Config) -> CheckOut { check_tool(Tool::$variant, cfg) })+
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

pub fn defs() -> Vec<CheckDef> {
    vec![
        CheckDef { id: "git", title: "Git", category: "toolchains", platforms: &[], func: check_git },
        CheckDef { id: "node", title: "Node.js", category: "toolchains", platforms: &[], func: check_node },
        CheckDef { id: "npm", title: "npm", category: "toolchains", platforms: &[], func: check_npm },
        CheckDef { id: "java", title: "Java", category: "toolchains", platforms: &[], func: check_java },
        CheckDef { id: "go", title: "Go", category: "toolchains", platforms: &[], func: check_go },
        CheckDef { id: "rustc", title: "Rust (rustc)", category: "toolchains", platforms: &[], func: check_rustc },
        CheckDef { id: "cargo", title: "Cargo", category: "toolchains", platforms: &[], func: check_cargo },
        CheckDef { id: "gcc", title: "GCC", category: "toolchains", platforms: &[], func: check_gcc },
        CheckDef { id: "g++", title: "G++", category: "toolchains", platforms: &[], func: check_gxx },
        CheckDef { id: "make", title: "Make", category: "toolchains", platforms: &[], func: check_make },
        CheckDef { id: "dotnet", title: ".NET", category: "toolchains", platforms: &[], func: check_dotnet },
        CheckDef { id: "python", title: "Python（系统级）", category: "toolchains", platforms: &[], func: check_python },
    ]
}

fn check_tool(tool: Tool, cfg: &Config) -> CheckOut {
    let meta = TOOLS.iter().find(|m| m.tool == tool);
    let name = meta.map(|m| m.name).unwrap_or("工具");
    let id = tool.id();
    let required = cfg
        .required
        .as_ref()
        .map(|r| r.iter().any(|s| s == id))
        .unwrap_or(false);

    let out = probes::run_tool(tool, TOOL_TIMEOUT);
    if out.not_found {
        return if required {
            CheckOut::problem(
                status::FAIL,
                vec![format!("{name} 未安装（已在 --require 中声明为必备）")],
                "请安装并确保其位于 PATH 中",
            )
        } else {
            CheckOut::info(vec![format!("{name} 未安装")])
        };
    }
    if out.timed_out {
        return CheckOut::new(status::TIMEOUT, vec![format!("{name} 检测超时（10s）")], None);
    }
    let version = out.first_line();
    if out.success && !version.is_empty() {
        CheckOut::ok(vec![version])
    } else if !version.is_empty() {
        CheckOut::info(vec![version])
    } else {
        CheckOut::info(vec![format!("{name} 已安装但无法解析版本输出")])
    }
}
