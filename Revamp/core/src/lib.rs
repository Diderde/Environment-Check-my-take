//! C ABI 出口：由 Python 侧通过 ctypes 调用。
//!
//! 约定：
//! - 返回的 JSON 字符串用 `envdoctor_string_free` 释放；
//! - 取消令牌用 `envdoctor_cancel_new` 创建、`envdoctor_cancel_trigger` 触发、
//!   `envdoctor_cancel_free` 释放；
//! - 进度回调在引擎工作线程上被调用，Python 侧需自行做线程切换（Qt 信号等）。

mod checks;
mod engine;
mod model;
mod probes;

use engine::CancelToken;
use std::ffi::{c_char, CStr, CString};
use std::ptr;
use std::sync::atomic::AtomicBool;

#[no_mangle]
pub extern "C" fn envdoctor_core_version() -> *const c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const c_char
}

#[no_mangle]
pub extern "C" fn envdoctor_cancel_new() -> *mut CancelToken {
    Box::into_raw(Box::new(CancelToken { flag: AtomicBool::new(false) }))
}

/// # Safety
/// `token` 必须是 `envdoctor_cancel_new` 返回的指针，且未被 free。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_cancel_trigger(token: *mut CancelToken) {
    if !token.is_null() {
        (*token).flag.store(true, std::sync::atomic::Ordering::SeqCst);
    }
}

/// # Safety
/// `token` 必须是 `envdoctor_cancel_new` 返回的指针，且只能 free 一次。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_cancel_free(token: *mut CancelToken) {
    if !token.is_null() {
        drop(Box::from_raw(token));
    }
}

pub type ProgressCb = extern "C" fn(done: u32, total: u32, current_id: *const c_char);

type ProgressBox = Box<dyn Fn(u32, u32, &str) + Sync>;

/// 运行诊断并返回报告 JSON（UTF-8，\0 结尾）。失败时返回含 error 字段的 JSON。
///
/// # Safety
/// `config_json` 必须是合法的 UTF-8 C 字符串指针。
#[no_mangle]
pub unsafe extern "C" fn envdoctor_run(
    config_json: *const c_char,
    progress: Option<ProgressCb>,
    cancel: *mut CancelToken,
) -> *mut c_char {
    let cfg_str = if config_json.is_null() {
        String::new()
    } else {
        CStr::from_ptr(config_json).to_string_lossy().into_owned()
    };

    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let config: model::Config = if cfg_str.trim().is_empty() {
            model::Config::default()
        } else {
            serde_json::from_str(&cfg_str).unwrap_or_default()
        };
        let cb: Option<ProgressBox> = progress.map(|p| {
            Box::new(move |done: u32, total: u32, id: &str| {
                if let Ok(c) = CString::new(id) {
                    p(done, total, c.as_ptr());
                }
            }) as ProgressBox
        });
        let cancel_ref = if cancel.is_null() { None } else { Some(&*(cancel)) };
        engine::run(&config, cancel_ref, cb.as_deref())
    }));

    let report = match outcome {
        Ok(r) => r,
        Err(_) => model::Report::error("rust", "引擎内部 panic"),
    };

    match serde_json::to_string(&report) {
        Ok(s) => match CString::new(s) {
            Ok(c) => c.into_raw(),
            Err(_) => ptr::null_mut(),
        },
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
