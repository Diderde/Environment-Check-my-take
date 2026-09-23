//! 数据模型与配置（serde 序列化，JSON 为跨 ABI 的数据交换格式）。

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub mod status {
    pub const OK: &str = "ok";
    pub const WARN: &str = "warn";
    pub const FAIL: &str = "fail";
    pub const SKIP: &str = "skip";
    pub const INFO: &str = "info";
    pub const TIMEOUT: &str = "timeout";
}

/// 运行配置，由 Python 侧以 JSON 传入。
#[derive(Deserialize, Default, Clone)]
pub struct Config {
    /// 只跑这些类别；None = 全部
    pub categories: Option<Vec<String>>,
    /// 必备工具：这些工具链缺失时报 fail（其余缺失只报 info）
    pub required: Option<Vec<String>>,
    /// 是否启用隐私外发检查（公网 IP 等），默认关闭
    pub net_full: Option<bool>,
    /// 单项检测预算（秒），用于整体超时兜底
    pub timeout_secs: Option<u64>,
}

/// 单个检查项的结果。
#[derive(Serialize, Clone)]
pub struct Outcome {
    pub id: String,
    pub title: String,
    pub category: String,
    pub status: String,
    pub detail: Vec<String>,
    pub hint: Option<String>,
    pub duration_ms: f64,
    pub error: Option<String>,
}

impl Outcome {
    pub fn new(id: &str, title: &str, category: &str, status: &str) -> Self {
        Outcome {
            id: id.to_string(),
            title: title.to_string(),
            category: category.to_string(),
            status: status.to_string(),
            detail: Vec::new(),
            hint: None,
            duration_ms: 0.0,
            error: None,
        }
    }

    pub fn is_problem(&self) -> bool {
        self.status == status::WARN || self.status == status::FAIL
    }
}

#[derive(Serialize)]
pub struct Summary {
    pub counts: BTreeMap<String, u32>,
    pub problems: Vec<String>,
}

#[derive(Serialize)]
pub struct Report {
    pub report_version: u32,
    pub source: String,
    pub platform: String,
    pub generated_at_unix: u64,
    pub duration_ms: f64,
    pub error: Option<String>,
    pub results: Vec<Outcome>,
    pub summary: Summary,
}

impl Report {
    pub fn empty(source: &str) -> Self {
        Report {
            report_version: 1,
            source: source.to_string(),
            platform: crate::engine::platform_name().to_string(),
            generated_at_unix: now_unix(),
            duration_ms: 0.0,
            error: None,
            results: Vec::new(),
            summary: Summary { counts: BTreeMap::new(), problems: Vec::new() },
        }
    }

    pub fn error(source: &str, msg: &str) -> Self {
        let mut r = Report::empty(source);
        r.error = Some(msg.to_string());
        r
    }

    pub fn finish(&mut self, duration_ms: f64) {
        self.duration_ms = duration_ms;
        let mut counts: BTreeMap<String, u32> = BTreeMap::new();
        let mut problems = Vec::new();
        for r in &self.results {
            *counts.entry(r.status.clone()).or_insert(0) += 1;
            if r.is_problem() {
                problems.push(format!("[{}] {}", r.id, r.title));
            }
        }
        self.summary = Summary { counts, problems };
    }
}

pub fn now_unix() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_constants_are_stable() {
        assert_eq!(status::OK, "ok");
        assert_eq!(status::WARN, "warn");
        assert_eq!(status::FAIL, "fail");
        assert_eq!(status::SKIP, "skip");
        assert_eq!(status::INFO, "info");
        assert_eq!(status::TIMEOUT, "timeout");
    }

    #[test]
    fn outcome_problem_semantics() {
        let mut o = Outcome::new("a.b", "T", "cat", status::OK);
        assert!(!o.is_problem());
        o.status = status::WARN.to_string();
        assert!(o.is_problem());
        o.status = status::FAIL.to_string();
        assert!(o.is_problem());
        o.status = status::INFO.to_string();
        assert!(!o.is_problem());
    }

    #[test]
    fn finish_aggregates_counts_and_problems() {
        let mut r = Report::empty("rust");
        let mut a = Outcome::new("x.ok", "A", "c", status::OK);
        a.duration_ms = 1.0;
        let mut b = Outcome::new("x.warn", "B", "c", status::WARN);
        b.hint = Some("fix".into());
        r.results = vec![a, b];
        r.finish(5.0);
        assert_eq!(r.summary.counts.get("ok"), Some(&1));
        assert_eq!(r.summary.counts.get("warn"), Some(&1));
        assert_eq!(r.summary.problems.len(), 1);
        assert_eq!(r.duration_ms, 5.0);
    }

    #[test]
    fn report_serializes_to_json() {
        let mut r = Report::empty("rust");
        r.results.push(Outcome::new("a", "A", "c", status::OK));
        r.finish(0.0);
        let s = serde_json::to_string(&r).expect("serialize");
        assert!(s.contains("\"results\""));
        assert!(s.contains("\"summary\""));
    }
}
