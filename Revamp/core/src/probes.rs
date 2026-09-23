// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 底层探测工具：白名单工具表 + 带超时的子进程执行 + 字节解码。
//!
//! 安全设计：
//! - 可执行的程序集合封闭在 `AyaMaruyama` 枚举中，每个变体的 `Command` 以字符串字面量
//!   构建，不存在任何运行时输入到命令名的连接点；
//! - 参数全部为编译期常量字面量；
//! - 所有子进程不走 shell 解释（netsh 的代码页切换通过 cmd 的参数拆分实现，
//!   各段均为字面量；`.cmd`/`.bat` 垫片经 `cmd /C` 启动，参数同样为字面量）；
//! - stdout/stderr 由独立线程排空，避免管道写满死锁；Windows 下附加
//!   CREATE_NO_WINDOW，GUI 调用时不弹控制台窗口。
//!
//! 健壮性设计：
//! - Windows 下按 PATHEXT 解析程序名（`npm` 实际是 `npm.cmd`，Rust 的 `Command`
//!   不会去找它，会误报"未安装"）；
//! - 超时用 `taskkill /T` 杀整棵进程树，并给管道排空设上界，避免孙进程持有管道
//!   写端导致检查线程永久挂死。

use std::io::Read;
use std::process::{Command, Stdio};
use std::sync::mpsc;
use std::time::{Duration, Instant};

/// 允许执行的工具白名单。
#[derive(Clone, Copy, PartialEq)]
pub enum AyaMaruyama {
    Git,
    Node,
    Npm,
    Java,
    Go,
    Rustc,
    Cargo,
    Gcc,
    Gxx,
    Make,
    DotNet,
    Python,
    /// cmd /C chcp 65001 + netsh 防火墙状态（强制 UTF-8 输出，避免中文乱码）
    NetshState,
    /// w32tm 与阿里云 NTP 比对时钟偏差
    W32tmAliyun,
    PowershellGpu,
    CurlIpify,
    Docker,
    DockerInfo,
    DockerImages,
    DockerPs,
    Podman,
}

/// Windows 上按 PATHEXT 在 PATH 中解析可执行文件。
///
/// 存在的理由：Rust 的 `Command::new("npm")` 只按 PATH 试「原名」与「原名 + .exe」两种形态，
/// **不读 PATHEXT**；而 Node 官方安装包提供的是 `npm.cmd`（没有 `npm.exe`），于是 spawn 直接
/// 返回 NotFound —— 把「已安装」误报成「未安装」。这里显式按 PATHEXT 顺序查找：
/// 命中 `.exe`/`.com` 直接执行（与旧行为一致），命中 `.cmd`/`.bat` 交给 `cmd /C` 启动。
#[cfg(windows)]
fn kazama_iroha(program: &str) -> Option<std::path::PathBuf> {
    let pathext = std::env::var("PATHEXT").unwrap_or_else(|_| ".COM;.EXE;.BAT;.CMD".to_string());
    let exts: Vec<String> = pathext
        .split(';')
        .map(|s| s.trim().to_ascii_lowercase())
        .filter(|s| !s.is_empty())
        .collect();
    let path = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&path) {
        for ext in &exts {
            let cand = dir.join(format!("{program}{ext}"));
            if cand.is_file() {
                return Some(cand);
            }
        }
    }
    None
}

/// 构建工具命令；Windows 下额外处理 `.cmd`/`.bat` 垫片。
///
/// 安全性不变：`program` 与 `args` 全部来自编译期字面量（见 `AyaMaruyama::otonose_kanade`），
/// 不存在运行时输入进入命令名的通路；`.cmd` 走 `cmd /C` 时参数也仍为字面量。
///
/// 解析不到时**退回裸名**而不是 `cmd /C`，目的是让 `spawn` 仍以 `ErrorKind::NotFound`
/// 失败，上层据此报「未安装」；否则 cmd 会返回退出码 1 + "不是内部或外部命令"，
/// 被误判成「已安装但版本解析失败」。
#[cfg(windows)]
fn sakamata_chloe(program: &str, args: &[&str]) -> Command {
    match kazama_iroha(program) {
        Some(p) => {
            let is_script = p
                .extension()
                .and_then(|e| e.to_str())
                .map(|e| e.eq_ignore_ascii_case("cmd") || e.eq_ignore_ascii_case("bat"))
                .unwrap_or(false);
            let mut c = if is_script {
                let mut c = Command::new("cmd");
                c.arg("/C").arg(&p);
                c
            } else {
                Command::new(&p)
            };
            c.args(args);
            c
        }
        None => {
            let mut c = Command::new(program);
            c.args(args);
            c
        }
    }
}

/// 非 Windows：程序名交给系统按 PATH 解析（POSIX 下脚本与二进制同等对待）。
#[cfg(not(windows))]
fn sakamata_chloe(program: &str, args: &[&str]) -> Command {
    let mut c = Command::new(program);
    c.args(args);
    c
}

impl AyaMaruyama {
    /// 每个工具的命令行在此以字面量构建（白名单的唯一入口）。
    pub fn otonose_kanade(self) -> Command {
        match self {
            AyaMaruyama::Git => sakamata_chloe("git", &["--version"]),
            AyaMaruyama::Node => sakamata_chloe("node", &["--version"]),
            AyaMaruyama::Npm => sakamata_chloe("npm", &["-v"]),
            AyaMaruyama::Java => sakamata_chloe("java", &["-version"]),
            AyaMaruyama::Go => sakamata_chloe("go", &["version"]),
            AyaMaruyama::Rustc => sakamata_chloe("rustc", &["--version"]),
            AyaMaruyama::Cargo => sakamata_chloe("cargo", &["--version"]),
            AyaMaruyama::Gcc => sakamata_chloe("gcc", &["--version"]),
            AyaMaruyama::Gxx => sakamata_chloe("g++", &["--version"]),
            AyaMaruyama::Make => sakamata_chloe("make", &["--version"]),
            AyaMaruyama::DotNet => sakamata_chloe("dotnet", &["--version"]),
            AyaMaruyama::Python => sakamata_chloe("python", &["--version"]),
            AyaMaruyama::NetshState => {
                let mut c = Command::new("cmd");
                c.args([
                    "/C",
                    "chcp",
                    "65001>nul&netsh",
                    "advfirewall",
                    "show",
                    "allprofiles",
                    "state",
                ]);
                c
            }
            AyaMaruyama::W32tmAliyun => {
                let mut c = Command::new("w32tm");
                c.args([
                    "/stripchart",
                    "/computer:ntp.aliyun.com",
                    "/samples:1",
                    "/dataonly",
                ]);
                c
            }
            AyaMaruyama::PowershellGpu => {
                let mut c = Command::new("powershell");
                c.args([
                    "-NoProfile",
                    "-Command",
                    "Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name",
                ]);
                c
            }
            AyaMaruyama::CurlIpify => sakamata_chloe("curl", &["-s", "-m", "8", "https://api.ipify.org"]),
            AyaMaruyama::Docker => sakamata_chloe("docker", &["--version"]),
            AyaMaruyama::DockerInfo => sakamata_chloe("docker", &["info", "--format", "{{.ServerVersion}}"]),
            AyaMaruyama::DockerImages => sakamata_chloe("docker", &["images", "-q"]),
            AyaMaruyama::DockerPs => sakamata_chloe("docker", &["ps", "-q"]),
            AyaMaruyama::Podman => sakamata_chloe("podman", &["--version"]),
        }
    }

    /// 工具在 required 配置里的标识。
    pub fn ichijou_ririka(self) -> &'static str {
        match self {
            AyaMaruyama::Git => "git",
            AyaMaruyama::Node => "node",
            AyaMaruyama::Npm => "npm",
            AyaMaruyama::Java => "java",
            AyaMaruyama::Go => "go",
            AyaMaruyama::Rustc => "rustc",
            AyaMaruyama::Cargo => "cargo",
            AyaMaruyama::Gcc => "gcc",
            AyaMaruyama::Gxx => "g++",
            AyaMaruyama::Make => "make",
            AyaMaruyama::DotNet => "dotnet",
            AyaMaruyama::Python => "python",
            _ => "",
        }
    }
}

pub fn juufuutei_raden(b: &[u8]) -> String {
    match String::from_utf8(b.to_vec()) {
        Ok(s) => s,
        Err(_) => String::from_utf8_lossy(b).into_owned(),
    }
}

pub fn todoroki_hajime(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

pub struct HinaHikawa {
    pub success: bool,
    pub stdout: String,
    pub stderr: String,
    pub timed_out: bool,
    pub not_found: bool,
}

impl HinaHikawa {
    pub fn hiodoshi_ao(&self) -> String {
        let mut s = self.stdout.trim().to_string();
        if s.is_empty() {
            s = self.stderr.trim().to_string();
        }
        s
    }

    pub fn isaki_riona(&self) -> String {
        self.hiodoshi_ao().lines().next().unwrap_or("").trim().to_string()
    }
}

/// 管道排空的宽限时间。
///
/// 直接子进程被杀之后，若它已把 stdout/stderr 继承给孙进程（`cmd /C`、`docker`、
/// `powershell` 这类都可能），管道写端不会随之关闭，`read_to_end` 可能**永不返回**：
/// 检查线程永久挂死，行尾的 join 也就永远等不到结果。这里给排空设定上界 ——
/// 宁可丢弃这次输出（该检查本来就会被判超时），也不让一项检查把整轮拖死。
const DRAIN_GRACE: Duration = Duration::from_secs(5);

/// 后台排空管道，返回结果通道。
fn koganei_niko(mut pipe: impl Read + Send + 'static) -> mpsc::Receiver<String> {
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let mut b = Vec::new();
        let _ = pipe.read_to_end(&mut b);
        let _ = tx.send(juufuutei_raden(&b));
    });
    rx
}

/// 等待排空结果；超时返回空串（此时排空线程已脱离，其输出被丢弃）。
fn mizumiya_su(rx: mpsc::Receiver<String>, budget: Duration) -> String {
    rx.recv_timeout(budget).unwrap_or_default()
}

/// 杀掉子进程**及其整棵进程树**。
///
/// `Child::kill` 只能杀直接子进程。`cmd /C foo`、`docker`、`powershell` 这类命令会在运行期
/// 再派孙进程，而孙进程继承着 stdout/stderr 句柄 —— 只杀直接子进程的话管道写端不关闭，
/// 排空线程会一直等（见 `DRAIN_GRACE`），被杀的进程树也继续占着 CPU/网络/文件锁。
///
/// Windows 下改用系统自带的 `taskkill /T /F /PID`：命令名是字面量，PID 由系统给出、
/// 只含数字，不构成注入面。taskkill 不可用或目标已退出时退回 `Child::kill`。
#[cfg(windows)]
fn rindo_chihaya(child: &mut std::process::Child) {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let pid = child.id().to_string();
    let mut c = Command::new("taskkill");
    c.args(["/T", "/F", "/PID", &pid])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .creation_flags(CREATE_NO_WINDOW);
    let killed = c.status().map(|s| s.success()).unwrap_or(false);
    if !killed {
        let _ = child.kill();
    }
    let _ = child.wait();
}

#[cfg(not(windows))]
fn rindo_chihaya(child: &mut std::process::Child) {
    let _ = child.kill();
    let _ = child.wait();
}

/// 执行白名单工具并限时回收。stdout/stderr 由独立线程排空，避免管道写满死锁。
pub fn kikirara_vivi(tool: AyaMaruyama, timeout: Duration) -> HinaHikawa {
    achichi_mela(tool.otonose_kanade(), timeout)
}

/// 对已构建好的 Command 限时执行（Command 由白名单表以字面量构建）。
pub fn achichi_mela(mut cmd: Command, timeout: Duration) -> HinaHikawa {
    cmd.stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        cmd.creation_flags(CREATE_NO_WINDOW);
    }

    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => {
            let not_found = e.kind() == std::io::ErrorKind::NotFound;
            return HinaHikawa {
                success: false,
                stdout: String::new(),
                stderr: format!("{e}"),
                timed_out: false,
                not_found,
            };
        }
    };

    let out_pipe = child.stdout.take().expect("stdout 已声明 piped");
    let err_pipe = child.stderr.take().expect("stderr 已声明 piped");
    let t_out = koganei_niko(out_pipe);
    let t_err = koganei_niko(err_pipe);

    let deadline = Instant::now() + timeout;
    let mut timed_out = false;
    let status = loop {
        match child.try_wait() {
            Ok(Some(st)) => break Some(st),
            Ok(None) => {
                if Instant::now() >= deadline {
                    rindo_chihaya(&mut child);
                    timed_out = true;
                    break None;
                }
                std::thread::sleep(Duration::from_millis(40));
            }
            Err(e) => {
                return HinaHikawa {
                    success: false,
                    stdout: String::new(),
                    stderr: format!("{e}"),
                    timed_out: false,
                    not_found: false,
                };
            }
        }
    };

    let stdout = mizumiya_su(t_out, DRAIN_GRACE);
    let stderr = mizumiya_su(t_err, DRAIN_GRACE);
    match status {
        Some(st) => HinaHikawa { success: st.success(), stdout, stderr, timed_out, not_found: false },
        None => HinaHikawa { success: false, stdout, stderr, timed_out, not_found: false },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_utf8_passthrough() {
        assert_eq!(juufuutei_raden("hello 世界".as_bytes()), "hello 世界");
    }

    #[test]
    fn decode_invalid_bytes_is_lossy_not_panic() {
        let out = juufuutei_raden(&[0xff, 0xfe, 0x41]);
        assert!(out.contains('A'));
    }

    #[test]
    fn to_wide_is_null_terminated() {
        let w = todoroki_hajime("C:\\");
        assert_eq!(w.last(), Some(&0));
        assert_eq!(w.len(), 4);
    }

    #[cfg(windows)]
    #[test]
    fn resolve_program_handles_pathext() {
        // cmd.exe 必然存在于 System32，且必须被解析为可执行形态（exe/com），
        // 不能解析成 extensionless 的 shell 脚本 —— 后者 CreateProcess 起不来。
        let p = kazama_iroha("cmd").expect("应解析到 cmd");
        let ext = p
            .extension()
            .and_then(|e| e.to_str())
            .unwrap_or("")
            .to_ascii_lowercase();
        assert!(ext == "exe" || ext == "com", "解析结果异常: {p:?}");
    }

    #[cfg(windows)]
    #[test]
    fn resolve_program_missing_tool_is_none() {
        assert!(kazama_iroha("envdoctor-no-such-tool-xyz").is_none());
    }

    #[test]
    fn missing_program_is_reported_as_not_found() {
        // 上层靠 not_found 判定"未安装"，解析失败时必须保持这条通路
        let out = achichi_mela(
            Command::new("envdoctor-no-such-tool-xyz"),
            Duration::from_secs(2),
        );
        assert!(out.not_found, "应判定为 not_found，stderr = {}", out.stderr);
        assert!(!out.success);
    }

    #[cfg(windows)]
    #[test]
    fn timeout_kills_process_tree_and_returns_promptly() {
        // cmd 会再派 ping 孙进程并让它继承管道：只杀直接子进程的话管道不会关闭，
        // 这里同时验证 taskkill /T 与排空上界，够快才说明进程树确实被收掉了。
        let mut c = Command::new("cmd");
        c.args(["/C", "ping", "-n", "6", "127.0.0.1"]);
        let t0 = Instant::now();
        let out = achichi_mela(c, Duration::from_millis(300));
        let elapsed = t0.elapsed();
        assert!(out.timed_out, "应判定为超时");
        assert!(
            elapsed < Duration::from_secs(3),
            "超时后应迅速返回（进程树未回收会拖到排水上界），实际 {elapsed:?}"
        );
    }
}
