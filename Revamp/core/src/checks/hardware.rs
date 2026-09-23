// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 硬件与系统检查：全部走 Windows API（FFI），不依赖已废弃的 wmic。

use super::{SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba};
use crate::probes;

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
