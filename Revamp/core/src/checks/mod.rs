//! 检查项注册：CheckDef 定义 + 各领域模块汇总。

pub mod containers;
pub mod databases;
pub mod hardware;
pub mod network;
pub mod toolchains;

use crate::model::{status, Config};

/// 单项检查的产出：状态 + 明细行 + 修复建议。
pub struct CheckOut {
    pub status: &'static str,
    pub detail: Vec<String>,
    pub hint: Option<String>,
}

impl CheckOut {
    pub fn new(status: &'static str, detail: Vec<String>, hint: Option<String>) -> Self {
        CheckOut { status, detail, hint }
    }

    pub fn ok(detail: Vec<String>) -> Self {
        Self::new(status::OK, detail, None)
    }

    pub fn info(detail: Vec<String>) -> Self {
        Self::new(status::INFO, detail, None)
    }

    pub fn skip(detail: Vec<String>) -> Self {
        Self::new(status::SKIP, detail, None)
    }

    pub fn problem(status: &'static str, detail: Vec<String>, hint: &str) -> Self {
        Self::new(status, detail, Some(hint.to_string()))
    }
}

/// 检查项定义。func 为普通函数指针（所有实现均来自本 crate 的静态代码）。
pub struct CheckDef {
    pub id: &'static str,
    pub title: &'static str,
    pub category: &'static str,
    /// 空切片 = 全平台
    pub platforms: &'static [&'static str],
    pub func: fn(&Config) -> CheckOut,
}

impl CheckDef {
    pub fn supports(&self, system: &str) -> bool {
        self.platforms.is_empty() || self.platforms.contains(&system)
    }
}

pub fn all_checks() -> Vec<CheckDef> {
    let mut v = Vec::new();
    v.extend(hardware::defs());
    v.extend(toolchains::defs());
    v.extend(network::defs());
    v.extend(containers::defs());
    v.extend(databases::defs());
    v
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn all_checks_have_unique_ids() {
        let all = all_checks();
        assert!(all.len() >= 20, "检查项数量异常: {}", all.len());
        let mut ids = HashSet::new();
        for d in &all {
            assert!(ids.insert(d.id), "重复的检查 id: {}", d.id);
        }
    }

    #[test]
    fn categories_are_from_known_set() {
        let known: HashSet<&str> = ["hardware", "toolchains", "network", "containers", "databases"]
            .into_iter()
            .collect();
        for d in all_checks() {
            assert!(known.contains(d.category), "未知类别: {}", d.category);
        }
    }
}
