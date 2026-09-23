# Environment Check · Revamp

旧版「环境诊断工具」的Revamp版。原件（[`../Original File.py`](../Original%20File.py)）作为反面教材保留，
主要构成：**Rust 核心（C ABI）+ Typer CLI + Textual TUI + PySide6 GUI**。

## 架构

```
Revamp/
├── core/                 # Rust cdylib（envdoctor_core.dll），C ABI
│   └── src/
│       ├── lib.rs        #   ABI 出口：run / cancel / progress / string_free
│       ├── engine.rs     #   并发调度、整体超时、取消令牌、进度回调
│       ├── model.rs      #   Config / Outcome / Report（JSON 交换格式）
│       ├── probes.rs     #   白名单工具表 + 限时子进程 + Win32 FFI
│       └── checks/       #   hardware / toolchains / network / containers / databases
├── app/                  # Python 包（envdoctor）
│   └── src/envdoctor/
│       ├── binding.py    #   ctypes 绑定（DLL 发现、进度回调、取消令牌）
│       ├── pychecks.py   #   Python 生态检查（必须在 Python 进程内执行的部分）
│       ├── merge.py      #   Rust + Python 结果合并、诊断结论
│       ├── cli.py        #   Typer CLI（默认折叠为分类摘要）
│       ├── tui.py        #   Textual TUI（可展开收缩诊断树）
│       └── gui.py        #   PySide6 三级可展开收缩树
├── tests/                # Python 层单元测试（unittest）
└── start.bat             # 一键环境检查 + 补齐 + 启动器（GBK 编码）
```

## 快速开始（Windows）

双击 `start.bat`：它会依次检查系统 Python、虚拟环境、Python 依赖、Rust 核心 DLL，
每一步缺失时询问是否自动补齐，最后选择 GUI / TUI / CLI 启动。

## 手动构建

```bash
# 1) Rust 核心（需要 rustup；国内可用 rsproxy/TUNA 镜像）
cd core
cargo build --release
# 产物: core/target/release/envdoctor_core.dll

# 2) Python 包
cd ..
python -m venv .venv
.venv/Scripts/python -m pip install -e "app[gui,tui]"
```

## 使用

```bash
envdoctor                        # 全量诊断，分类折叠显示
envdoctor --list-checks          # 列出全部检查项
envdoctor run -E                 # 展开全部明细
envdoctor run -c python -c network -e network   # 选类别并展开
envdoctor run --require git,node # 声明必备工具，缺失记 FAIL
envdoctor run --json report.json --txt report.md
envdoctor run --net-full         # 启用公网 IP 检查（默认关闭，见隐私）
envdoctor tui                    # Textual 终端界面（R 运行 / C 取消 / E 展开 / Q 退出）
envdoctor gui                    # PySide6 图形界面
```

退出码：存在 FAIL 级问题时为 1（可直接用于 CI/脚本）。

## FFI 约定（Rust ↔ Python）

- 报告 JSON 由 Rust 以 `CString::into_raw` 移交，Python 侧解析后**必须**调用
  `envdoctor_string_free` 归还（`CString::from_raw` 配对回收），否则泄漏；
- 取消令牌 `envdoctor_cancel_new / trigger / free` 同样严格配对；
- `envdoctor_run` 内部以 `catch_unwind` 隔离 panic：不会跨 FFI 展开，失败转为报告的
  `error` 字段，Python 侧可见；
- `CDLL` 调用期间 ctypes 自动释放 GIL（GUI 不卡顿）；进度回调由 ctypes 在引擎线程
  进入 Python 前自动获取 GIL，界面层负责再切回自己的主线程（Qt 信号 / call_from_thread）；
- `#[no_mangle]` + `crate-type = ["cdylib"]` 保证符号可被 ctypes 按原名找到；
  Python 侧所有导出函数均显式声明 `argtypes` / `restype`。

## 测试

```bash
cargo test --release      # Rust 核心（模型/引擎/注册表/探测）
.venv/Scripts/python -m unittest discover -s tests -v   # Python 层
```

## 隐私

- 默认不发起任何包含设备信息的外发请求；公网 IP 查询需显式 `--net-full`；
- 报告中不包含用户名、序列化凭据（代理地址凭据自动 `***` 打码）；
- 导出文件内容由使用者全权控制（`--json` / `--txt` / GUI 导出按钮）。

## 许可

MIT（与本仓库 LICENSE 一致）。
