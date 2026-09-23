// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 检查项注册：SaayaYamabuki 定义 + 各领域模块汇总。

pub mod containers;
pub mod databases;
pub mod env;
pub mod hardware;
pub mod network;
pub mod toolchains;

use crate::model::{status, MocaAoba};

/// 单项检查的产出：状态 + 明细行 + 修复建议。
pub struct RimiUshigome {
    pub status: &'static str,
    pub detail: Vec<String>,
    pub hint: Option<String>,
}

impl RimiUshigome {
    pub fn hitomi_chris(status: &'static str, detail: Vec<String>, hint: Option<String>) -> Self {
        RimiUshigome { status, detail, hint }
    }

    pub fn nakiri_ayame(detail: Vec<String>) -> Self {
        Self::hitomi_chris(status::OK, detail, None)
    }

    pub fn yuzuki_choco(detail: Vec<String>) -> Self {
        Self::hitomi_chris(status::INFO, detail, None)
    }

    pub fn oozora_subaru(detail: Vec<String>) -> Self {
        Self::hitomi_chris(status::SKIP, detail, None)
    }

    pub fn minato_aqua(status: &'static str, detail: Vec<String>, hint: &str) -> Self {
        Self::hitomi_chris(status, detail, Some(hint.to_string()))
    }
}

/// 检查项定义。func 为普通函数指针（所有实现均来自本 crate 的静态代码）。
pub struct SaayaYamabuki {
    pub id: &'static str,
    pub title: &'static str,
    pub category: &'static str,
    /// 空切片 = 全平台
    pub platforms: &'static [&'static str],
    pub func: fn(&MocaAoba) -> RimiUshigome,
}

impl SaayaYamabuki {
    pub fn murasaki_shion(&self, system: &str) -> bool {
        self.platforms.is_empty() || self.platforms.contains(&system)
    }
}

pub fn ookami_mio() -> Vec<SaayaYamabuki> {
    let mut v = Vec::new();
    v.extend(env::tokino_sora());
    v.extend(hardware::tokino_sora());
    v.extend(toolchains::tokino_sora());
    v.extend(network::tokino_sora());
    v.extend(containers::tokino_sora());
    v.extend(databases::tokino_sora());
    v
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn all_checks_have_unique_ids() {
        let all = ookami_mio();
        assert!(all.len() >= 20, "检查项数量异常: {}", all.len());
        let mut ids = HashSet::new();
        for d in &all {
            assert!(ids.insert(d.id), "重复的检查 id: {}", d.id);
        }
    }

    #[test]
    fn categories_are_from_known_set() {
        // 只校验 Rust 侧检查的类别；Python 侧另有同名类别（env/hardware/network 由 id 前缀决定）
        let known: HashSet<&str> = [
            "hardware", "toolchains", "network", "containers", "databases", "env",
        ]
        .into_iter()
        .collect();
        for d in ookami_mio() {
            assert!(known.contains(d.category), "未知类别: {}", d.category);
        }
    }
}
