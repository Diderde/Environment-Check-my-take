// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 网络诊断：DNS / 连通性 / 代理 / 防火墙 / 时钟同步 / 端口占用 / 公网 IP（隐私门控）。

use super::{CheckDef, CheckOut};
use crate::model::{status, Config};
use crate::probes::{self, Tool};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::{Duration, Instant};

pub fn defs() -> Vec<CheckDef> {
    vec![
        CheckDef { id: "network.dns", title: "DNS 解析", category: "network", platforms: &[], func: check_dns },
        CheckDef { id: "network.connectivity", title: "PyPI 连通性", category: "network", platforms: &[], func: check_connectivity },
        CheckDef { id: "network.mirror", title: "国内镜像连通性", category: "network", platforms: &[], func: check_mirror },
        CheckDef { id: "network.proxy", title: "代理配置", category: "network", platforms: &[], func: check_proxy },
        CheckDef { id: "network.firewall", title: "防火墙状态", category: "network", platforms: &["windows"], func: check_firewall },
        CheckDef { id: "network.timesync", title: "系统时钟同步", category: "network", platforms: &["windows"], func: check_timesync },
        CheckDef { id: "network.ports", title: "常用开发端口占用", category: "network", platforms: &[], func: check_ports },
        CheckDef { id: "network.public_ip", title: "公网 IP", category: "network", platforms: &[], func: check_public_ip },
    ]
}

fn tcp_connect(host: &str, port: u16, timeout: Duration) -> Result<f64, String> {
    let resolved: Vec<std::net::SocketAddr> = (host, port)
        .to_socket_addrs()
        .map_err(|e| format!("DNS 解析失败: {e}"))?
        .collect();
    if resolved.is_empty() {
        return Err("DNS 未返回地址".into());
    }
    let t0 = Instant::now();
    let mut last_err = String::new();
    for target in &resolved {
        match TcpStream::connect_timeout(target, timeout) {
            Ok(_) => return Ok(t0.elapsed().as_secs_f64() * 1000.0),
            Err(e) => last_err = format!("{target}: {e}"),
        }
    }
    Err(format!(
        "连接失败（{} 个地址均尝试）: {last_err}",
        resolved.len()
    ))
}

fn check_dns(_cfg: &Config) -> CheckOut {
    let t0 = Instant::now();
    match ("pypi.org", 443).to_socket_addrs() {
        Ok(mut it) => match it.next() {
            Some(addr) => CheckOut::ok(vec![format!(
                "pypi.org → {addr}（{:.1}ms）",
                t0.elapsed().as_secs_f64() * 1000.0
            )]),
            None => CheckOut::problem(
                status::FAIL,
                vec!["DNS 未返回任何地址".into()],
                "检查本机 DNS 配置或尝试切换公共 DNS",
            ),
        },
        Err(e) => CheckOut::problem(
            status::FAIL,
            vec![format!("pypi.org 解析失败: {e}")],
            "检查本机 DNS 配置、hosts 文件或代理软件的 DNS 接管",
        ),
    }
}

fn check_connectivity(_cfg: &Config) -> CheckOut {
    match tcp_connect("pypi.org", 443, Duration::from_secs(5)) {
        Ok(ms) => CheckOut::ok(vec![format!("pypi.org:443 可达（TCP 握手 {ms:.0}ms）")]),
        Err(e) => CheckOut::problem(
            status::WARN,
            vec![format!("pypi.org:443 {e}")],
            "直连被阻断时，为 pip 配置国内镜像或为终端设置代理",
        ),
    }
}

fn check_mirror(_cfg: &Config) -> CheckOut {
    let mut detail = Vec::new();
    for (name, host) in [("清华源", "pypi.tuna.tsinghua.edu.cn"), ("阿里源", "mirrors.aliyun.com")] {
        match tcp_connect(host, 443, Duration::from_secs(4)) {
            Ok(ms) => detail.push(format!("{name} {host}:443 可达（{ms:.0}ms）")),
            Err(e) => detail.push(format!("{name} 不可达：{e}")),
        }
    }
    CheckOut::ok(detail)
}

fn mask_credentials(url: &str) -> String {
    // 形如 scheme://user:pass@host 的代理地址不回显凭据
    match url.split_once("://") {
        Some((scheme, rest)) => match rest.split_once('@') {
            Some((_creds, host)) => format!("{scheme}://***@{host}"),
            None => url.to_string(),
        },
        None => url.to_string(),
    }
}

fn check_proxy(_cfg: &Config) -> CheckOut {
    let mut detail = Vec::new();
    for var in ["HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY", "http_proxy", "https_proxy"] {
        if let Ok(v) = std::env::var(var) {
            if !v.is_empty() {
                detail.push(format!("{var} = {}", mask_credentials(&v)));
            }
        }
    }
    if detail.is_empty() {
        CheckOut::info(vec!["未设置代理环境变量".into()])
    } else {
        CheckOut::ok(detail)
    }
}

fn check_firewall(_cfg: &Config) -> CheckOut {
    let out = probes::run_tool(Tool::NetshState, Duration::from_secs(8));
    if out.not_found {
        return CheckOut::skip(vec!["未找到 netsh".into()]);
    }
    if out.timed_out {
        return CheckOut::new(status::TIMEOUT, vec!["防火墙状态查询超时".into()], None);
    }
    // 只匹配含"状态/State"的行（修正旧版 ON/OFF 子串乱匹配的问题）
    let lines: Vec<String> = out
        .combined()
        .lines()
        .map(str::trim)
        .filter(|l| l.contains("状态") || l.contains("State"))
        .map(|l| l.to_string())
        .collect();
    if lines.is_empty() {
        CheckOut::info(vec!["netsh 输出中未找到状态行".into()])
    } else {
        CheckOut::ok(lines)
    }
}

fn check_timesync(_cfg: &Config) -> CheckOut {
    let out = probes::run_tool(Tool::W32tmAliyun, Duration::from_secs(15));
    if out.not_found {
        return CheckOut::skip(vec!["未找到 w32tm".into()]);
    }
    if out.timed_out {
        return CheckOut::skip(vec!["NTP 比对超时（网络不可达或 UDP 123 被拦）".into()]);
    }
    // 输出形如 "hh:mm:ss, +00.1234567s"
    let text = out.combined();
    let offset = text
        .lines()
        .filter_map(|l| {
            let idx = l.find(", ")?;
            let raw = l[idx + 2..].trim_end_matches('s');
            raw.parse::<f64>().ok()
        })
        .next();
    match offset {
        Some(d) if d.abs() < 2.0 => CheckOut::ok(vec![format!(
            "与 ntp.aliyun.com 偏差 {d:+.3}s，时钟正常"
        )]),
        Some(d) => CheckOut::problem(
            status::WARN,
            vec![format!("与 ntp.aliyun.com 偏差 {d:+.3}s")],
            "时钟偏差过大会导致 TLS 证书校验失败，请开启系统自动时间同步",
        ),
        None => CheckOut::skip(vec!["NTP 输出中未解析到偏差值".into()]),
    }
}

fn check_ports(_cfg: &Config) -> CheckOut {
    let mut occupied = Vec::new();
    for (port, name) in [
        (80, "HTTP"),
        (443, "HTTPS"),
        (3000, "Node dev"),
        (8000, "Python dev"),
        (8080, "HTTP-Alt"),
        (8888, "Jupyter"),
    ] {
        if TcpStream::connect_timeout(
            &std::net::SocketAddr::from(([127, 0, 0, 1], port)),
            Duration::from_millis(400),
        )
        .is_ok()
        {
            occupied.push(format!("端口 {port}（{name}）：占用"));
        }
    }
    if occupied.is_empty() {
        CheckOut::info(vec!["常用开发端口均空闲".into()])
    } else {
        CheckOut::info(occupied)
    }
}

fn check_public_ip(cfg: &Config) -> CheckOut {
    if !cfg.net_full.unwrap_or(false) {
        return CheckOut::skip(vec![
            "未启用：查询公网 IP 会向第三方服务暴露请求（--net-full 开启）".into(),
        ]);
    }
    let out = probes::run_tool(Tool::CurlIpify, Duration::from_secs(12));
    if out.success && !out.stdout.trim().is_empty() {
        CheckOut::ok(vec![format!("公网 IP: {}", out.stdout.trim())])
    } else {
        CheckOut::skip(vec!["公网 IP 获取失败（curl 缺失或网络不可达）".into()])
    }
}
