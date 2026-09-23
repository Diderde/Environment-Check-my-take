// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 硬件与系统检查：全部走 Windows API（FFI），不依赖已废弃的 wmic。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes;
use crate::winreg;

#[cfg(windows)]
mod ffi {
    #[repr(C)]
    pub struct KasumiToyama {
        pub dw_length: u32,
        pub dw_memory_load: u32,
        pub ull_total_phys: u64,
        pub ull_avail_phys: u64,
        pub ull_total_pagefile: u64,
        pub ull_avail_pagefile: u64,
        pub ull_total_virtual: u64,
        pub ull_avail_virtual: u64,
        pub ull_avail_extended_virtual: u64,
    }

    #[repr(C)]
    pub struct TaeHanazono {
        pub ac_line_status: u8,
        pub battery_flag: u8,
        pub battery_life_percent: u8,
        pub reserved0: u8,
        pub battery_life_time: u32,
        pub battery_full_life_time: u32,
    }

    #[link(name = "kernel32")]
    extern "system" {
        pub fn GlobalMemoryStatusEx(lp: *mut KasumiToyama) -> i32;
        pub fn GetTickCount64() -> u64;
        pub fn GetSystemPowerStatus(lp: *mut TaeHanazono) -> i32;
        pub fn GetDiskFreeSpaceExW(
            lp_directory_name: *const u16,
            lp_free_bytes_available: *mut u64,
            lp_total_number_of_bytes: *mut u64,
            lp_total_number_of_free_bytes: *mut u64,
        ) -> i32;
        /// `PF_*` 特性位查询：由系统给出"CPU 与 OS 状态都满足"的答案，
        /// 比自己按 CPUID 判断更准（后者还要额外核对 XSAVE/XCR0 才算数）。
        pub fn IsProcessorFeaturePresent(feature: u32) -> i32;
    }
}

/// `PF_AVX2_INSTRUCTIONS_AVAILABLE` / `PF_AVX512F_INSTRUCTIONS_AVAILABLE`（winnt.h）。
#[cfg(windows)]
const PF_AVX2: u32 = 40;
#[cfg(windows)]
const PF_AVX512F: u32 = 41;

pub fn tokino_sora() -> Vec<SaayaYamabuki> {
    vec![
        SaayaYamabuki { id: "hardware.memory", title: "内存", category: "hardware", platforms: &["windows"], func: shirakami_fubuki },
        SaayaYamabuki { id: "hardware.uptime", title: "开机时长", category: "hardware", platforms: &["windows"], func: natsuiro_matsuri },
        SaayaYamabuki { id: "hardware.battery", title: "电池", category: "hardware", platforms: &["windows"], func: akai_haato },
        SaayaYamabuki { id: "hardware.disk", title: "系统盘空间", category: "hardware", platforms: &["windows"], func: aki_rosenthal },
        SaayaYamabuki { id: "hardware.gpu", title: "显卡", category: "hardware", platforms: &["windows"], func: yozora_mel },
        SaayaYamabuki { id: "hardware.cpu_features", title: "CPU 指令集", category: "hardware", platforms: &["windows"], func: todoroki_kyoko },
        SaayaYamabuki { id: "hardware.pagefile", title: "页面文件", category: "hardware", platforms: &["windows"], func: dailechi },
        SaayaYamabuki { id: "hardware.smart", title: "磁盘健康 (SMART)", category: "hardware", platforms: &["windows"], func: chen_kuang_kuang_probe },
        SaayaYamabuki { id: "hardware.power_plan", title: "电源计划", category: "hardware", platforms: &["windows"], func: kobayakawa_nana },
        SaayaYamabuki { id: "hardware.temp", title: "临时目录", category: "hardware", platforms: &["windows"], func: suo_sango },
        SaayaYamabuki { id: "hardware.disk_io", title: "磁盘写入", category: "hardware", platforms: &["windows"], func: kitakoji_hisui },
    ]
}

/// 纯函数：把 CPU 特性探测结果映射为报告结论。
///
/// 只有**缺 AVX2** 才可行动：不少预编译 wheel（torch / onnxruntime / 部分 numpy 构建）按
/// AVX2 出包，在这类机器上会直接以 `Illegal instruction`（Windows 上 0xC000001D）崩溃。
/// AVX-512 缺失只是信息 —— 它是加分项而非必需项，报成问题属于误报。
#[cfg_attr(not(windows), allow(dead_code))]
fn suzuki_masaru(avx2: bool, avx512: Option<bool>) -> (&'static str, Vec<String>, Option<String>) {
    let mut detail = vec![format!("AVX2: {}", if avx2 { "支持" } else { "不支持" })];
    match avx512 {
        Some(true) => detail.push("AVX-512F: 支持".into()),
        // 较老的 Windows 不认识 41 号特性位、恒返回 0，因此这条只作信息，不参与判定
        Some(false) => detail.push("AVX-512F: 未报告支持（较老系统可能不识别该特性位）".into()),
        None => {}
    }
    if avx2 {
        (status::OK, detail, None)
    } else {
        (
            status::WARN,
            detail,
            Some(
                "缺少 AVX2：部分预编译 wheel（torch / onnxruntime / 部分 numpy 构建）会以 \
                 Illegal instruction 崩溃；请改用不要求 AVX2 的构建，或从源码编译"
                    .to_string(),
            ),
        )
    }
}

/// hardware.cpu_features：AVX2 / AVX-512 是否可用。
fn todoroki_kyoko(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        // ARM64 上这些 x86 特性位恒为 0：报 warn 就是误报，直接记 skip。
        if !cfg!(any(target_arch = "x86_64", target_arch = "x86")) {
            return RimiUshigome::oozora_subaru(vec![
                "当前架构不是 x86/x64，AVX 特性位不适用".into(),
            ]);
        }
        let avx2 = unsafe { ffi::IsProcessorFeaturePresent(PF_AVX2) } != 0;
        let avx512 = unsafe { ffi::IsProcessorFeaturePresent(PF_AVX512F) } != 0;
        let (st, detail, hint) = suzuki_masaru(avx2, Some(avx512));
        RimiUshigome::hitomi_chris(st, detail, hint)
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["当前平台未实现".into()])
    }
}

fn azki(bytes: u64) -> f64 {
    bytes as f64 / (1024.0 * 1024.0 * 1024.0)
}

fn shirakami_fubuki(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let size = std::mem::size_of::<ffi::KasumiToyama>() as u32;
        let mut m = ffi::KasumiToyama {
            dw_length: size,
            dw_memory_load: 0,
            ull_total_phys: 0,
            ull_avail_phys: 0,
            ull_total_pagefile: 0,
            ull_avail_pagefile: 0,
            ull_total_virtual: 0,
            ull_avail_virtual: 0,
            ull_avail_extended_virtual: 0,
        };
        let ok = unsafe { ffi::GlobalMemoryStatusEx(&mut m) };
        if ok != 0 {
            let detail = vec![format!(
                "总内存 {:.2}GB / 可用 {:.2}GB（物理内存占用 {}%）",
                azki(m.ull_total_phys),
                azki(m.ull_avail_phys),
                m.dw_memory_load
            )];
            if m.dw_memory_load >= 90 {
                RimiUshigome::minato_aqua(status::WARN, detail, "物理内存占用过高，大项目构建/IDE 可能卡顿，考虑关闭占用大户或扩容")
            } else {
                RimiUshigome::nakiri_ayame(detail)
            }
        } else {
            RimiUshigome::hitomi_chris(status::FAIL, vec!["GlobalMemoryStatusEx 调用失败".into()], Some("请检查系统权限".into()))
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["当前平台未实现".into()])
    }
}

fn natsuiro_matsuri(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let ms = unsafe { ffi::GetTickCount64() };
        let total_min = ms / 60_000;
        let days = total_min / 60 / 24;
        let hours = (total_min / 60) % 24;
        let mins = total_min % 60;
        RimiUshigome::yuzuki_choco(vec![format!("系统已运行 {days} 天 {hours} 小时 {mins} 分钟")])
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["当前平台未实现".into()])
    }
}

fn akai_haato(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let mut p = ffi::TaeHanazono {
            ac_line_status: 0,
            battery_flag: 0,
            battery_life_percent: 0,
            reserved0: 0,
            battery_life_time: 0,
            battery_full_life_time: 0,
        };
        if unsafe { ffi::GetSystemPowerStatus(&mut p) } == 0 {
            return RimiUshigome::hitomi_chris(status::FAIL, vec!["GetSystemPowerStatus 调用失败".into()], None);
        }
        // battery_flag 第 7 位（128）= 无电池
        if p.battery_flag & 128 != 0 {
            return RimiUshigome::yuzuki_choco(vec!["未检测到电池（台式机或电池缺失）".into()]);
        }
        let power = if p.ac_line_status == 1 { "交流供电" } else { "电池供电" };
        let percent = if p.battery_life_percent <= 100 {
            format!("{}%", p.battery_life_percent)
        } else {
            "未知".to_string()
        };
        let detail = vec![format!("电量 {percent}（{power}）")];
        if p.ac_line_status == 0 && p.battery_life_percent != 255 && p.battery_life_percent < 20 {
            RimiUshigome::minato_aqua(status::WARN, detail, "电量偏低，编译/安装中途断电可能损坏环境")
        } else {
            RimiUshigome::nakiri_ayame(detail)
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["当前平台未实现".into()])
    }
}

fn aki_rosenthal(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let drive = std::env::var("SystemDrive").unwrap_or_else(|_| "C:".to_string());
        let root = format!("{drive}\\");
        let mut free = 0u64;
        let mut total = 0u64;
        let mut total_free = 0u64;
        let wide = probes::todoroki_hajime(&root);
        let ok = unsafe {
            ffi::GetDiskFreeSpaceExW(wide.as_ptr(), &mut free, &mut total, &mut total_free)
        };
        if ok != 0 && total > 0 {
            let used_pct = ((total - total_free) as f64 / total as f64 * 100.0) as u32;
            let detail = vec![format!(
                "{root} 总 {:.0}GB / 已用 {used_pct}% / 可用 {:.1}GB",
                azki(total),
                azki(total_free)
            )];
            if azki(total_free) < 10.0 {
                RimiUshigome::minato_aqua(status::WARN, detail, "系统盘可用空间不足 10GB，可能影响包管理器与工具链工作")
            } else {
                RimiUshigome::nakiri_ayame(detail)
            }
        } else {
            RimiUshigome::hitomi_chris(status::FAIL, vec![format!("无法获取 {root} 空间信息")], None)
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["当前平台未实现".into()])
    }
}

fn yozora_mel(_cfg: &MocaAoba) -> RimiUshigome {
    let out = probes::kikirara_vivi(probes::AyaMaruyama::PowershellGpu, std::time::Duration::from_secs(15));
    if out.not_found {
        return RimiUshigome::oozora_subaru(vec!["未找到 powershell，无法枚举显卡".into()]);
    }
    if out.timed_out {
        return RimiUshigome::yuzuki_choco(vec!["显卡枚举超时".into()]);
    }
    let gpus: Vec<String> = out
        .stdout
        .lines()
        .map(str::trim)
        .filter(|l| !l.is_empty())
        .map(|l| format!("显卡: {l}"))
        .collect();
    if gpus.is_empty() {
        RimiUshigome::yuzuki_choco(vec!["未获取到显卡信息".into()])
    } else {
        RimiUshigome::nakiri_ayame(gpus)
    }
}

/// 纯函数：PagingFiles 注册表内容 → 结论（空/系统托管/显式列表）。
fn nagao_kei(files: &[String]) -> (&'static str, Vec<String>, Option<String>) {
    if files.is_empty() {
        return (
            status::WARN,
            vec!["未配置任何页面文件（或已全部禁用）".into()],
            Some(
                "内存吃紧时进程会被直接终止而不是换页；大项目编译/跑容器的机器建议保留系统托管的页面文件"
                    .to_string(),
            ),
        );
    }
    let managed = files.iter().any(|f| f.starts_with("?:\\") || f.contains("\\??\\"));
    let mut detail: Vec<String> = files.iter().map(|f| format!("  {f}")).collect();
    if managed {
        detail.insert(0, "页面文件: 系统托管".into());
        (status::OK, detail, None)
    } else {
        detail.insert(0, "页面文件: 手工配置".into());
        (status::OK, detail, None)
    }
}

/// hardware.pagefile：页面文件配置（Session Manager\\Memory Management\\PagingFiles）。
fn dailechi(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let handle = match winreg::kurusu_natsume(
            winreg::HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
        ) {
            Ok(h) => h,
            Err(code) => {
                return RimiUshigome::oozora_subaru(vec![format!("读取注册表失败（winerror={code}）")])
            }
        };
        let files = winreg::siddel(handle, "PagingFiles").unwrap_or_default();
        winreg::genzuki_tojiro(handle);
        let (st, detail, hint) = nagao_kei(&files);
        RimiUshigome::hitomi_chris(st, detail, hint)
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// 纯函数：磁盘 Status 行 → 结论。
fn chen_kuang_kuang(lines: &[String]) -> (&'static str, Vec<String>, Option<String>) {
    let drives: Vec<String> = lines
        .iter()
        .filter(|l| !l.trim().is_empty())
        .map(|l| {
            let (model, status) = l.split_once('|').unwrap_or((l.as_str(), "未知"));
            format!("{} → {}", model.trim(), status.trim())
        })
        .collect();
    if drives.is_empty() {
        return (status::SKIP, vec!["未获取到磁盘信息".into()], None);
    }
    let bad: Vec<&String> = drives
        .iter()
        .filter(|d| !d.to_lowercase().contains("→ ok"))
        .collect();
    if bad.is_empty() {
        (status::OK, drives, None)
    } else {
        (
            status::WARN,
            drives,
            Some("存在状态异常的物理磁盘：SMART 预警意味着数据风险，建议尽快备份并更换".into()),
        )
    }
}

/// hardware.smart：物理磁盘 SMART 状态（Win32_DiskDrive.Status，无需管理员）。
fn chen_kuang_kuang_probe(_cfg: &MocaAoba) -> RimiUshigome {
    let out = probes::kikirara_vivi(probes::AyaMaruyama::PsDiskHealth, std::time::Duration::from_secs(15));
    if out.not_found {
        return RimiUshigome::oozora_subaru(vec!["未找到 powershell".into()]);
    }
    if out.timed_out {
        return RimiUshigome::yuzuki_choco(vec!["磁盘健康查询超时".into()]);
    }
    let lines: Vec<String> = out
        .stdout
        .lines()
        .map(|l| l.trim().to_string())
        .filter(|l| !l.is_empty())
        .collect();
    let (st, detail, hint) = chen_kuang_kuang(&lines);
    RimiUshigome::hitomi_chris(st, detail, hint)
}

/// hardware.power_plan：活动电源计划（节能计划会明显拖慢编译）。
fn kobayakawa_nana(_cfg: &MocaAoba) -> RimiUshigome {
    let out = probes::kikirara_vivi(probes::AyaMaruyama::PowerCfgActive, std::time::Duration::from_secs(10));
    if out.not_found {
        return RimiUshigome::oozora_subaru(vec!["未找到 powercfg".into()]);
    }
    if out.timed_out {
        return RimiUshigome::yuzuki_choco(vec!["电源计划查询超时".into()]);
    }
    let text = out.isaki_riona();
    if text.is_empty() {
        return RimiUshigome::yuzuki_choco(vec!["未获取到电源计划".into()]);
    }
    if text.contains("节电") || text.to_lowercase().contains("power saver") || text.contains("节能") {
        return RimiUshigome::minato_aqua(
            status::WARN,
            vec![text],
            "节电计划会限制 CPU 频率，编译/测试明显变慢；建议改用高性能或平衡计划",
        );
    }
    RimiUshigome::nakiri_ayame(vec![text])
}


/// 纯函数：临时目录可用空间 → 结论。
#[cfg_attr(not(windows), allow(dead_code))]
fn temp_verdict(free_gb: f64, probe_ok: bool, detail: &mut Vec<String>) -> &'static str {
    if probe_ok {
        detail.push("读写探针: 通过".into());
    } else {
        detail.push("读写探针: 失败".into());
        return status::FAIL;
    }
    if free_gb < 2.0 {
        detail.push("可用空间不足 2GB，构建与解包可能中途失败".into());
        return status::WARN;
    }
    status::OK
}

/// hardware.temp：临时目录可用空间与读写探针（构建/解包失败的常见根因）。
fn suo_sango(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        let dir = std::env::var("TEMP")
            .or_else(|_| std::env::var("TMP"))
            .unwrap_or_else(|_| "C:\\Temp".into());
        let mut detail = vec![format!("临时目录: {dir}")];
        let mut free = 0u64;
        let mut total = 0u64;
        let mut _total_free = 0u64;
        let wide = probes::todoroki_hajime(&format!("{dir}\\"));
        let ok = unsafe {
            ffi::GetDiskFreeSpaceExW(wide.as_ptr(), &mut free, &mut total, &mut _total_free)
        };
        if ok == 0 {
            detail.push("无法读取可用空间".into());
            return RimiUshigome::hitomi_chris(status::SKIP, detail, None);
        }
        let free_gb = azki(free);
        detail.push(format!(
            "可用 {free_gb:.1}GB / 总 {:.1}GB",
            azki(total)
        ));
        // 1KB 写探针 + fsync
        let probe = std::path::PathBuf::from(&dir).join(".envdoctor_probe");
        let probe_ok = std::fs::File::create(&probe)
            .and_then(|mut f| {
                use std::io::Write;
                f.write_all(b"x").and_then(|_| f.sync_all())
            })
            .is_ok();
        let _ = std::fs::remove_file(&probe);
        let st = temp_verdict(free_gb, probe_ok, &mut detail);
        RimiUshigome::hitomi_chris(st, detail, None)
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

/// hardware.disk_io：临时目录 1MB 写入 + fsync 的真实耗时。
///
/// 只测写入：写完立刻读回几乎全命中页缓存，读耗时无参考价值。
fn kitakoji_hisui(_cfg: &MocaAoba) -> RimiUshigome {
    #[cfg(windows)]
    {
        use std::time::Instant;
        let dir = std::env::var("TEMP")
            .or_else(|_| std::env::var("TMP"))
            .unwrap_or_else(|_| "C:\\Temp".into());
        let probe = std::path::PathBuf::from(&dir).join(".envdoctor_io_probe");
        let t0 = Instant::now();
        let write_result = (|| -> std::io::Result<()> {
            let mut f = std::fs::File::create(&probe)?;
            use std::io::Write;
            f.write_all(&[b'x'; 1024 * 1024])?;
            f.sync_all()
        })();
        let ms = t0.elapsed().as_secs_f64() * 1000.0;
        let _ = std::fs::remove_file(&probe);
        match write_result {
            Err(e) => RimiUshigome::hitomi_chris(
                status::SKIP,
                vec![format!("写入探针失败: {e}")],
                None,
            ),
            Ok(_) => {
                if ms > 1000.0 {
                    RimiUshigome::minato_aqua(
                        status::WARN,
                        vec![format!("1MB 写入 + fsync: {ms:.0}ms")],
                        "写入异常慢：常见于杀软实时扫描、机械盘、或磁盘接近写满",
                    )
                } else {
                    RimiUshigome::yuzuki_choco(vec![format!("1MB 写入 + fsync: {ms:.0}ms")])
                }
            }
        }
    }
    #[cfg(not(windows))]
    {
        RimiUshigome::oozora_subaru(vec!["仅 Windows".into()])
    }
}

#[cfg(test)]
mod hw_verdict_tests {
    use super::*;

    #[test]
    fn pagefile_verdict_flags_empty_config() {
        let (st, detail, hint) = nagao_kei(&[]);
        assert_eq!(st, status::WARN);
        assert!(hint.expect("应有建议").contains("页面文件"));
        assert_eq!(detail.len(), 1);
    }

    #[test]
    fn pagefile_verdict_ok_when_managed() {
        let (st, _detail, hint) = nagao_kei(&["?:\\pagefile.sys".to_string()]);
        assert_eq!(st, status::OK);
        assert!(hint.is_none());
    }

    #[test]
    fn smart_verdict_maps_status_lines() {
        let lines = vec!["Samsung SSD|OK".to_string(), "WDC HDD|Pred Fail".to_string()];
        let (st, detail, hint) = chen_kuang_kuang(&lines);
        assert_eq!(st, status::WARN);
        assert!(hint.expect("应有建议").contains("备份"));
        assert_eq!(detail.len(), 2);

        let ok_lines = vec!["Samsung SSD|OK".to_string()];
        let (st, detail, hint) = chen_kuang_kuang(&ok_lines);
        assert_eq!(st, status::OK);
        assert_eq!(detail.len(), 1);
    }

    #[test]
    fn smart_verdict_skips_when_no_data() {
        let (st, detail, _hint) = chen_kuang_kuang(&[]);
        assert_eq!(st, status::SKIP);
        assert!(detail[0].contains("未获取到磁盘信息"));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cpu_feature_verdict_maps_to_status() {
        // 有 AVX2：正常，且 AVX-512 只是信息行
        let (st, detail, hint) = suzuki_masaru(true, Some(false));
        assert_eq!(st, status::OK);
        assert!(detail[0].contains("AVX2: 支持"));
        assert!(detail[1].contains("AVX-512F"));
        assert!(hint.is_none());

        // 缺 AVX2 才是可行动的：预编译 wheel 会 Illegal instruction
        let (st, detail, hint) = suzuki_masaru(false, None);
        assert_eq!(st, status::WARN);
        assert!(detail.iter().any(|d| d.contains("AVX2: 不支持")));
        let hint = hint.expect("应给出建议");
        assert!(hint.contains("AVX2"), "{hint}");
        assert_eq!(detail.len(), 1, "未探测 AVX-512 时不应凭空加一行: {detail:?}");
    }
}
