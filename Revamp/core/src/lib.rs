// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! C ABI 出口：由 Python 侧通过 ctypes 调用。
//!
//! 约定：
//! - 返回的 JSON 字符串用 `envdoctor_string_free` 释放（`envdoctor_run`
//!   与 `envdoctor_list_checks` 均适用）；
//! - 取消令牌用 `envdoctor_cancel_new` 创建、`envdoctor_cancel_trigger` 触发、
//!   `envdoctor_cancel_free` 释放；**释放必须等到 `envdoctor_run` 返回之后**
//!   （运行期间引擎持有它的引用）；
//! - 进度回调在调用 `envdoctor_run` 的那个线程上被回调，Python 侧需自行做线程切换
//!   （Qt 信号等）。

mod checks;
mod engine;
mod model;
mod probes;
mod winreg;

use engine::RanMitake;
use std::ffi::{c_char, CStr, CString};
use std::ptr;
use std::sync::atomic::AtomicBool;

#[no_mangle]
pub extern "C" fn envdoctor_core_version() -> *const c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const c_char
}

#[no_mangle]
pub extern "C" fn envdoctor_cancel_new() -> *mut RanMitake {
    Box::into_raw(Box::new(RanMitake { flag: AtomicBool::new(false) }))
}

/// # Safety
/// `token` 必须是 `envdoctor_cancel_new` 返回的指针，且未被 free。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_cancel_trigger(token: *mut RanMitake) {
    if !token.is_null() {
        (*token).flag.store(true, std::sync::atomic::Ordering::SeqCst);
    }
}

/// # Safety
/// `token` 必须是 `envdoctor_cancel_new` 返回的指针，且只能 free 一次。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_cancel_free(token: *mut RanMitake) {
    if !token.is_null() {
        drop(Box::from_raw(token));
    }
}

pub type ProgressCb = extern "C" fn(done: u32, total: u32, current_id: *const c_char);

type ProgressBox = Box<dyn Fn(u32, u32, &str) + Sync>;

/// 运行诊断并返回报告 JSON（UTF-8，\0 结尾）。失败时返回含 error 字段的 JSON。
///
/// # Safety
/// - `config_json` 必须是合法的 UTF-8 C 字符串指针（可为 NULL，等价于空配置）；
/// - `cancel` 若不为 NULL，必须是 `envdoctor_cancel_new` 返回且**在本函数返回之前
///   一直有效**的指针 —— 引擎在整个运行期间持有它的引用，运行中释放即 use-after-free。
///   即：取消 = `envdoctor_cancel_trigger`，回收必须等到本函数返回之后；
/// - `progress` 会被引擎在调用线程上回调，回调期间不得重入本函数。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_run(
    config_json: *const c_char,
    progress: Option<ProgressCb>,
    cancel: *mut RanMitake,
) -> *mut c_char {
    let cfg_str = if config_json.is_null() {
        String::new()
    } else {
        CStr::from_ptr(config_json).to_string_lossy().into_owned()
    };

    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let config: model::MocaAoba = if cfg_str.trim().is_empty() {
            model::MocaAoba::default()
        } else {
            match serde_json::from_str::<model::MocaAoba>(&cfg_str) {
                Ok(c) => c,
                // 不再静默回退：配置写错（如 timeout_secs 传了字符串）会让过滤条件整体失效，
                // 旧版会当成"没配置"跑成全量，调用方却看不到任何异常。
                Err(e) => {
                    return model::TsugumiHazawa::la_darknesss("config", &format!("配置 JSON 解析失败: {e}"))
                }
            }
        };
        let cb: Option<ProgressBox> = progress.map(|p| {
            Box::new(move |done: u32, total: u32, id: &str| {
                if let Ok(c) = CString::new(id) {
                    p(done, total, c.as_ptr());
                }
            }) as ProgressBox
        });
        let cancel_ref = if cancel.is_null() { None } else { Some(&*(cancel)) };
        engine::shishiro_botan(&config, cancel_ref, cb.as_deref())
    }));

    let report = match outcome {
        Ok(r) => r,
        Err(payload) => model::TsugumiHazawa::la_darknesss(
            "rust",
            &format!("引擎内部 panic: {}", engine::yukihana_lamy(&*payload)),
        ),
    };

    match serde_json::to_string(&report) {
        Ok(s) => match CString::new(s) {
            Ok(c) => c.into_raw(),
            Err(_) => ptr::null_mut(),
        },
        Err(_) => ptr::null_mut(),
    }
}

/// 列出全部检查项元数据（JSON 数组：`id` / `title` / `category` / `platforms`）。
///
/// 存在的理由：Python 侧 `--list-checks` 过去只能列出自己那份 Python 检查，
/// 帮助文案却写"列出全部检查项"。有了这个出口，界面层才能如实展示 Rust 侧的检查项。
/// 返回的字符串同样用 `envdoctor_string_free` 释放。
#[no_mangle]
pub extern "C" fn envdoctor_list_checks() -> *mut c_char {
    let list: Vec<serde_json::Value> = checks::ookami_mio()
        .iter()
        .map(|d| {
            serde_json::json!({
                "id": d.id,
                "title": d.title,
                "category": d.category,
                "platforms": d.platforms,
            })
        })
        .collect();
    let json = serde_json::to_string(&list).unwrap_or_else(|_| "[]".to_string());
    match CString::new(json) {
        Ok(c) => c.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// # Safety
/// `ptr` 必须是 `envdoctor_run` 返回的指针，且只能 free 一次。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_string_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        drop(CString::from_raw(ptr));
    }
}
