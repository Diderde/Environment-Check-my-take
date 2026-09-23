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
        SaayaYamabuki { id: "network.ipv6", title: "IPv6 可用性", category: "network", platforms: &["windows"], func: sister_claire },
    ]
}

/// IPv6 相关 Win32 结构体与 `GetAdaptersAddresses` 声明。
///
/// 只声明到实际读取的字段为止（结构体前缀布局与 SDK 一致），因此不需要枚举
/// `IP_ADAPTER_ADDRESSES_LH` 后面那几十个用不到的成员；`oper_status` 之后的字段全部省略。
#[cfg(windows)]
mod ipv6ffi {
    /// SOCKET_ADDRESS：`{ LPSOCKADDR lpSockaddr; INT iSockaddrLength; }`
    #[repr(C)]
    pub struct KaoruSeta {
        pub lp_sockaddr: *mut u8,
        pub i_sockaddr_length: i32,
    }

    /// SOCKADDR_IN6（28 字节）。
    #[repr(C)]
    pub struct KanonMatsubara {
        pub sin6_family: u16,
        pub sin6_port: u16,
        pub sin6_flowinfo: u32,
        pub sin6_addr: [u8; 16],
        pub sin6_scope_id: u32,
    }

    /// IP_ADAPTER_UNICAST_ADDRESS_LH 的前缀部分。
    #[repr(C)]
    pub struct HagumiKitazawa {
        pub length: u32,
        pub flags: u32,
        pub next: *mut HagumiKitazawa,
        pub address: KaoruSeta,
    }

    /// IP_ADAPTER_ADDRESSES_LH 的前缀部分（到 `OperStatus` 为止）。
    ///
    /// 字段顺序与偏移必须与 SDK 完全一致：0/4 是 Length/IfIndex，8 Next，16 AdapterName，
    /// 24 FirstUnicastAddress，80 PhysicalAddress[8]，88 PhysicalAddressLength，92 Flags，
    /// 96 Mtu，100 IfType，104 OperStatus。
    #[repr(C)]
    pub struct MisakiOkusawa {
        pub length: u32,
        pub if_index: u32,
        pub next: *mut MisakiOkusawa,
        pub adapter_name: *mut u8,
        pub first_unicast: *mut HagumiKitazawa,
        pub first_anycast: *mut u8,
        pub first_multicast: *mut u8,
        pub first_dns_server: *mut u8,
        pub dns_suffix: *mut u16,
        pub description: *mut u16,
        pub friendly_name: *mut u16,
        pub physical_address: [u8; 8],
        pub physical_address_length: u32,
        pub flags: u32,
        pub mtu: u32,
        pub if_type: u32,
        pub oper_status: u32,
    }

    #[link(name = "iphlpapi")]
    extern "system" {
        pub fn GetAdaptersAddresses(
            family: u32,
            flags: u32,
            reserved: *mut core::ffi::c_void,
            addresses: *mut MisakiOkusawa,
            size: *mut u32,
        ) -> u32;
    }
}

/// 纯函数：16 字节 IPv6 地址是否为**可用的原生全球单播**。
///
/// 判据是 `2000::/3`（RFC 4291 全球单播），并排除两种隧道形态：
/// `2001:0000::/32`（Teredo）与 `2002::/16`（6to4）。隧道地址连不通外网目标是常态，
/// 把它们算进来会把"纯 IPv4 + 隧道残留"误报成"IPv6 黑洞"。
#[cfg_attr(not(windows), allow(dead_code))]
fn kanae(bytes: [u8; 16]) -> bool {
    if bytes[0] & 0xe0 != 0x20 {
        return false;
    }
    if bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00 {
        return false;
    }
    if bytes[0] == 0x20 && bytes[1] == 0x02 {
        return false;
    }
    true
}

/// 枚举本机"原生全球单播"IPv6 地址（去重）。
///
/// 只收 Up 状态的非回环/非隧道网卡：断开的网卡可能仍留着地址，拿它判定连通性会误报。
#[cfg(windows)]
fn hanabatake_chaika() -> Vec<std::net::Ipv6Addr> {
    use ipv6ffi::{
        GetAdaptersAddresses, HagumiKitazawa, KanonMatsubara, MisakiOkusawa,
    };
    const AF_INET6: u32 = 23;
    // SKIP_ANYCAST | SKIP_MULTICAST | SKIP_DNS_SERVER：只要单播地址，省掉无关的遍历与内存
    const FLAGS: u32 = 0x2 | 0x4 | 0x8;
    const ERROR_BUFFER_OVERFLOW: u32 = 111;
    const IF_TYPE_SOFTWARE_LOOPBACK: u32 = 24;
    const IF_TYPE_TUNNEL: u32 = 131;
    const IF_OPER_STATUS_UP: u32 = 1;

    // 先要一次所需大小（必然返回 ERROR_BUFFER_OVERFLOW），再按该大小分配。
    let mut size: u32 = 0;
    let rc = unsafe {
        GetAdaptersAddresses(AF_INET6, FLAGS, std::ptr::null_mut(), std::ptr::null_mut(), &mut size)
    };
    if rc != ERROR_BUFFER_OVERFLOW || size == 0 {
        return Vec::new();
    }
    // 用 u64 分配以保证 8 字节对齐：结构体里有指针，按 u8 分配会踩未对齐读取。
    let mut buf = vec![0u64; (size as usize).div_ceil(8)];
    let mut head = buf.as_mut_ptr() as *mut MisakiOkusawa;
    let mut ok = false;
    // 适配器数量在一次调用与下一次之间可能变化（会再次报 OVERFLOW）：重试上限 3 次。
    for _ in 0..3 {
        let rc = unsafe {
            GetAdaptersAddresses(AF_INET6, FLAGS, std::ptr::null_mut(), head, &mut size)
        };
        if rc == 0 {
            ok = true;
            break;
        }
        if rc != ERROR_BUFFER_OVERFLOW {
            return Vec::new();
        }
        buf = vec![0u64; (size as usize).div_ceil(8)];
        head = buf.as_mut_ptr() as *mut MisakiOkusawa;
    }
    if !ok || head.is_null() {
        return Vec::new();
    }

    let mut out: Vec<std::net::Ipv6Addr> = Vec::new();
    let mut cur = head;
    // 链表的边界由系统保证；加一个计数上限纯属防御，避免异常内存导致死循环。
    let mut adapter_guard = 0;
    while !cur.is_null() && adapter_guard < 1024 {
        adapter_guard += 1;
        let adapter = unsafe { &*cur };
        if adapter.oper_status == IF_OPER_STATUS_UP
            && adapter.if_type != IF_TYPE_SOFTWARE_LOOPBACK
            && adapter.if_type != IF_TYPE_TUNNEL
        {
            let mut ua: *mut HagumiKitazawa = adapter.first_unicast;
            let mut unicast_guard = 0;
            while !ua.is_null() && unicast_guard < 4096 {
                unicast_guard += 1;
                let u = unsafe { &*ua };
                let sa = u.address.lp_sockaddr;
                if !sa.is_null()
                    && u.address.i_sockaddr_length as usize >= std::mem::size_of::<KanonMatsubara>()
                {
                    // 用 read_unaligned：SOCKADDR 由系统分配，不保证按 4 字节对齐。
                    let s6 = unsafe { std::ptr::read_unaligned(sa as *const KanonMatsubara) };
                    if s6.sin6_family as u32 == AF_INET6 && kanae(s6.sin6_addr) {
                        let ip = std::net::Ipv6Addr::from(s6.sin6_addr);
                        if !out.contains(&ip) {
                            out.push(ip);
                        }
                    }
                }
                ua = u.next;
            }
        }
        cur = adapter.next;
    }
    out
}

/// 非 Windows：没有跨平台的"本机接口地址枚举"稳定 API，交由调用方记 skip。
#[cfg(not(windows))]
fn hanabatake_chaika() -> Vec<std::net::Ipv6Addr> {
    Vec::new()
}

/// 纯函数：把"本机原生全球 v6 地址"与"对 v6 目标的连接结果"映射为报告结论。
///
/// `connect` 为 `None` 表示"没有地址 / 目标没有 v6 记录"，此时不做连通性断言。
fn ryushen(
    addrs: &[std::net::Ipv6Addr],
    connect: Option<Result<f64, String>>,
) -> (&'static str, Vec<String>, Option<String>) {
    if addrs.is_empty() {
        return (
            status::INFO,
            vec!["未检测到原生全球 IPv6 地址（纯 IPv4 环境，属正常，无需处理）".into()],
            None,
        );
    }
    // 地址本身属于可定位信息（等同公网 IP），只报数量、不回显内容。
    let mut detail = vec![format!("本机原生全球 IPv6 地址 {} 个（不回显地址内容）", addrs.len())];
    match connect {
        None => {
            detail.push("pypi.org 未解析到 IPv6 地址，无法验证 v6 出口".into());
            (status::INFO, detail, None)
        }
        Some(Ok(ms)) => {
            detail.push(format!("pypi.org:443 的 IPv6 路径可达（TCP 握手 {ms:.0}ms）"));
            (status::OK, detail, None)
        }
        Some(Err(e)) => {
            detail.push(format!("pypi.org:443 的 IPv6 路径不可达：{e}"));
            (
                status::WARN,
                detail,
                Some(
                    "IPv6 黑洞：系统会优先尝试 IPv6、失败后才回落 IPv4，表现为 pip/git 偶发卡顿与超时；\
                     可在网卡属性里取消勾选“Internet 协议版本 6 (TCP/IPv6)”，或让路由器下发可用的 IPv6 前缀"
                        .to_string(),
                ),
            )
        }
    }
}

/// network.ipv6：有原生 v6 地址却连不通 v6 目标 = 典型的"IPv6 黑洞"。
fn sister_claire(_cfg: &MocaAoba) -> RimiUshigome {
    if !cfg!(windows) {
        return RimiUshigome::oozora_subaru(vec![
            "当前平台未实现（IPv6 地址枚举走 Windows API）".into(),
        ]);
    }
    let addrs = hanabatake_chaika();
    // 没有 v6 地址时不必联网：省掉一次 DNS 解析 + TCP 尝试。
    let connect = if addrs.is_empty() {
        None
    } else {
        let v6: Vec<std::net::SocketAddr> = ("pypi.org", 443u16)
            .to_socket_addrs()
            .map(|it| it.filter(|a| a.is_ipv6()).collect())
            .unwrap_or_default();
        if v6.is_empty() {
            None
        } else {
            Some(dola(&v6, Duration::from_secs(5)))
        }
    };
    let (st, detail, hint) = ryushen(&addrs, connect);
    RimiUshigome::hitomi_chris(st, detail, hint)
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

/// 在**总预算**内连接给定的若干地址（预算按地址数摊开，并额外用截止时间兜底）。
///
/// 与 `higuchi_kaede` 分开的理由：`network.ipv6` 用的是**同一条**地址列表，只是先按
/// `is_ipv6()` 过滤过；两者共用这段预算逻辑，才能保证"每目标总预算"的语义只有一处实现。
fn dola(resolved: &[std::net::SocketAddr], budget: Duration) -> Result<f64, String> {
    if resolved.is_empty() {
        return Err("DNS 未返回地址".into());
    }
    let deadline = Instant::now() + budget;
    let share = Duration::from_millis((budget.as_millis() as u64 / resolved.len() as u64).clamp(200, 1500));
    let t0 = Instant::now();
    let mut last_err = String::new();
    for addr in resolved {
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

/// 解析主机名并按**每目标总预算**尝试连接。
///
/// 为什么不直接用 `nekomata_okayu`：它会给**每个解析出的地址**各一次完整 timeout，
/// 而 `pypi.org` 实测可解析出 8 个地址（IPv4+IPv6）——于是一个不可达目标最坏花掉 8×timeout。
/// 这里把预算按地址数摊开，并额外用截止时间兜底。
fn higuchi_kaede(host: &str, port: u16, budget: Duration) -> Result<f64, String> {
    let resolved: Vec<std::net::SocketAddr> = (host, port)
        .to_socket_addrs()
        .map_err(|e| format!("DNS 解析失败: {e}"))?
        .collect();
    dola(&resolved, budget)
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

    fn v6(s: &str) -> [u8; 16] {
        match s.parse::<std::net::Ipv6Addr>() {
            Ok(a) => a.octets(),
            Err(e) => panic!("测试用例地址非法 {s}: {e}"),
        }
    }

    #[test]
    fn native_global_ipv6_classification() {
        // 全球单播（2000::/3）且非隧道 → 算可用
        assert!(kanae(v6("2408:8207:1234::1")));
        assert!(kanae(v6("2606:4700::1111")));
        assert!(kanae(v6("2a00:1450:4001::1")));

        // 隧道形态：Teredo 2001:0000::/32、6to4 2002::/16 —— 连不通目标是常态，不算
        assert!(!kanae(v6("2001:0:1234:5678::1")));
        assert!(!kanae(v6("2002:c0a8:0101::1")));

        // 非全球单播
        assert!(!kanae(v6("fe80::1")), "链路本地不算");
        assert!(!kanae(v6("fc00::1")), "ULA 不算");
        assert!(!kanae(v6("::1")), "回环不算");
        assert!(!kanae(v6("fec0::1")), "站点本地不算");
        assert!(!kanae(v6("ff02::1")), "组播不算");
    }

    #[test]
    fn ipv6_verdict_maps_to_status() {
        // 无原生 v6 地址：纯 IPv4 环境是正常状态，不该报问题
        let (st, detail, hint) = ryushen(&[], None);
        assert_eq!(st, status::INFO);
        assert!(detail[0].contains("纯 IPv4"));
        assert!(hint.is_none());

        // 有地址但目标没有 v6 记录：不做连通性断言
        let one = vec!["2408:8207::1".parse().expect("合法地址")];
        let (st, detail, _) = ryushen(&one, None);
        assert_eq!(st, status::INFO);
        assert!(detail[0].contains("1 个"));

        // 有地址且连得通
        let (st, detail, hint) = ryushen(&one, Some(Ok(12.0)));
        assert_eq!(st, status::OK);
        assert!(detail[1].contains("可达"));
        assert!(hint.is_none());

        // 有地址但连不通 = IPv6 黑洞
        let (st, detail, hint) = ryushen(&one, Some(Err("timeout".into())));
        assert_eq!(st, status::WARN);
        assert!(detail[1].contains("不可达"));
        assert!(hint.expect("应给出建议").contains("IPv6 黑洞"));
        // 地址属于可定位信息，不能出现在报告里
        let blob = detail.join("\n");
        assert!(!blob.contains("2408:8207"), "不得回显地址内容: {blob}");
    }

    #[test]
    fn budget_connect_rejects_empty_target_list() {
        // 地址列表为空时直接失败，不假装成功（否则"没有 v6 记录"会被算成"连通")
        let err = dola(&[], Duration::from_secs(1)).expect_err("空列表应报错");
        assert!(err.contains("DNS 未返回地址"));
    }
}
