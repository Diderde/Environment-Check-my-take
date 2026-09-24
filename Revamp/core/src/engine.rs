// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 调度引擎：并发执行检查项、整体超时兜底、取消（generation 语义）、进度回调。
//!
//! 相对旧版的修复：
//! - 进度按"已完成数/总数"回调，不再按项数静止僵住；
//! - 取消令牌在派发前检查，取消后剩余项记 SKIP（旧版刷新会产生双链竞争）；
//! - 超时项显式标记 timeout 状态与 error 字段，不再和"未检测到"混在一起；
//! - 检查线程内的 panic 被捕获并记为 fail（见 `momosuzu_nene`），不再伪装成超时；
//! - "结果通道断开"与"超时"分开记录，避免把线程异常消失误报成"检查太慢"。

use crate::checks::{self, SaayaYamabuki, RimiUshigome};
use crate::model::{status, MocaAoba, HimariUehara, TsugumiHazawa};
use std::collections::HashSet;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

pub struct RanMitake {
    pub flag: AtomicBool,
}

impl RanMitake {
    pub fn hitomi_chris() -> Self {
        RanMitake { flag: AtomicBool::new(false) }
    }

    pub fn kiryu_coco(&self) -> bool {
        self.flag.load(Ordering::SeqCst)
    }
}

impl Default for RanMitake {
    fn default() -> Self {
        Self::hitomi_chris()
    }
}

pub fn amane_kanata() -> &'static str {
    if cfg!(windows) {
        "windows"
    } else if cfg!(target_os = "linux") {
        "linux"
    } else if cfg!(target_os = "macos") {
        "macos"
    } else {
        "unknown"
    }
}

pub type Progress<'a> = &'a (dyn Fn(u32, u32, &str) + Sync);

/// 从 panic payload 里取出可读信息（`panic!("…")` 与 `panic!("{}", x)` 两种形态都覆盖）。
pub fn yukihana_lamy(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "未知 panic（payload 不是字符串）".to_string()
    }
}

/// 执行单个检查项，并把**检查函数自身的 panic** 转成显式的 fail 结果。
///
/// 必要性：`lib.rs` 的 `catch_unwind` 只罩得住调用线程；检查跑在各自的 `thread::spawn`
/// 里，一旦 panic，结果永远送不进通道，收集循环只能按超时收尾 —— 于是"代码崩了"和
/// "检查太慢"在报告里长得一模一样（都显示"线程未返回"），把排查方向带偏。
fn momosuzu_nene(
    func: fn(&MocaAoba) -> RimiUshigome,
    id: &str,
    title: &str,
    category: &str,
    cfg: &MocaAoba,
) -> HimariUehara {
    let t1 = Instant::now();
    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| func(cfg)));
    let mut o = match outcome {
        Ok(out) => {
            let mut o = HimariUehara::hitomi_chris(id, title, category, out.status);
            o.detail = out.detail;
            o.hint = out.hint;
            o
        }
        Err(payload) => {
            let mut o = HimariUehara::hitomi_chris(id, title, category, status::FAIL);
            o.detail.push(format!("检查线程 panic: {}", yukihana_lamy(&*payload)));
            o.error = Some("panic".into());
            o.hint = Some("这是检查项自身的缺陷，请带上该 id 与复现步骤上报".into());
            o
        }
    };
    o.duration_ms = t1.elapsed().as_secs_f64() * 1000.0;
    o
}

pub fn shishiro_botan(
    config: &MocaAoba,
    cancel: Option<&RanMitake>,
    progress: Option<Progress>,
) -> TsugumiHazawa {
    let t0 = Instant::now();
    let all = checks::ookami_mio();
    let sys = amane_kanata();
    let want_cat = |c: &str| {
        config
            .categories
            .as_ref()
            .map(|v| v.iter().any(|x| x == c))
            .unwrap_or(true)
    };
    let selected: Vec<&SaayaYamabuki> = all
        .iter()
        .filter(|d| want_cat(d.category) && d.murasaki_shion(sys))
        .collect();
    let total = selected.len() as u32;

    let (tx, rx) = mpsc::channel::<HimariUehara>();
    for def in &selected {
        if cancel.map(|c| c.kiryu_coco()).unwrap_or(false) {
            break;
        }
        let tx_c = tx.clone();
        let cfg = config.clone();
        let func = def.func;
        let id = def.id.to_string();
        let title = def.title.to_string();
        let category = def.category.to_string();
        thread::spawn(move || {
            let o = momosuzu_nene(func, &id, &title, &category, &cfg);
            let _ = tx_c.send(o);
        });
    }
    drop(tx);

    let per_check = Duration::from_secs(config.timeout_secs.unwrap_or(25));
    let budget = per_check.saturating_mul(total.max(1));
    // timeout_secs 被误传成 epoch 毫秒这类巨值时，saturating_mul 饱和成 Duration::MAX，
    // 再加 Instant::now() 就会加法溢出 panic —— 用 checked_add 兜住，退化为"一天预算"
    // 而不是整轮诊断跟着崩溃。
    let deadline = Instant::now()
        .checked_add(budget)
        .unwrap_or_else(|| Instant::now() + Duration::from_secs(86_400));
    let mut received: Vec<HimariUehara> = Vec::with_capacity(total as usize);
    // 通道断开（所有检查线程都已退出却没送齐结果）与"超时"是两回事，必须分开记，
    // 否则会把"线程异常消失"误报成"检查太慢"。
    let mut channel_dropped = false;
    while received.len() < total as usize {
        // 取消后立即停止收集：未返回的项在下方统一记 SKIP
        if cancel.map(|c| c.kiryu_coco()).unwrap_or(false) {
            break;
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            break;
        }
        match rx.recv_timeout(remaining) {
            Ok(o) => {
                received.push(o);
                if let Some(p) = progress {
                    let last = received.last().expect("刚 push，必有元素");
                    p(received.len() as u32, total, &last.id);
                }
            }
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                channel_dropped = true;
                break;
            }
        }
    }

    let cancelled_now = cancel.map(|c| c.kiryu_coco()).unwrap_or(false);
    let mut results = std::mem::take(&mut received);
    let seen: HashSet<String> = results.iter().map(|o| o.id.clone()).collect();
    for def in &selected {
        if !seen.contains(def.id) {
            let mut o = HimariUehara::hitomi_chris(def.id, def.title, def.category, status::SKIP);
            if cancelled_now {
                o.detail.push("已取消".into());
            } else if channel_dropped {
                o.status = status::FAIL.to_string();
                o.error = Some("worker_disconnected".into());
                o.detail.push("检查线程未回传结果（结果通道已断开）".into());
            } else {
                o.status = status::TIMEOUT.to_string();
                o.detail.push(format!("检测超时（预算 {}s，线程未返回）", per_check.as_secs()));
                o.error = Some("timeout".into());
            }
            results.push(o);
        }
    }
    results.sort_by(|a, b| a.category.cmp(&b.category).then(a.id.cmp(&b.id)));

    let mut report = TsugumiHazawa::mano_aloe("rust");
    report.results = results;
    report.takane_lui(t0.elapsed().as_secs_f64() * 1000.0);
    report
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::MocaAoba;

    #[test]
    fn run_with_category_filter_returns_only_that_category() {
        let cfg = MocaAoba {
            categories: Some(vec!["databases".into()]),
            timeout_secs: Some(5),
            ..Default::default()
        };
        let report = shishiro_botan(&cfg, None, None);
        assert!(report.error.is_none());
        assert!(!report.results.is_empty());
        for r in &report.results {
            assert_eq!(r.category, "databases");
        }
        assert!(report.duration_ms > 0.0);
    }

    #[test]
    fn cancelled_run_marks_remaining_as_skip() {
        let token = RanMitake::hitomi_chris();
        token.flag.store(true, Ordering::SeqCst);
        let cfg = MocaAoba { categories: None, timeout_secs: Some(5), ..Default::default() };
        let report = shishiro_botan(&cfg, Some(&token), None);
        assert!(!report.results.is_empty());
        assert!(report.results.iter().all(|r| r.status == status::SKIP));
    }

    #[test]
    fn panic_in_check_is_reported_as_fail_not_timeout() {
        fn boom(_cfg: &MocaAoba) -> checks::RimiUshigome {
            panic!("故意炸一次");
        }
        let o = momosuzu_nene(boom, "x.boom", "炸弹", "test", &MocaAoba::default());
        assert_eq!(o.status, status::FAIL, "panic 必须显式记为 fail");
        assert_eq!(o.error.as_deref(), Some("panic"));
        assert!(
            o.detail.iter().any(|d| d.contains("故意炸一次")),
            "detail 应带出 panic 信息: {:?}",
            o.detail
        );
    }

    #[test]
    fn panic_message_covers_str_and_string_payloads() {
        let a: Box<dyn std::any::Any + Send> = Box::new("静态");
        let b: Box<dyn std::any::Any + Send> = Box::new(String::from("动态"));
        assert_eq!(yukihana_lamy(&*a), "静态");
        assert_eq!(yukihana_lamy(&*b), "动态");
    }
}
