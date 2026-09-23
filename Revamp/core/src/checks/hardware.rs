//! 硬件与系统检查：全部走 Windows API（FFI），不依赖已废弃的 wmic。

use super::{CheckDef, CheckOut};
use crate::model::{status, Config};
use crate::probes;

#[cfg(windows)]
mod ffi {
    #[repr(C)]
    pub struct MemoryStatusEx {
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
    pub struct PowerStatus {
        pub ac_line_status: u8,
        pub battery_flag: u8,
        pub battery_life_percent: u8,
        pub reserved0: u8,
        pub battery_life_time: u32,
        pub battery_full_life_time: u32,
    }

    #[link(name = "kernel32")]
    extern "system" {
        pub fn GlobalMemoryStatusEx(lp: *mut MemoryStatusEx) -> i32;
        pub fn GetTickCount64() -> u64;
        pub fn GetSystemPowerStatus(lp: *mut PowerStatus) -> i32;
        pub fn GetDiskFreeSpaceExW(
            lp_directory_name: *const u16,
            lp_free_bytes_available: *mut u64,
            lp_total_number_of_bytes: *mut u64,
            lp_total_number_of_free_bytes: *mut u64,
        ) -> i32;
    }
}

pub fn defs() -> Vec<CheckDef> {
    vec![
        CheckDef { id: "hardware.memory", title: "内存", category: "hardware", platforms: &["windows"], func: check_memory },
        CheckDef { id: "hardware.uptime", title: "开机时长", category: "hardware", platforms: &["windows"], func: check_uptime },
        CheckDef { id: "hardware.battery", title: "电池", category: "hardware", platforms: &["windows"], func: check_battery },
        CheckDef { id: "hardware.disk", title: "系统盘空间", category: "hardware", platforms: &["windows"], func: check_disk },
        CheckDef { id: "hardware.gpu", title: "显卡", category: "hardware", platforms: &["windows"], func: check_gpu },
    ]
}

fn gb(bytes: u64) -> f64 {
    bytes as f64 / (1024.0 * 1024.0 * 1024.0)
}

fn check_memory(_cfg: &Config) -> CheckOut {
    #[cfg(windows)]
    {
        let size = std::mem::size_of::<ffi::MemoryStatusEx>() as u32;
        let mut m = ffi::MemoryStatusEx {
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
                gb(m.ull_total_phys),
                gb(m.ull_avail_phys),
                m.dw_memory_load
            )];
            if m.dw_memory_load >= 90 {
                CheckOut::problem(status::WARN, detail, "物理内存占用过高，大项目构建/IDE 可能卡顿，考虑关闭占用大户或扩容")
            } else {
                CheckOut::ok(detail)
            }
        } else {
            CheckOut::new(status::FAIL, vec!["GlobalMemoryStatusEx 调用失败".into()], Some("请检查系统权限".into()))
        }
    }
    #[cfg(not(windows))]
    {
        CheckOut::skip(vec!["当前平台未实现".into()])
    }
}

fn check_uptime(_cfg: &Config) -> CheckOut {
    #[cfg(windows)]
    {
        let ms = unsafe { ffi::GetTickCount64() };
        let total_min = ms / 60_000;
        let days = total_min / 60 / 24;
        let hours = (total_min / 60) % 24;
        let mins = total_min % 60;
        CheckOut::info(vec![format!("系统已运行 {days} 天 {hours} 小时 {mins} 分钟")])
    }
    #[cfg(not(windows))]
    {
        CheckOut::skip(vec!["当前平台未实现".into()])
    }
}

fn check_battery(_cfg: &Config) -> CheckOut {
    #[cfg(windows)]
    {
        let mut p = ffi::PowerStatus {
            ac_line_status: 0,
            battery_flag: 0,
            battery_life_percent: 0,
            reserved0: 0,
            battery_life_time: 0,
            battery_full_life_time: 0,
        };
        if unsafe { ffi::GetSystemPowerStatus(&mut p) } == 0 {
            return CheckOut::new(status::FAIL, vec!["GetSystemPowerStatus 调用失败".into()], None);
        }
        // battery_flag 第 7 位（128）= 无电池
        if p.battery_flag & 128 != 0 {
            return CheckOut::info(vec!["未检测到电池（台式机或电池缺失）".into()]);
        }
        let power = if p.ac_line_status == 1 { "交流供电" } else { "电池供电" };
        let percent = if p.battery_life_percent <= 100 {
            format!("{}%", p.battery_life_percent)
        } else {
            "未知".to_string()
        };
        let detail = vec![format!("电量 {percent}（{power}）")];
        if p.ac_line_status == 0 && p.battery_life_percent != 255 && p.battery_life_percent < 20 {
            CheckOut::problem(status::WARN, detail, "电量偏低，编译/安装中途断电可能损坏环境")
        } else {
            CheckOut::ok(detail)
        }
    }
    #[cfg(not(windows))]
    {
        CheckOut::skip(vec!["当前平台未实现".into()])
    }
}

fn check_disk(_cfg: &Config) -> CheckOut {
    #[cfg(windows)]
    {
        let drive = std::env::var("SystemDrive").unwrap_or_else(|_| "C:".to_string());
        let root = format!("{drive}\\");
        let mut free = 0u64;
        let mut total = 0u64;
        let mut total_free = 0u64;
        let wide = probes::to_wide(&root);
        let ok = unsafe {
            ffi::GetDiskFreeSpaceExW(wide.as_ptr(), &mut free, &mut total, &mut total_free)
        };
        if ok != 0 && total > 0 {
            let used_pct = ((total - total_free) as f64 / total as f64 * 100.0) as u32;
            let detail = vec![format!(
                "{root} 总 {:.0}GB / 已用 {used_pct}% / 可用 {:.1}GB",
                gb(total),
                gb(total_free)
            )];
            if gb(total_free) < 10.0 {
                CheckOut::problem(status::WARN, detail, "系统盘可用空间不足 10GB，可能影响包管理器与工具链工作")
            } else {
                CheckOut::ok(detail)
            }
        } else {
            CheckOut::new(status::FAIL, vec![format!("无法获取 {root} 空间信息")], None)
        }
    }
    #[cfg(not(windows))]
    {
        CheckOut::skip(vec!["当前平台未实现".into()])
    }
}

fn check_gpu(_cfg: &Config) -> CheckOut {
    let out = probes::run_tool(probes::Tool::PowershellGpu, std::time::Duration::from_secs(15));
    if out.not_found {
        return CheckOut::skip(vec!["未找到 powershell，无法枚举显卡".into()]);
    }
    if out.timed_out {
        return CheckOut::info(vec!["显卡枚举超时".into()]);
    }
    let gpus: Vec<String> = out
        .stdout
        .lines()
        .map(str::trim)
        .filter(|l| !l.is_empty())
        .map(|l| format!("显卡: {l}"))
        .collect();
    if gpus.is_empty() {
        CheckOut::info(vec!["未获取到显卡信息".into()])
    } else {
        CheckOut::ok(gpus)
    }
}
