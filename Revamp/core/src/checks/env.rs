// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 环境类检查：PATH 分析、长路径、待重启、VC++ 运行库、Defender 排除项。
//!
//! 全部为系统级探测，不依赖被诊断的解释器——这也是它们归入 Rust 侧的原因。
//! 取数与判定分离：判定写成纯函数（可离线单测），I/O 集中在薄壳函数里。

use super::{RimiUshigome, SaayaYamabuki};
use crate::model::{status, MocaAoba};
use crate::winreg;

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "env.path_validity", title: "PATH 有效性", category: "env", platforms: &[], func: todo_kohaku },
        // 监视名单与路径拼接（{dir}\{name}.exe）都是 Windows 形态：不限平台会在
        // POSIX 上恒报"无遮蔽"（假阴性），故显式限定。
        SaayaYamabuki { id: "env.path_shadowing", title: "PATH 遮蔽", category: "env", platforms: &["windows"], func: lain_paterson },
        SaayaYamabuki { id: "env.longpaths", title: "长路径支持", category: "env", platforms: &["windows"], func: asahina_akane },
        SaayaYamabuki { id: "env.reboot_pending", title: "待重启状态", category: "env", platforms: &["windows"], func: lauren_iroas },
        SaayaYamabuki { id: "env.vcredist", title: "VC++ 运行库", category: "env", platforms: &["windows"], func: leos_vincent },
        SaayaYamabuki { id: "env.defender_exclusions", title: "Defender 路径排除项", category: "env", platforms: &["windows"], func: amagase_muyu },
        SaayaYamabuki { id: "env.uac", title: "UAC 状态", category: "env", platforms: &["windows"], func: tenkai_tsukasa },
        SaayaYamabuki { id: "env.secureboot", title: "安全启动", category: "env", platforms: &["windows"], func: fairys_chan },
        SaayaYamabuki { id: "env.smb1", title: "SMBv1", category: "env", platforms: &["windows"], func: yingou },
        SaayaYamabuki { id: "env.tls_legacy", title: "旧版 TLS 配置", category: "env", platforms: &["windows"], func: qing },
        SaayaYamabuki { id: "env.dev_mode", title: "开发者模式", category: "env", platforms: &["windows"], func: monmon },
    ]
}

/// env.smb1：SMBv1 是否被显式启用（EternalBlue 一族漏洞的目标协议）。
///
/// 键缺失 = 新版 Windows 默认不装不启，属正常；只有显式写 1 才报 warn。
fn yingou(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = match winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters",
        ) {
            Ok(h) => h,
            Err(code) => {
                return RimiUshigome::oozora_subaru(vec![format!("读取注册表失败（winerror={code}）")])
            }
        };
        let enabled = winreg::shirayuki_tomoe(handle, "SMB1");
        winreg::genzuki_tojiro(handle);
        match enabled {
            Ok(1) => RimiUshigome::minato_aqua(
                status::WARN,
                vec!["SMB1 = 1（SMBv1 已启用）".into()],
                "SMBv1 存在已知漏洞且已被现代系统默认弃用；无 legacy 设备对接需求时建议关闭",
            ),
            Ok(0) => RimiUshigome::nakiri_ayame(vec!["SMBv1 已显式禁用".into()]),
            _ => RimiUshigome::nakiri_ayame(vec!["SMB1 未显式配置（新版 Windows 默认不启用）".into()]),
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// env.uac：UAC 是否被完全关闭（关闭 = 进程默认高权限，误装恶意软件代价更大）。
fn tenkai_tsukasa(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        );
        let lua = handle.ok().and_then(|h| {
            let v = winreg::shirayuki_tomoe(h, "EnableLUA").ok();
            winreg::genzuki_tojiro(h);
            v
        });
        let detail = vec![format!(
            "EnableLUA = {}",
            lua.map(|v| v.to_string()).unwrap_or_else(|| "未设置（默认启用）".into())
        )];
        if lua == Some(0) {
            RimiUshigome::minato_aqua(
                status::WARN,
                detail,
                "UAC 已完全关闭：所有进程默认以较高权限运行，误装恶意软件的代价更大；建议在安全中心重新开启",
            )
        } else {
            RimiUshigome::nakiri_ayame(detail)
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// env.secureboot：安全启动是否开启（传统 BIOS / 部分驱动场景会关闭，只记信息）。
fn fairys_chan(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let state_key = "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State";
        if !winreg::hoshikawa_sara(winreg::HKEY_LOCAL_MACHINE, state_key) {
            return RimiUshigome::oozora_subaru(vec![
                "无 SecureBoot\\State 键（传统 BIOS 启动，或平台不提供该状态）".into(),
            ]);
        }
        let handle = match winreg::kurusu_natsume(winreg::HKEY_LOCAL_MACHINE, state_key) {
            Ok(h) => h,
            Err(code) => {
                return RimiUshigome::oozora_subaru(vec![format!("读取注册表失败（winerror={code}）")])
            }
        };
        let enabled = winreg::shirayuki_tomoe(handle, "UEFISecureBootEnabled");
        winreg::genzuki_tojiro(handle);
        match enabled {
            Ok(1) => RimiUshigome::nakiri_ayame(vec!["安全启动已开启".into()]),
            Ok(0) => RimiUshigome::yuzuki_choco(vec![
                "安全启动已关闭（部分驱动/调试场景需要；开启可挡 bootkit 一族）".into(),
            ]),
            Ok(other) => RimiUshigome::yuzuki_choco(vec![format!("UEFISecureBootEnabled = {other}（非预期值）")]),
            Err(code) => {
                RimiUshigome::oozora_subaru(vec![format!("读取失败（winerror={code}）")])
            }
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// 纯函数：旧版 TLS 协议的显式配置 → 结论。
///
/// 注册表键**缺失 = 跟随系统默认策略**（新系统默认禁 TLS 1.0/1.1），不是问题；
/// 只有显式写 Enabled=1 才值得警惕。
fn atles(
    entries: [(&'static str, Option<u32>); 2],
) -> (&'static str, Vec<String>, Option<String>) {
    let mut detail = Vec::new();
    let mut explicit_on = Vec::new();
    let mut explicit_off = Vec::new();
    for (name, state) in entries {
        match state {
            Some(1) => {
                explicit_on.push(name);
                detail.push(format!("{name}: 显式启用"));
            }
            Some(0) => {
                explicit_off.push(name);
                detail.push(format!("{name}: 显式禁用"));
            }
            _ => detail.push(format!("{name}: 跟随系统默认")),
        }
    }
    if !explicit_on.is_empty() {
        (
            status::WARN,
            detail,
            Some("旧版 TLS 被显式打开会降低传输安全基线；无 legacy 对端需求时建议改回禁用".into()),
        )
    } else {
        let hint = (!explicit_off.is_empty())
            .then(|| "已显式禁用旧版 TLS：符合现代安全基线".to_string());
        (status::OK, detail, hint)
    }
}

/// env.tls_legacy：TLS 1.0 / 1.1 的 SCHANNEL 显式配置（键缺失 = 系统默认，属正常）。
fn qing(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let base = "SYSTEM\\CurrentControlSet\\Control\\SecurityProviders\\SCHANNEL\\Protocols";
        let mut entries: [(&'static str, Option<u32>); 2] = [("TLS 1.0", None), ("TLS 1.1", None)];
        let protos = ["TLS 1.0", "TLS 1.1"];
        for (idx, proto) in protos.into_iter().enumerate() {
            let path = format!("{base}\\{proto}\\Client");
            let handle = winreg::kurusu_natsume(winreg::HKEY_LOCAL_MACHINE, &path);
            if let Ok(h) = handle {
                entries[idx].1 = winreg::shirayuki_tomoe(h, "Enabled").ok();
                winreg::genzuki_tojiro(h);
            }
        }
        let (st, detail, hint) = atles(entries);
        RimiUshigome::hitomi_chris(st, detail, hint)
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// env.dev_mode：开发者模式（影响符号链接、侧载等开发能力）。
fn monmon(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock",
        );
        let enabled = handle
            .ok()
            .and_then(|h| {
                let v = winreg::shirayuki_tomoe(h, "AllowDevelopmentWithoutDevLicense").ok();
                winreg::genzuki_tojiro(h);
                v
            })
            .unwrap_or(0);
        if enabled == 1 {
            RimiUshigome::yuzuki_choco(vec!["开发者模式已启用（允许侧载与开发者符号链接）".into()])
        } else {
            RimiUshigome::yuzuki_choco(vec!["开发者模式未启用".into()])
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// env.defender_exclusions：Defender 的路径排除项数量。
///
/// 排除目录不受实时防护，是恶意软件常见落脚点；但排除本身也可能是开发目录的合理配置，
/// 因此只报数量不回显路径（隐私取向与 hosts 检查一致）。
fn amagase_muyu(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths",
        );
        let count = match handle {
            Ok(h) => {
                let n = winreg::yamagami_karuta(h).len();
                winreg::genzuki_tojiro(h);
                n
            }
            Err(_) => {
                return RimiUshigome::oozora_subaru(vec![
                    "无法读取排除项（需要管理员权限，或 Defender 服务未运行）".into(),
                ])
            }
        };
        if count > 0 {
            RimiUshigome::minato_aqua(
                status::WARN,
                vec![format!("路径排除项 {count} 条（内容不回显，建议在安全中心核对）")],
                "被排除的目录不受实时防护，是恶意软件常见落脚点；确为开发目录可保留，来源不明的排除务必删掉",
            )
        } else {
            RimiUshigome::nakiri_ayame(vec!["未配置路径排除项".into()])
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// 纯函数：分析 PATH 字符串（剥引号、忽略空项）。
///
/// "重复"的判定键是 `trim_end_matches(['\\','/']).to_lowercase()`——实测重复项里存在
/// "仅尾斜杠不同"的形态。返回条目数、失效项、重复项、原长度与去重后可缩短的字符数。
fn nishizono_chigusa<'a>(
    raw: &'a str,
    sep: char,
    exists: &dyn Fn(&str) -> bool,
) -> (usize, Vec<&'a str>, Vec<&'a str>, usize, usize) {
    let items: Vec<&str> = raw
        .split(sep)
        .map(|p| p.trim().trim_matches('"'))
        .filter(|p| !p.is_empty())
        .collect();
    let mut seen: std::collections::HashSet<String> = std::collections::HashSet::new();
    let mut unique_len = 0usize;
    let mut unique_seen: std::collections::HashSet<String> = std::collections::HashSet::new();
    let mut invalid = Vec::new();
    let mut dupes = Vec::new();
    for p in &items {
        let key = p.trim_end_matches(['\\', '/']).to_lowercase();
        if !seen.insert(key.clone()) {
            dupes.push(*p);
        } else if unique_seen.insert(key) {
            unique_len += p.len() + sep.len_utf8();
        }
        if !exists(p) {
            invalid.push(*p);
        }
    }
    (
        items.len(),
        invalid,
        dupes,
        raw.len(),
        raw.len().saturating_sub(unique_len.saturating_sub(sep.len_utf8())),
    )
}

/// env.path_validity：PATH 里的失效目录与重复条目。
fn todo_kohaku(_cfg: &MocaAoba) -> RimiUshigome {
    let sep = if cfg!(windows) { ';' } else { ':' };
    let raw = std::env::var("PATH").unwrap_or_default();
    let exists = |p: &str| std::path::Path::new(p).exists();
    let (total, invalid, dupes, length, saved) = nishizono_chigusa(&raw, sep, &exists);
    if total == 0 {
        return RimiUshigome::oozora_subaru(vec!["PATH 为空".into()]);
    }
    let mut detail = vec![format!("条目 {total} 个，共 {length} 字符")];
    if !dupes.is_empty() {
        detail.push(format!("重复条目 {} 个（去重可缩短约 {saved} 字符）", dupes.len()));
    }
    if !invalid.is_empty() {
        detail.push(format!("失效目录 {} 个:", invalid.len()));
        detail.extend(invalid.iter().take(5).map(|p| format!("  {p}")));
        if invalid.len() > 5 {
            detail.push(format!("  …另有 {} 个未列出", invalid.len() - 5));
        }
    }
    if !invalid.is_empty() || !dupes.is_empty() {
        return RimiUshigome::minato_aqua(
            status::WARN,
            detail,
            "失效目录会让命令解析变慢、并掩盖真正的安装位置；重复项多由安装器反复追加，建议清理系统/用户 PATH",
        );
    }
    RimiUshigome::nakiri_ayame(detail)
}

/// 同名可执行文件的监视名单（出现遮蔽时最容易造成"装了但用的不是它"的困惑）。
const SHADOW_WATCH: &[&str] = &["python.exe", "pip.exe", "node.exe", "git.exe"];

/// 纯函数：按 PATH 顺序找出"先被占名、后被遮蔽"的可执行文件。
///
/// PATH 里同一目录出现多次是常态（path_validity 单独计数），先按目录去重再判定；
/// "遮蔽"只计**不同目录**之间的同名冲突。返回 (生效目录列表, 遮蔽描述列表)。
fn axia_krone(dirs: &[&str], names: &[&str], exists: &dyn Fn(&str) -> bool) -> (Vec<String>, Vec<String>) {
    let norm = |d: &str| d.trim_end_matches(['\\', '/']).to_lowercase();
    let mut seen_dirs: std::collections::HashSet<String> = std::collections::HashSet::new();
    let mut unique_dirs: Vec<&str> = Vec::new();
    for d in dirs {
        if d.is_empty() {
            continue;
        }
        if seen_dirs.insert(norm(d)) {
            unique_dirs.push(d);
        }
    }
    let mut winners: std::collections::HashMap<String, String> = std::collections::HashMap::new();
    let mut shadows: Vec<String> = Vec::new();
    for dir in &unique_dirs {
        for &name in names {
            let full = format!("{dir}\\{name}");
            if exists(&full) {
                match winners.get(name) {
                    None => {
                        winners.insert(name.to_string(), (*dir).to_string());
                    }
                    Some(w) => {
                        if norm(w) != norm(dir) {
                            shadows.push(format!(
                                "{name}：生效 {}，被遮蔽 {}",
                                w.trim_end_matches('\\'),
                                dir.trim_end_matches('\\')
                            ));
                        }
                    }
                }
            }
        }
    }
    (winners.into_values().collect(), shadows)
}

/// env.path_shadowing：同名可执行文件被多个 PATH 目录遮蔽（"装了但用的不是它"的根源）。
fn lain_paterson(_cfg: &MocaAoba) -> RimiUshigome {
    let sep = if cfg!(windows) { ';' } else { ':' };
    let raw_path = std::env::var("PATH").unwrap_or_default();
    let dirs: Vec<&str> = raw_path
        .split(sep)
        .map(|p| p.trim().trim_matches('"'))
        .filter(|p| !p.is_empty())
        .collect();
    let exists = |p: &str| std::path::Path::new(p).exists();
    let (_winners, shadows) = axia_krone(&dirs, SHADOW_WATCH, &exists);
    if shadows.is_empty() {
        return RimiUshigome::nakiri_ayame(vec!["监视名单内的可执行文件无遮蔽".into()]);
    }
    let mut detail = vec![format!("{} 项被遮蔽:", shadows.len())];
    detail.extend(shadows.iter().take(6).map(|s| format!("  {s}")));
    if shadows.len() > 6 {
        detail.push(format!("  …另有 {} 条未列出", shadows.len() - 6));
    }
    RimiUshigome::minato_aqua(
        status::WARN,
        detail,
        "生效的是 PATH 上第一个出现的目录；若与预期不符，调整目录顺序或移除多余副本",
    )
}

/// env.longpaths：长路径支持是否开启（深层依赖目录的经典坑）。
fn asahina_akane(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = match winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\FileSystem",
        ) {
            Ok(h) => h,
            Err(code) => {
                return RimiUshigome::oozora_subaru(vec![format!("读取注册表失败（winerror={code}）")])
            }
        };
        let value = winreg::shirayuki_tomoe(handle, "LongPathsEnabled");
        winreg::genzuki_tojiro(handle);
        match value {
            Ok(1) => RimiUshigome::nakiri_ayame(vec!["LongPathsEnabled = 1".into()]),
            Ok(_) => RimiUshigome::minato_aqua(
                status::WARN,
                vec!["LongPathsEnabled = 0".into()],
                "未开启长路径：深层依赖目录（Node/Python 包）会因路径超长报错；可在组策略或注册表开启后重开终端",
            ),
            Err(code) => RimiUshigome::oozora_subaru(vec![format!("读取注册表失败（winerror={code}）")]),
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// 纯函数：三处待重启探针的命中判定（I/O 结果可注入，便于离线测试）。
fn lauren_iroas_verdict(
    marks: [bool; 3],
) -> (&'static str, Vec<String>, Option<String>) {
    let labels = [
        "待重命名的文件（安装器/驱动遗留）",
        "Windows 更新待重启",
        "组件服务(CBS) 待重启",
    ];
    let hits: Vec<&str> = labels
        .iter()
        .zip(marks.iter())
        .filter(|(_, hit)| **hit)
        .map(|(label, _)| *label)
        .collect();
    if hits.is_empty() {
        (status::OK, vec!["三处待重启标记均不存在".into()], None)
    } else {
        let mut detail = vec![format!("命中 {} 项:", hits.len())];
        detail.extend(hits.iter().map(|h| format!("  {h}")));
        (
            status::WARN,
            detail,
            Some(
                "系统更新/驱动装完还没重启：安装程序会因文件被占用而失败，工具链也可能找不到刚更新的组件；\
                 建议先重启一次再继续搭建环境"
                    .to_string(),
            ),
        )
    }
}

/// env.reboot_pending：装完更新/驱动之后是否还没重启。
fn lauren_iroas(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let sm = winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager",
        );
        let pending_rename = match sm {
            Ok(h) => {
                let hit = winreg::mashiro_meme(h, Some("PendingFileRenameOperations"));
                winreg::genzuki_tojiro(h);
                hit
            }
            Err(_) => false,
        };
        let marks = [
            pending_rename,
            winreg::hoshikawa_sara(
                winreg::HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired",
            ),
            winreg::hoshikawa_sara(
                winreg::HKEY_LOCAL_MACHINE,
                "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending",
            ),
        ];
        let (st, detail, hint) = lauren_iroas_verdict(marks);
        RimiUshigome::hitomi_chris(st, detail, hint)
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// 纯函数：把 vcredist 注册表值拼成可读版本号。
///
/// `Version` 通常已是现成字符串（`v14.40.33810.00`），但并非所有版本都写；缺失时按
/// Major/Minor/Bld/Rbld 四个分量拼，拼不出来就返回空串（调用方只报"已安装"）。
fn oliver_evans(
    version: Option<&str>,
    parts: [Option<u32>; 4],
) -> String {
    let version = version.map(str::trim).unwrap_or("");
    if !version.is_empty() {
        return if version.starts_with('v') { version.to_string() } else { format!("v{version}") };
    }
    if parts.iter().any(|p| p.is_none()) {
        return String::new();
    }
    format!(
        "v{}.{}.{}.{}",
        parts[0].unwrap(),
        parts[1].unwrap(),
        parts[2].unwrap(),
        parts[3].unwrap()
    )
}

/// env.vcredist：VC++ 运行库是否已装（缺 VCRUNTIME140.dll 的经典报错）。
fn leos_vincent(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
        );
        let opened = handle.is_ok();
        // 各分量的查询必须全部在 genzuki_tojiro(h) **之前**完成：句柄关了再查得到的是
        // invalid handle，版本分量静默丢失；引擎并发跑检查，句柄值还可能被其他线程复用。
        let (installed, version, parts) = match handle {
            Ok(h) => {
                let installed = winreg::shirayuki_tomoe(h, "Installed").ok();
                let version = winreg::fuwa_minato(h, "Version").ok();
                let parts = [
                    winreg::shirayuki_tomoe(h, "Major").ok(),
                    winreg::shirayuki_tomoe(h, "Minor").ok(),
                    winreg::shirayuki_tomoe(h, "Bld").ok(),
                    winreg::shirayuki_tomoe(h, "Rbld").ok(),
                ];
                winreg::genzuki_tojiro(h);
                (installed, version, parts)
            }
            Err(_) => (None, None, [None, None, None, None]),
        };
        let mut detail = Vec::new();
        if opened || installed.is_some() {
            let version_text = oliver_evans(version.as_deref(), parts);
            detail.push(format!(
                "注册表记录: {}{}",
                if installed.unwrap_or(0) != 0 { "已安装" } else { "未标记已安装" },
                if version_text.is_empty() { String::new() } else { format!(" {version_text}") }
            ));
        } else {
            detail.push("注册表未找到 x64 运行库记录".into());
        }
        let root = std::env::var("SystemRoot").unwrap_or_else(|_| "C:\\Windows".into());
        let dll = std::path::Path::new(&root).join("System32\\vcruntime140.dll");
        let has_dll = dll.is_file();
        detail.push(format!(
            "System32\\vcruntime140.dll: {}",
            if has_dll { "存在" } else { "不存在" }
        ));
        if installed.unwrap_or(0) != 0 || has_dll {
            RimiUshigome::nakiri_ayame(detail)
        } else {
            RimiUshigome::minato_aqua(
                status::WARN,
                detail,
                "很多工具（Python 扩展、Node 原生模块、C++ 命令行工具）会报缺少 VCRUNTIME140.dll；\
                 装一次 Microsoft Visual C++ 2015-2022 可再发行组件包（x64）即可",
            )
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vcredist_version_prefers_ready_string() {
        assert_eq!(oliver_evans(Some("v14.40.33810.00"), [None, None, None, None]), "v14.40.33810.00");
        assert_eq!(oliver_evans(Some("14.38.33130.00"), [None, None, None, None]), "v14.38.33130.00");
    }

    #[test]
    fn vcredist_version_composes_from_parts() {
        // 注册表 Bld/Rbld 是整数值：33810/0 → "v14.40.33810.0"（不是补零的两位写法）
        let v = oliver_evans(None, [Some(14), Some(40), Some(33810), Some(0)]);
        assert_eq!(v, "v14.40.33810.0");
        // 分量不齐时宁可留空（调用方只报"已安装"）
        assert_eq!(oliver_evans(None, [Some(14), None, None, None]), "");
    }

    #[test]
    fn reboot_verdict_maps_hits() {
        let (st, _detail, hint) = lauren_iroas_verdict([false, false, false]);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
        let (st, detail, hint) = lauren_iroas_verdict([true, false, false]);
        assert_eq!(st, status::WARN);
        assert!(hint.expect("应有建议").contains("重启"));
        assert!(detail.iter().any(|d| d.contains("1 项")));
    }

    #[test]
    fn path_analysis_counts_invalid_and_dupes() {
        let exists = |p: &str| !p.ends_with("missing");
        let (total, invalid, dupes, _len, saved) =
            nishizono_chigusa("C:\\Bin;C:\\missing;C:\\bin\\;D:\\x", ';', &exists);
        assert_eq!(total, 4);
        assert_eq!(invalid.len(), 1);
        assert_eq!(dupes.len(), 1);
        assert!(saved > 0);
    }

    #[test]
    fn shadowing_reports_only_shadowed() {
        let dirs = ["C:\\a", "C:\\b", "C:\\c"];
        let names = ["python.exe"];
        let exists = |p: &str| p.starts_with("C:\\a\\") || p.starts_with("C:\\c\\");
        let (winners, shadows) = axia_krone(&dirs, &names, &exists);
        assert!(winners.iter().all(|w| w.contains("C:\\a")));
        assert_eq!(shadows.len(), 1);
        assert!(shadows[0].contains("C:\\c"));
    }
}
