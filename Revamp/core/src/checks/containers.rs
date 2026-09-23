// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 容器环境：Docker / Podman。

use super::{CheckDef, CheckOut};
use crate::model::{status, Config};
use crate::probes::{self, Tool};
use std::time::Duration;

pub fn defs() -> Vec<CheckDef> {
    vec![
        CheckDef { id: "containers.docker", title: "Docker", category: "containers", platforms: &[], func: check_docker },
        CheckDef { id: "containers.podman", title: "Podman", category: "containers", platforms: &[], func: check_podman },
    ]
}

fn check_docker(_cfg: &Config) -> CheckOut {
    let ver = probes::run_tool(Tool::Docker, Duration::from_secs(10));
    if ver.not_found {
        return CheckOut::info(vec!["Docker 未安装".into()]);
    }
    if ver.timed_out {
        return CheckOut::new(status::TIMEOUT, vec!["docker --version 超时".into()], None);
    }
    let mut detail = vec![ver.first_line()];

    // daemon 健康与资源计数
    let info = probes::run_tool(Tool::DockerInfo, Duration::from_secs(10));
    if info.success {
        let server = info.stdout.trim().to_string();
        if !server.is_empty() {
            detail.push(format!("daemon 运行中（server {server}）"));
        }
        let images = probes::run_tool(Tool::DockerImages, Duration::from_secs(10));
        let containers = probes::run_tool(Tool::DockerPs, Duration::from_secs(10));
        let count = |s: &str| s.lines().filter(|l| !l.trim().is_empty()).count();
        if images.success {
            detail.push(format!("本地镜像: {}", count(&images.stdout)));
        }
        if containers.success {
            detail.push(format!("运行中容器: {}", count(&containers.stdout)));
        }
        CheckOut::ok(detail)
    } else {
        CheckOut::problem(
            status::WARN,
            detail,
            "Docker CLI 可用但 daemon 未运行；Windows 下请启动 Docker Desktop 后重测",
        )
    }
}

fn check_podman(_cfg: &Config) -> CheckOut {
    let ver = probes::run_tool(Tool::Podman, Duration::from_secs(10));
    if ver.not_found {
        CheckOut::info(vec!["Podman 未安装".into()])
    } else if ver.timed_out {
        CheckOut::new(status::TIMEOUT, vec!["podman --version 超时".into()], None)
    } else {
        CheckOut::ok(vec![ver.first_line()])
    }
}
