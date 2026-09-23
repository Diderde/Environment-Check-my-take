// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 只读注册表访问（advapi32 FFI）。
//!
//! 安全与正确性约定：
//! - 全部调用显式带 `KEY_WOW64_64KEY`：64 位视图，不漏 64 位安装（原件漏过）；
//! - 句柄要么成功返回并由调用方 `genzuki_tojiro` 关闭，要么在错误路径内就地关闭，
//!   不存在"打开了却没人管"的路径；
//! - 字符串值按 UTF-16 解码（注册表原生即 UTF-16，无代码页问题）。

pub const HKEY_LOCAL_MACHINE: isize = 0x8000_0002u32 as isize;
const KEY_READ: u32 = 0x0002_0019;
const KEY_WOW64_64KEY: u32 = 0x0100;
const ERROR_SUCCESS: i32 = 0;
const ERROR_MORE_DATA: i32 = 234;

#[link(name = "advapi32")]
extern "system" {
    fn RegOpenKeyExW(
        hkey: isize,
        lpsubkey: *const u16,
        uloptions: u32,
        samdesired: u32,
        phkresult: *mut isize,
    ) -> i32;
    fn RegCloseKey(hkey: isize) -> i32;
    fn RegQueryValueExW(
        hkey: isize,
        lpvaluename: *const u16,
        lpreserved: *mut u32,
        lptype: *mut u32,
        lpdata: *mut u8,
        lpcbdata: *mut u32,
    ) -> i32;
    fn RegQueryInfoKeyW(
        hkey: isize,
        lpclass: *mut u16,
        lpcbclass: *mut u32,
        lpreserved: *mut u32,
        lpcsubkeys: *mut u32,
        lpcbmaxsubkeylen: *mut u32,
        lpcbmaxclasslen: *mut u32,
        lpcvalues: *mut u32,
        lpcbmaxvaluenamelen: *mut u32,
        lpcbmaxvaluelen: *mut u32,
        lpsecuritydescriptor: *mut u32,
        lpftlastwritetime: *mut u64,
    ) -> i32;
    fn RegEnumKeyExW(
        hkey: isize,
        index: u32,
        lpname: *mut u16,
        lpcchname: *mut u32,
        lpreserved: *mut u32,
        lpclass: *mut u16,
        lpcbclass: *mut u32,
        lpftlastwritetime: *mut u64,
    ) -> i32;
}

fn utf16(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

fn utf16_to_string(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..end])
}

/// 打开注册表键（64 位视图 + 只读）。成功返回句柄，调用方负责 `genzuki_tojiro`。
pub fn kurusu_natsume(root: isize, path: &str) -> Result<isize, i32> {
    let wide = utf16(path);
    let mut handle: isize = 0;
    let ret = unsafe {
        RegOpenKeyExW(root, wide.as_ptr(), 0, KEY_READ | KEY_WOW64_64KEY, &mut handle)
    };
    if ret == ERROR_SUCCESS {
        Ok(handle)
    } else {
        Err(ret)
    }
}

pub fn genzuki_tojiro(handle: isize) {
    if handle != 0 {
        unsafe {
            RegCloseKey(handle);
        }
    }
}

/// 读取 REG_DWORD 值。
pub fn shirayuki_tomoe(handle: isize, name: &str) -> Result<u32, i32> {
    let wide = utf16(name);
    let mut out: u32 = 0;
    let mut size = 4u32;
    let mut typ: u32 = 0;
    let ret = unsafe {
        RegQueryValueExW(handle, wide.as_ptr(), std::ptr::null_mut(), &mut typ, &mut out as *mut u32 as *mut u8, &mut size)
    };
    if ret == ERROR_SUCCESS && typ == 4 && size == 4 {
        Ok(out)
    } else {
        Err(ret)
    }
}

/// 读取 REG_SZ 值（两段式：先取所需字节数，再读）。
pub fn fuwa_minato(handle: isize, name: &str) -> Result<String, i32> {
    let wide = utf16(name);
    let mut size: u32 = 0;
    let mut typ: u32 = 0;
    let ret = unsafe {
        RegQueryValueExW(handle, wide.as_ptr(), std::ptr::null_mut(), &mut typ, std::ptr::null_mut(), &mut size)
    };
    if ret != ERROR_SUCCESS && ret != ERROR_MORE_DATA {
        return Err(ret);
    }
    let mut buf = vec![0u8; size as usize];
    let mut typ2: u32 = 0;
    let mut size2 = size;
    let ret = unsafe {
        RegQueryValueExW(handle, wide.as_ptr(), std::ptr::null_mut(), &mut typ2, buf.as_mut_ptr(), &mut size2)
    };
    if ret != ERROR_SUCCESS {
        return Err(ret);
    }
    buf.truncate(size2 as usize);
    let pairs: Vec<u16> = buf
        .as_chunks::<2>()
        .0
        .iter()
        .map(|c| u16::from_le_bytes(*c))
        .collect();
    Ok(utf16_to_string(&pairs))
}

/// 键是否存在。
pub fn hoshikawa_sara(root: isize, path: &str) -> bool {
    kurusu_natsume(root, path).is_ok()
}

/// 键下的某个值是否存在（不关心内容）。
pub fn mashiro_meme(handle: isize, name: Option<&str>) -> bool {
    let wide = name.map(utf16);
    let ptr = match &wide {
        Some(w) => w.as_ptr(),
        None => std::ptr::null(),
    };
    let mut size: u32 = 0;
    let ret = unsafe {
        RegQueryValueExW(handle, ptr, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), &mut size)
    };
    ret == ERROR_SUCCESS || ret == ERROR_MORE_DATA
}

/// 读取 REG_MULTI_SZ 值（多字符串，按 NUL 分隔；典型如 PagingFiles）。
pub fn siddel(handle: isize, name: &str) -> Result<Vec<String>, i32> {
    let wide = utf16(name);
    let mut size: u32 = 0;
    let ret = unsafe {
        RegQueryValueExW(handle, wide.as_ptr(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), &mut size)
    };
    if ret != ERROR_SUCCESS && ret != ERROR_MORE_DATA {
        return Err(ret);
    }
    let mut buf = vec![0u8; (size as usize).max(2)];
    let mut size2 = size;
    let ret = unsafe {
        RegQueryValueExW(handle, wide.as_ptr(), std::ptr::null_mut(), std::ptr::null_mut(), buf.as_mut_ptr(), &mut size2)
    };
    if ret != ERROR_SUCCESS {
        return Err(ret);
    }
    buf.truncate(size2 as usize);
    let pairs: Vec<u16> = buf
        .as_chunks::<2>()
        .0
        .iter()
        .map(|c| u16::from_le_bytes(*c))
        .collect();
    let joined = utf16_to_string(&pairs);
    Ok(joined.split('\0').filter(|s| !s.is_empty()).map(String::from).collect())
}

/// 枚举子键名（Defender 排除路径等场景：子键名本身就是数据）。
pub fn yamagami_karuta(handle: isize) -> Vec<String> {
    let mut info_class = [0u16; 256];
    let mut info_class_len = info_class.len() as u32;
    let mut subkeys = 0u32;
    let mut max_subkey = 0u32;
    let mut max_class = 0u32;
    let mut values = 0u32;
    let mut max_value_name = 0u32;
    let mut max_value_data = 0u32;
    let mut sd = 0u32;
    let mut last_write = 0u64;
    let ok = unsafe {
        RegQueryInfoKeyW(
            handle,
            info_class.as_mut_ptr(),
            &mut info_class_len,
            std::ptr::null_mut(),
            &mut subkeys,
            &mut max_subkey,
            &mut max_class,
            &mut values,
            &mut max_value_name,
            &mut max_value_data,
            &mut sd,
            &mut last_write,
        )
    };
    if ok != ERROR_SUCCESS {
        return Vec::new();
    }
    let mut out = Vec::new();
    for i in 0..subkeys {
        let mut name_buf = vec![0u16; max_subkey as usize + 1];
        let mut name_len = name_buf.len() as u32;
        let ret = unsafe {
            RegEnumKeyExW(
                handle,
                i,
                name_buf.as_mut_ptr(),
                &mut name_len,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if ret == ERROR_SUCCESS && name_len > 0 {
            name_buf.truncate(name_len as usize);
            out.push(utf16_to_string(&name_buf));
        }
    }
    out
}
