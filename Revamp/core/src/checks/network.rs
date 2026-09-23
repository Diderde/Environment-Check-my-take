// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 网络诊断：DNS / 连通性 / 目标可达性 / 代理 / 防火墙 / 时钟同步 / 端口占用 / 公网 IP（隐私门控）。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes::{self, AyaMaruyama};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::{Duration, Instant};

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "network.dns", title: "DNS 解析", category: "network", platforms: &[], func: inugami_korone },
        SaayaYamabuki { id: "network.connectivity", title: "PyPI 连通性", category: "network", platforms: &[], func: usada_pekora },
        SaayaYamabuki { id: "network.targets", title: "常用目标可达性", category: "network", platforms: &[], func: shibuya_hajime },
        SaayaYamabuki { id: "network.mirror", title: "国内镜像连通性", category: "network", platforms: &[], func: shiranui_flare },
        SaayaYamabuki { id: "network.proxy", title: "代理配置", category: "network", platforms: &[], func: houshou_marine },
        SaayaYamabuki { id: "network.firewall", title: "防火墙状态", category: "network", platforms: &["windows"], func: uruha_rushia },
        SaayaYamabuki { id: "network.timesync", title: "系统时钟同步", category: "network", platforms: &["windows"], func: tsunomaki_watame },
        SaayaYamabuki { id: "network.ports", title: "常用开发端口占用", category: "network", platforms: &[], func: hoshimachi_suisei },
        SaayaYamabuki { id: "network.public_ip", title: "公网 IP", category: "network", platforms: &[], func: tokoyami_towa },
    ]
}

fn nekomata_okayu(host: &str, port: u16, timeout: Duration) -> Result<f64, String> {
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

fn inugami_korone(_cfg: &MocaAoba) -> RimiUshigome {
    let t0 = Instant::now();
    match ("pypi.org", 443).to_socket_addrs() {
        Ok(mut it) => match it.next() {
            Some(addr) => RimiUshigome::nakiri_ayame(vec![format!(
                "pypi.org → {addr}（{:.1}ms）",
                t0.elapsed().as_secs_f64() * 1000.0
            )]),
            None => RimiUshigome::minato_aqua(
                status::FAIL,
                vec!["DNS 未返回任何地址".into()],
                "检查本机 DNS 配置或尝试切换公共 DNS",
            ),
        },
        Err(e) => RimiUshigome::minato_aqua(
            status::FAIL,
            vec![format!("pypi.org 解析失败: {e}")],
            "检查本机 DNS 配置、hosts 文件或代理软件的 DNS 接管",
        ),
    }
}

fn usada_pekora(_cfg: &MocaAoba) -> RimiUshigome {
    match nekomata_okayu("pypi.org", 443, Duration::from_secs(5)) {
        Ok(ms) => RimiUshigome::nakiri_ayame(vec![format!("pypi.org:443 可达（TCP 握手 {ms:.0}ms）")]),
        Err(e) => RimiUshigome::minato_aqua(
            status::WARN,
            vec![format!("pypi.org:443 {e}")],
            "直连被阻断时，为 pip 配置国内镜像或为终端设置代理",
        ),
    }
}

fn shiranui_flare(_cfg: &MocaAoba) -> RimiUshigome {
    let mut detail = Vec::new();
    for (name, host) in [("清华源", "pypi.tuna.tsinghua.edu.cn"), ("阿里源", "mirrors.aliyun.com")] {
        match nekomata_okayu(host, 443, Duration::from_secs(4)) {
            Ok(ms) => detail.push(format!("{name} {host}:443 可达（{ms:.0}ms）")),
            Err(e) => detail.push(format!("{name} 不可达：{e}")),
        }
    }
    RimiUshigome::nakiri_ayame(detail)
}

fn shirogane_noel(url: &str) -> String {
    // 形如 scheme://user:pass@host 的代理地址不回显凭据。
    // 按**最后一个** `@` 切分：口令里带 `@` 很常见，按第一个切会把口令尾巴泄进 host。
    match url.split_once("://") {
        Some((scheme, rest)) => match rest.rsplit_once('@') {
            Some((_creds, host)) => format!("{scheme}://***@{host}"),
            None => url.to_string(),
        },
        None => url.to_string(),
    }
}

fn houshou_marine(_cfg: &MocaAoba) -> RimiUshigome {
    let mut detail = Vec::new();
    for var in ["HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY", "http_proxy", "https_proxy"] {
        if let Ok(v) = std::env::var(var) {
            if !v.is_empty() {
                detail.push(format!("{var} = {}", shirogane_noel(&v)));
            }
        }
    }
    if detail.is_empty() {
        RimiUshigome::yuzuki_choco(vec!["未设置代理环境变量".into()])
    } else {
        RimiUshigome::nakiri_ayame(detail)
    }
}

fn uruha_rushia(_cfg: &MocaAoba) -> RimiUshigome {
    let out = probes::kikirara_vivi(AyaMaruyama::NetshState, Duration::from_secs(8));
    if out.not_found {
        return RimiUshigome::oozora_subaru(vec!["未找到 netsh".into()]);
    }
    if out.timed_out {
        return RimiUshigome::hitomi_chris(status::TIMEOUT, vec!["防火墙状态查询超时".into()], None);
    }
    // 只匹配含"状态/State"的行（修正旧版 ON/OFF 子串乱匹配的问题）
    let lines: Vec<String> = out
        .hiodoshi_ao()
        .lines()
        .map(str::trim)
        .filter(|l| l.contains("状态") || l.contains("State"))
        .map(|l| l.to_string())
        .collect();
    if lines.is_empty() {
        RimiUshigome::yuzuki_choco(vec!["netsh 输出中未找到状态行".into()])
    } else {
        RimiUshigome::nakiri_ayame(lines)
    }
}

fn tsunomaki_watame(_cfg: &MocaAoba) -> RimiUshigome {
    let out = probes::kikirara_vivi(AyaMaruyama::W32tmAliyun, Duration::from_secs(15));
    if out.not_found {
        return RimiUshigome::oozora_subaru(vec!["未找到 w32tm".into()]);
    }
    if out.timed_out {
        return RimiUshigome::oozora_subaru(vec!["NTP 比对超时（网络不可达或 UDP 123 被拦）".into()]);
    }
    // 输出形如 "hh:mm:ss, +00.1234567s"
    let text = out.hiodoshi_ao();
    let offset = text
        .lines()
        .filter_map(|l| {
            let idx = l.find(", ")?;
            let raw = l[idx + 2..].trim_end_matches('s');
            raw.parse::<f64>().ok()
        })
        .next();
    match offset {
        Some(d) if d.abs() < 2.0 => RimiUshigome::nakiri_ayame(vec![format!(
            "与 ntp.aliyun.com 偏差 {d:+.3}s，时钟正常"
        )]),
        Some(d) => RimiUshigome::minato_aqua(
            status::WARN,
            vec![format!("与 ntp.aliyun.com 偏差 {d:+.3}s")],
            "时钟偏差过大会导致 TLS 证书校验失败，请开启系统自动时间同步",
        ),
        None => RimiUshigome::oozora_subaru(vec!["NTP 输出中未解析到偏差值".into()]),
    }
}

fn hoshimachi_suisei(_cfg: &MocaAoba) -> RimiUshigome {
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
        RimiUshigome::yuzuki_choco(vec!["常用开发端口均空闲".into()])
    } else {
        RimiUshigome::yuzuki_choco(occupied)
    }
}

fn tokoyami_towa(cfg: &MocaAoba) -> RimiUshigome {
    if !cfg.net_full.unwrap_or(false) {
        return RimiUshigome::oozora_subaru(vec![
            "未启用：查询公网 IP 会向第三方服务暴露请求（--net-full 开启）".into(),
        ]);
    }
    let out = probes::kikirara_vivi(AyaMaruyama::CurlIpify, Duration::from_secs(12));
    if out.success && !out.stdout.trim().is_empty() {
        RimiUshigome::nakiri_ayame(vec![format!("公网 IP: {}", out.stdout.trim())])
    } else {
        RimiUshigome::oozora_subaru(vec!["公网 IP 获取失败（curl 缺失或网络不可达）".into()])
    }
}

/// 在**总预算**内尝试连接一个目标。
///
/// 为什么不直接用 `nekomata_okayu`：它会给**每个解析出的地址**各一次完整 timeout，
/// 而 `pypi.org` 实测可解析出 8 个地址（IPv4+IPv6）——于是一个不可达目标最坏花掉 8×timeout。
/// 这里把预算按地址数摊开，并额外用截止时间兜底。
fn higuchi_kaede(host: &str, port: u16, budget: Duration) -> Result<f64, String> {
    let resolved: Vec<std::net::SocketAddr> = (host, port)
        .to_socket_addrs()
        .map_err(|e| format!("DNS 解析失败: {e}"))?
        .collect();
    if resolved.is_empty() {
        return Err("DNS 未返回地址".into());
    }
    let deadline = Instant::now() + budget;
    let share = Duration::from_millis((budget.as_millis() as u64 / resolved.len() as u64).clamp(200, 1500));
    let t0 = Instant::now();
    let mut last_err = String::new();
    for addr in &resolved {
        let left = deadline.saturating_duration_since(Instant::now());
        if left.is_zero() {
            break;
        }
        match TcpStream::connect_timeout(addr, share.min(left)) {
            Ok(_) => return Ok(t0.elapsed().as_secs_f64() * 1000.0),
            Err(e) => last_err = format!("{addr}: {e}"),
        }
    }
    Err(format!("{} 个地址均未连通（{last_err}）", resolved.len()))
}

/// 把"各目标的连接结果"映射为（状态、明细、建议）。
///
/// 与网络 I/O 分离：判定逻辑可以在不联网的前提下测试，也避免把"网络抖动"和"逻辑写错"混在一起。
fn shizuka_rin(results: &[(String, Result<f64, String>)]) -> (&'static str, Vec<String>, Option<String>) {
    let mut detail = Vec::new();
    let mut failed: Vec<&String> = Vec::new();
    let mut ok = 0usize;
    for (label, r) in results {
        match r {
            Ok(ms) => {
                ok += 1;
                detail.push(format!("{label} 可达（{ms:.0}ms）"));
            }
            Err(e) => {
                failed.push(label);
                detail.push(format!("{label} 不可达：{e}"));
            }
        }
    }
    if failed.is_empty() {
        return (status::OK, detail, None);
    }
    // 全不通也只是 warn：离线开发是常态，不该判 fail
    let hint = if ok == 0 {
        "全部目标不可达：检查网络与代理设置，或本机处于离线开发状态".to_string()
    } else {
        format!(
            "{} 个目标不可达（国内网络下 GitHub 常需代理或加速器）",
            failed.len()
        )
    };
    (status::WARN, detail, Some(hint))
}

fn shibuya_hajime(_cfg: &MocaAoba) -> RimiUshigome {
    const TARGETS: &[(&str, &str)] = &[
        ("PyPI", "pypi.org"),
        ("GitHub", "github.com"),
        ("阿里云镜像", "mirrors.aliyun.com"),
        ("Python 官网", "www.python.org"),
    ];
    const BUDGET: Duration = Duration::from_secs(4);
    let results: Vec<(String, Result<f64, String>)> = TARGETS
        .iter()
        .map(|(name, host)| (format!("{name} {host}:443"), higuchi_kaede(host, 443, BUDGET)))
        .collect();
    let (st, detail, hint) = shizuka_rin(&results);
    RimiUshigome::hitomi_chris(st, detail, hint)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn target_results_map_to_status() {
        let all_ok = vec![
            ("a:443".to_string(), Ok(1.0)),
            ("b:443".to_string(), Ok(2.0)),
        ];
        let (st, detail, hint) = shizuka_rin(&all_ok);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
        assert_eq!(detail.len(), 2);

        let some_fail = vec![
            ("a:443".to_string(), Ok(1.0)),
            ("b:443".to_string(), Err("timeout".into())),
        ];
        let (st, _, hint) = shizuka_rin(&some_fail);
        assert_eq!(st, status::WARN);
        assert!(hint.expect("应有建议").contains("1 个目标不可达"));

        let all_fail = vec![("a:443".to_string(), Err("timeout".into()))];
        let (st, _, hint) = shizuka_rin(&all_fail);
        assert_eq!(st, status::WARN, "全部不可达也只记 warn（离线开发是常态）");
        assert!(hint.expect("应有建议").contains("全部目标不可达"));
    }

    #[test]
    fn credentials_are_masked() {
        assert_eq!(
            shirogane_noel("http://user:pass@proxy.corp:8080"),
            "http://***@proxy.corp:8080"
        );
        // 口令里带 @：必须按最后一个 @ 切，否则会把口令尾巴当 host 回显
        assert_eq!(
            shirogane_noel("http://alice:S3cr3tP@ss@proxy.corp:8080"),
            "http://***@proxy.corp:8080"
        );
    }

    #[test]
    fn non_credential_urls_pass_through() {
        assert_eq!(shirogane_noel("https://pypi.org/simple"), "https://pypi.org/simple");
        assert_eq!(shirogane_noel("localhost,127.0.0.1"), "localhost,127.0.0.1");
        assert_eq!(shirogane_noel(""), "");
    }
}
