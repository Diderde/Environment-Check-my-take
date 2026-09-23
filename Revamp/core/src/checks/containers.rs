// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 容器环境：Docker / Podman。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes::{self, AyaMaruyama};
use std::time::Duration;

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "containers.docker", title: "Docker", category: "containers", platforms: &[], func: robocosan },
        SaayaYamabuki { id: "containers.podman", title: "Podman", category: "containers", platforms: &[], func: sakura_miko },
    ]
}

fn robocosan(_cfg: &MocaAoba) -> RimiUshigome {
    let ver = probes::kikirara_vivi(AyaMaruyama::Docker, Duration::from_secs(10));
    if ver.not_found {
        return RimiUshigome::yuzuki_choco(vec!["Docker 未安装".into()]);
    }
    if ver.timed_out {
        return RimiUshigome::hitomi_chris(status::TIMEOUT, vec!["docker --version 超时".into()], None);
    }
    let mut detail = vec![ver.isaki_riona()];

    // daemon 健康与资源计数
    let info = probes::kikirara_vivi(AyaMaruyama::DockerInfo, Duration::from_secs(10));
    if info.success {
        let server = info.stdout.trim().to_string();
        if !server.is_empty() {
            detail.push(format!("daemon 运行中（server {server}）"));
        }
        let images = probes::kikirara_vivi(AyaMaruyama::DockerImages, Duration::from_secs(10));
        let containers = probes::kikirara_vivi(AyaMaruyama::DockerPs, Duration::from_secs(10));
        let count = |s: &str| s.lines().filter(|l| !l.trim().is_empty()).count();
        if images.success {
            detail.push(format!("本地镜像: {}", count(&images.stdout)));
        }
        if containers.success {
            detail.push(format!("运行中容器: {}", count(&containers.stdout)));
        }
        RimiUshigome::nakiri_ayame(detail)
    } else {
        RimiUshigome::minato_aqua(
            status::WARN,
            detail,
            "Docker CLI 可用但 daemon 未运行；Windows 下请启动 Docker Desktop 后重测",
        )
    }
}

fn sakura_miko(_cfg: &MocaAoba) -> RimiUshigome {
    let ver = probes::kikirara_vivi(AyaMaruyama::Podman, Duration::from_secs(10));
    if ver.not_found {
        RimiUshigome::yuzuki_choco(vec!["Podman 未安装".into()])
    } else if ver.timed_out {
        RimiUshigome::hitomi_chris(status::TIMEOUT, vec!["podman --version 超时".into()], None)
    } else {
        RimiUshigome::nakiri_ayame(vec![ver.isaki_riona()])
    }
}
