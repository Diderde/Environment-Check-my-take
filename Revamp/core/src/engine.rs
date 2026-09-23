//! 调度引擎：并发执行检查项、整体超时兜底、取消（generation 语义）、进度回调。
//!
//! 相对旧版的修复：
//! - 进度按"已完成数/总数"回调，不再按项数静止僵住；
//! - 取消令牌在派发前检查，取消后剩余项记 SKIP（旧版刷新会产生双链竞争）；
//! - 超时项显式标记 timeout 状态与 error 字段，不再和"未检测到"混在一起。

use crate::checks::{self, CheckDef};
use crate::model::{status, Config, Outcome, Report};
use std::collections::HashSet;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

pub struct CancelToken {
    pub flag: AtomicBool,
}

impl CancelToken {
    pub fn new() -> Self {
        CancelToken { flag: AtomicBool::new(false) }
    }

    pub fn is_cancelled(&self) -> bool {
        self.flag.load(Ordering::SeqCst)
    }
}

impl Default for CancelToken {
    fn default() -> Self {
        Self::new()
    }
}

pub fn platform_name() -> &'static str {
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

pub fn run(
    config: &Config,
    cancel: Option<&CancelToken>,
    progress: Option<Progress>,
) -> Report {
    let t0 = Instant::now();
    let all = checks::all_checks();
    let sys = platform_name();
    let want_cat = |c: &str| {
        config
            .categories
            .as_ref()
            .map(|v| v.iter().any(|x| x == c))
            .unwrap_or(true)
    };
    let selected: Vec<&CheckDef> = all
        .iter()
        .filter(|d| want_cat(d.category) && d.supports(sys))
        .collect();
    let total = selected.len() as u32;

    let (tx, rx) = mpsc::channel::<Outcome>();
    for def in &selected {
        if cancel.map(|c| c.is_cancelled()).unwrap_or(false) {
            break;
        }
        let tx_c = tx.clone();
        let cfg = config.clone();
        let func = def.func;
        let id = def.id.to_string();
        let title = def.title.to_string();
        let category = def.category.to_string();
        thread::spawn(move || {
            let t1 = Instant::now();
            let out = func(&cfg);
            let mut o = Outcome::new(&id, &title, &category, out.status);
            o.detail = out.detail;
            o.hint = out.hint;
            o.duration_ms = t1.elapsed().as_secs_f64() * 1000.0;
            let _ = tx_c.send(o);
        });
    }
    drop(tx);

    let per_check = Duration::from_secs(config.timeout_secs.unwrap_or(25));
    let budget = per_check.saturating_mul(total.max(1));
    let deadline = Instant::now() + budget;
    let mut received: Vec<Outcome> = Vec::with_capacity(total as usize);
    while received.len() < total as usize {
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
            Err(_) => break,
        }
    }

    let cancelled_now = cancel.map(|c| c.is_cancelled()).unwrap_or(false);
    let mut results = std::mem::take(&mut received);
    let seen: HashSet<String> = results.iter().map(|o| o.id.clone()).collect();
    for def in &selected {
        if !seen.contains(def.id) {
            let mut o = Outcome::new(def.id, def.title, def.category, status::SKIP);
            if cancelled_now {
                o.detail.push("已取消".into());
            } else {
                o.status = status::TIMEOUT.to_string();
                o.detail.push(format!("检测超时（预算 {}s，线程未返回）", per_check.as_secs()));
                o.error = Some("timeout".into());
            }
            results.push(o);
        }
    }
    results.sort_by(|a, b| a.category.cmp(&b.category).then(a.id.cmp(&b.id)));

    let mut report = Report::empty("rust");
    report.results = results;
    report.finish(t0.elapsed().as_secs_f64() * 1000.0);
    report
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::Config;

    #[test]
    fn run_with_category_filter_returns_only_that_category() {
        let cfg = Config {
            categories: Some(vec!["databases".into()]),
            timeout_secs: Some(5),
            ..Default::default()
        };
        let report = run(&cfg, None, None);
        assert!(report.error.is_none());
        assert!(!report.results.is_empty());
        for r in &report.results {
            assert_eq!(r.category, "databases");
        }
        assert!(report.duration_ms > 0.0);
    }

    #[test]
    fn cancelled_run_marks_remaining_as_skip() {
        let token = CancelToken::new();
        token.flag.store(true, Ordering::SeqCst);
        let cfg = Config { categories: None, timeout_secs: Some(5), ..Default::default() };
        let report = run(&cfg, Some(&token), None);
        assert!(!report.results.is_empty());
        assert!(report.results.iter().all(|r| r.status == status::SKIP));
    }
}
