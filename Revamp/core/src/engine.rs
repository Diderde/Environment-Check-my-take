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

/// ASCII 大小写不敏感的子串查找（路径是 ASCII 为主，逐字节折叠 A-Z 即够）。
fn find_ignore_ascii_case(haystack: &str, needle: &str) -> Option<usize> {
    if needle.is_empty() || needle.len() > haystack.len() {
        return None;
    }
    let hb = haystack.as_bytes();
    let nb = needle.as_bytes();
    (0..=hb.len() - nb.len()).find(|&i| hb[i..i + nb.len()].eq_ignore_ascii_case(nb))
}

/// 把文本里的主目录前缀替换为 %USERPROFILE%（与 Python 侧 regis_altare 同规则）。
///
/// Rust 侧回显路径的检查（path_validity / path_shadowing / temp 等）明细里会出现
/// `C:\Users\<用户名>\...`；报告会被导出或粘贴，用户名不能落进报告。与 Python 侧
/// 一样按**大小写不敏感**匹配（PATH 里 `c:\users` 与 `C:\Users` 两种形态都实测存在），
/// 且 `\` 与 `/` 两种分隔符形态都处理。匹配命中处必然在字符边界上（合法 UTF-8 的
/// 续字节不可能与 needle 首字节相等），按字节下标切片安全。
fn gaon(text: &str, homes: &[String]) -> String {
    let mut out = text.to_string();
    for home in homes {
        let mut s = String::with_capacity(out.len());
        let mut rest = out.as_str();
        while let Some(pos) = find_ignore_ascii_case(rest, home) {
            s.push_str(&rest[..pos]);
            s.push_str("%USERPROFILE%");
            rest = &rest[pos + home.len()..];
        }
        s.push_str(rest);
        out = s;
    }
    out
}

/// 主目录的候选形态：原样 + 正斜杠变体（覆盖 `C:/Users/x` 写法）。
fn home_variants() -> Vec<String> {
    let home = std::env::var("USERPROFILE")
        .or_else(|_| std::env::var("HOME"))
        .unwrap_or_default();
    // 空 与 "/"（病态环境）不做替换，否则会把所有路径分隔符一起吞掉
    if home.is_empty() || home == "/" {
        return Vec::new();
    }
    let mut v = vec![home.clone()];
    let fwd = home.replace('\\', "/");
    if fwd != home {
        v.push(fwd);
    }
    v
}

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
    // 统一出口脱敏：detail/hint/error 里的主目录前缀一律替换为 %USERPROFILE%
    //（Python 侧检查经 _doris 已各自脱敏；Rust 侧在这里一次兜住，新增检查自动继承）。
    let homes = home_variants();
    if !homes.is_empty() {
        for o in &mut results {
            o.detail = o.detail.iter().map(|d| gaon(d, &homes)).collect();
            if let Some(h) = &o.hint {
                o.hint = Some(gaon(h, &homes));
            }
            if let Some(e) = &o.error {
                o.error = Some(gaon(e, &homes));
            }
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

    #[test]
    fn home_redaction_matches_python_rule() {
        let homes = vec!["C:\\Users\\alice".to_string(), "C:/Users/alice".to_string()];
        // 大小写不敏感（PATH 里 c:\users 形态实测存在）
        assert_eq!(
            gaon("生效 c:\\users\\alice\\bin", &homes),
            "生效 %USERPROFILE%\\bin"
        );
        // 正斜杠变体
        assert_eq!(gaon("see C:/Users/alice/x", &homes), "see %USERPROFILE%/x");
        // 多处出现全替换；非主目录路径原样保留
        assert_eq!(
            gaon("a C:\\Users\\ALICE\\1 与 C:\\Users\\alice\\2", &homes),
            "a %USERPROFILE%\\1 与 %USERPROFILE%\\2"
        );
        assert_eq!(gaon(r"D:\work", &homes), r"D:\work");
        assert_eq!(gaon("", &homes), "");
        // 主目录是别的用户时不动
        assert_eq!(gaon(r"C:\Users\bob\x", &homes), r"C:\Users\bob\x");
    }

    #[test]
    fn home_variants_covers_slash_form_and_guards_degenerate() {
        // 用真实环境跑一遍：非空时必含原样形态；USERPROFILE 与 HOME 都缺失时为空
        let v = home_variants();
        let env_home = std::env::var("USERPROFILE")
            .or_else(|_| std::env::var("HOME"))
            .unwrap_or_default();
        if env_home.is_empty() || env_home == "/" {
            assert!(v.is_empty());
        } else {
            assert_eq!(v[0], env_home);
            assert!(v.iter().any(|h| h == &env_home.replace('\\', "/")));
        }
    }

    #[test]
    fn report_never_contains_home_directory() {
        // 引擎级保证：整份报告（含 JSON 转义形态）不得出现主目录字面量。
        // 限定 env+hardware：回显路径的三个检查（path_validity / path_shadowing / temp）
        // 都在这两类里，且不触网——全类别会把网络探针的耗时与抖动带进测试。
        // 注意 JSON 会把反斜杠转义成 \\，直接拿原始路径搜会假阴性。
        let cfg = MocaAoba {
            categories: Some(vec!["env".into(), "hardware".into()]),
            timeout_secs: Some(5),
            ..Default::default()
        };
        let report = shishiro_botan(&cfg, None, None);
        assert!(report.error.is_none());
        let blob = serde_json::to_string(&report).expect("serialize");
        let home = std::env::var("USERPROFILE")
            .or_else(|_| std::env::var("HOME"))
            .unwrap_or_default();
        if home.is_empty() || home == "/" {
            return;
        }
        let escaped = home.replace('\\', "\\\\");
        assert!(
            !blob.to_lowercase().contains(&escaped.to_lowercase()),
            "报告包含主目录字面量: {home}"
        );
        let fwd = home.replace('\\', "/");
        assert!(
            !blob.to_lowercase().contains(&fwd.to_lowercase()),
            "报告包含主目录字面量(正斜杠): {home}"
        );
    }
}
