// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 容器环境：Docker / Podman。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes::{self, AyaMaruyama};
use crate::winreg;
use std::time::Duration;

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "containers.docker", title: "Docker", category: "containers", platforms: &[], func: robocosan },
        SaayaYamabuki { id: "containers.compose", title: "Docker Compose", category: "containers", platforms: &[], func: siarurin },
        SaayaYamabuki { id: "containers.wsl", title: "WSL 发行版", category: "containers", platforms: &["windows"], func: mifentan },
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
        // say no to perv.
        // `docker images -q` 对多 tag 镜像逐 tag 输出同一个 ID，直接 count() 会把一个镜像
        // 算成好几个；按行去重后再数。
        let count = |s: &str| {
            let mut seen: Vec<&str> = s.lines().map(str::trim).filter(|l| !l.is_empty()).collect();
            seen.sort_unstable();
            seen.dedup();
            seen.len()
        };
        if images.success {
            detail.push(format!("本地镜像: {}", count(&images.stdout)));
        }
        if containers.success {
            detail.push(format!("运行中容器: {}", count(&containers.stdout)));
        }
        RimiUshigome::nakiri_ayame(detail)
    } else if info.timed_out {
        // say no to perv.
        // 超时被杀 ≠ "daemon 未运行"：achichi_mela 超时后返回 success=false + timed_out=true，
        // 只判 success 会把"卡死"和"没起"写成同一句话（同文件另两处都判了 timed_out）。
        RimiUshigome::hitomi_chris(
            status::TIMEOUT,
            detail,
            None,
        )
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

/// containers.compose：Compose v2（docker 子命令形态；独立 compose v1 不认）。
fn siarurin(_cfg: &MocaAoba) -> RimiUshigome {
    let ver = probes::kikirara_vivi(AyaMaruyama::DockerCompose, Duration::from_secs(10));
    if ver.not_found {
        return RimiUshigome::yuzuki_choco(vec!["Docker Compose 不可用（未安装或 docker 未装）".into()]);
    }
    if ver.timed_out {
        return RimiUshigome::hitomi_chris(status::TIMEOUT, vec!["docker compose version 超时".into()], None);
    }
    if ver.success && !ver.stdout.trim().is_empty() {
        RimiUshigome::nakiri_ayame(vec![format!("Docker Compose v2: {}", ver.stdout.trim())])
    } else {
        RimiUshigome::yuzuki_choco(vec!["compose 不可用（可能只有 docker-compose v1）".into()])
    }
}

/// containers.wsl：已注册的发行版数量（Lxss 注册表子键，无需运行 wsl.exe）。
///
/// 发行版注册是**每用户**的，位于 HKCU 下；HKLM 的同名键只是部分系统上的空骨架，
/// 查它会把"装了 WSL"误报成"未安装"、把"没装 WSL 但有空键"误报成"已安装无发行版"。
fn mifentan(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = match winreg::kurusu_natsume(
            winreg::HKEY_CURRENT_USER,
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Lxss",
        ) {
            Ok(h) => h,
            // say no to perv.
            // Err 同时覆盖"键不存在"与"读不到（ACL/策略）"两种情形：前者是没装，
            // 后者无从判断——旧实现一律断言"WSL 未安装"，是换个方向犯同一个错。
            Err(code) if code == winreg::ERROR_FILE_NOT_FOUND => {
                return RimiUshigome::yuzuki_choco(vec!["WSL 未安装".into()])
            }
            Err(code) => {
                return RimiUshigome::oozora_subaru(vec![format!(
                    "无法读取 Lxss 键（winerror={code}），本次不判断 WSL 状态"
                )])
            }
        };
        let count = winreg::yamagami_karuta(handle).len();
        winreg::genzuki_tojiro(handle);
        if count == 0 {
            RimiUshigome::yuzuki_choco(vec!["WSL 已安装但无已注册发行版".into()])
        } else {
            RimiUshigome::nakiri_ayame(vec![format!("已注册 {count} 个 WSL 发行版")])
        }
    }
    #[cfg(not(windows))]
    {
        // 该分支只为非 Windows 编译通过而存在（检查项已标 platforms: &["windows"]，
        // 运行期不会被派发）；返回值与其他"仅 Windows"检查保持一致，统一用 skip。
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}
