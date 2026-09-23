//! 底层探测工具：白名单工具表 + 带超时的子进程执行 + 字节解码。
//!
//! 安全设计：
//! - 可执行的程序集合封闭在 `Tool` 枚举中，每个变体的 `Command` 以字符串字面量
//!   构建，不存在任何运行时输入到命令名的连接点；
//! - 参数全部为编译期常量字面量；
//! - 所有子进程不走 shell 解释（netsh 的代码页切换通过 cmd 的参数拆分实现，
//!   各段均为字面量）；
//! - stdout/stderr 由独立线程排空，避免管道写满死锁；Windows 下附加
//!   CREATE_NO_WINDOW，GUI 调用时不弹控制台窗口。

use std::io::Read;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

/// 允许执行的工具白名单。
#[derive(Clone, Copy, PartialEq)]
pub enum Tool {
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

impl Tool {
    /// 每个工具的命令行在此以字面量构建（白名单的唯一入口）。
    pub fn command(self) -> Command {
        match self {
            Tool::Git => {
                let mut c = Command::new("git");
                c.args(["--version"]);
                c
            }
            Tool::Node => {
                let mut c = Command::new("node");
                c.args(["--version"]);
                c
            }
            Tool::Npm => {
                let mut c = Command::new("npm");
                c.args(["-v"]);
                c
            }
            Tool::Java => {
                let mut c = Command::new("java");
                c.args(["-version"]);
                c
            }
            Tool::Go => {
                let mut c = Command::new("go");
                c.args(["version"]);
                c
            }
            Tool::Rustc => {
                let mut c = Command::new("rustc");
                c.args(["--version"]);
                c
            }
            Tool::Cargo => {
                let mut c = Command::new("cargo");
                c.args(["--version"]);
                c
            }
            Tool::Gcc => {
                let mut c = Command::new("gcc");
                c.args(["--version"]);
                c
            }
            Tool::Gxx => {
                let mut c = Command::new("g++");
                c.args(["--version"]);
                c
            }
            Tool::Make => {
                let mut c = Command::new("make");
                c.args(["--version"]);
                c
            }
            Tool::DotNet => {
                let mut c = Command::new("dotnet");
                c.args(["--version"]);
                c
            }
            Tool::Python => {
                let mut c = Command::new("python");
                c.args(["--version"]);
                c
            }
            Tool::NetshState => {
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
            Tool::W32tmAliyun => {
                let mut c = Command::new("w32tm");
                c.args([
                    "/stripchart",
                    "/computer:ntp.aliyun.com",
                    "/samples:1",
                    "/dataonly",
                ]);
                c
            }
            Tool::PowershellGpu => {
                let mut c = Command::new("powershell");
                c.args([
                    "-NoProfile",
                    "-Command",
                    "Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name",
                ]);
                c
            }
            Tool::CurlIpify => {
                let mut c = Command::new("curl");
                c.args(["-s", "-m", "8", "https://api.ipify.org"]);
                c
            }
            Tool::Docker => {
                let mut c = Command::new("docker");
                c.args(["--version"]);
                c
            }
            Tool::DockerInfo => {
                let mut c = Command::new("docker");
                c.args(["info", "--format", "{{.ServerVersion}}"]);
                c
            }
            Tool::DockerImages => {
                let mut c = Command::new("docker");
                c.args(["images", "-q"]);
                c
            }
            Tool::DockerPs => {
                let mut c = Command::new("docker");
                c.args(["ps", "-q"]);
                c
            }
            Tool::Podman => {
                let mut c = Command::new("podman");
                c.args(["--version"]);
                c
            }
        }
    }

    /// 工具在 required 配置里的标识。
    pub fn id(self) -> &'static str {
        match self {
            Tool::Git => "git",
            Tool::Node => "node",
            Tool::Npm => "npm",
            Tool::Java => "java",
            Tool::Go => "go",
            Tool::Rustc => "rustc",
            Tool::Cargo => "cargo",
            Tool::Gcc => "gcc",
            Tool::Gxx => "g++",
            Tool::Make => "make",
            Tool::DotNet => "dotnet",
            Tool::Python => "python",
            _ => "",
        }
    }
}

pub fn decode_bytes(b: &[u8]) -> String {
    match String::from_utf8(b.to_vec()) {
        Ok(s) => s,
        Err(_) => String::from_utf8_lossy(b).into_owned(),
    }
}

pub fn to_wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

pub struct CmdOut {
    pub success: bool,
    pub stdout: String,
    pub stderr: String,
    pub timed_out: bool,
    pub not_found: bool,
}

impl CmdOut {
    pub fn combined(&self) -> String {
        let mut s = self.stdout.trim().to_string();
        if s.is_empty() {
            s = self.stderr.trim().to_string();
        }
        s
    }

    pub fn first_line(&self) -> String {
        self.combined().lines().next().unwrap_or("").trim().to_string()
    }
}

/// 执行白名单工具并限时回收。stdout/stderr 由独立线程排空，避免管道写满死锁。
pub fn run_tool(tool: Tool, timeout: Duration) -> CmdOut {
    run_with_timeout(tool.command(), timeout)
}

/// 对已构建好的 Command 限时执行（Command 由白名单表以字面量构建）。
pub fn run_with_timeout(mut cmd: Command, timeout: Duration) -> CmdOut {
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
            return CmdOut {
                success: false,
                stdout: String::new(),
                stderr: format!("{e}"),
                timed_out: false,
                not_found,
            };
        }
    };

    let mut out_pipe = child.stdout.take().expect("stdout 已声明 piped");
    let mut err_pipe = child.stderr.take().expect("stderr 已声明 piped");
    let t_out = std::thread::spawn(move || {
        let mut b = Vec::new();
        let _ = out_pipe.read_to_end(&mut b);
        decode_bytes(&b)
    });
    let t_err = std::thread::spawn(move || {
        let mut b = Vec::new();
        let _ = err_pipe.read_to_end(&mut b);
        decode_bytes(&b)
    });

    let deadline = Instant::now() + timeout;
    let mut timed_out = false;
    let status = loop {
        match child.try_wait() {
            Ok(Some(st)) => break Some(st),
            Ok(None) => {
                if Instant::now() >= deadline {
                    let _ = child.kill();
                    let _ = child.wait();
                    timed_out = true;
                    break None;
                }
                std::thread::sleep(Duration::from_millis(40));
            }
            Err(e) => {
                return CmdOut {
                    success: false,
                    stdout: String::new(),
                    stderr: format!("{e}"),
                    timed_out: false,
                    not_found: false,
                };
            }
        }
    };

    let stdout = t_out.join().unwrap_or_default();
    let stderr = t_err.join().unwrap_or_default();
    match status {
        Some(st) => CmdOut { success: st.success(), stdout, stderr, timed_out, not_found: false },
        None => CmdOut { success: false, stdout, stderr, timed_out, not_found: false },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_utf8_passthrough() {
        assert_eq!(decode_bytes("hello 世界".as_bytes()), "hello 世界");
    }

    #[test]
    fn decode_invalid_bytes_is_lossy_not_panic() {
        let out = decode_bytes(&[0xff, 0xfe, 0x41]);
        assert!(out.contains('A'));
    }

    #[test]
    fn to_wide_is_null_terminated() {
        let w = to_wide("C:\\");
        assert_eq!(w.last(), Some(&0));
        assert_eq!(w.len(), 4);
    }
}
