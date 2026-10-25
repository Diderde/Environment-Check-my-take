# Third-Party Notices / 第三方许可声明

本项目自身代码采用 MIT 许可证（见 [`LICENSE`](./LICENSE)）。

以下为仓库所引用的全部第三方组件及其许可证。本仓库不修改、不重新分发这些组件的
源代码或二进制；此处随附许可证文本以满足归属要求。

## Python 依赖（Revamp/app）

直接依赖（声明于 [`Revamp/app/pyproject.toml`](./Revamp/app/pyproject.toml)）：

| 包 | 引入方式 | 许可证 | 上游 |
|---|---|---|---|
| PySide6 ≥ 6.6 | `gui` extra | LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only（另有 Qt 商业许可可选） | [qt.io](https://www.qt.io/) · [Qt for Python](https://wiki.qt.io/Qt_for_Python) |
| Typer ≥ 0.12 | 必需依赖 | MIT | [fastapi/typer](https://github.com/fastapi/typer) |
| Textual ≥ 1.0 | `tui` extra | MIT | [Textualize/textual](https://github.com/Textualize/textual) |

传递依赖（由 pip 解析，版本未锁定，随安装版本可能变化）：

| 包 | 经由 | 许可证 |
|---|---|---|
| typer-slim | typer | MIT |
| rich *（被 `tui.py` 直接 import）* | textual | MIT |
| click | typer | BSD-3-Clause |
| typing_extensions | typer / textual | PSF-2.0 |
| markdown-it-py | rich | MIT |
| mdurl | markdown-it-py | MIT |
| Pygments | rich | BSD-2-Clause |

> 本仓库以 **GNU LGPL-3.0** 形式引用 PySide6。其余依赖均为宽松许可证
> （MIT / BSD / PSF / Apache-2.0 / Unlicense）。

## Rust 依赖（Revamp/core）

以下 crate 均编译进 `envdoctor_core.dll`，版本按 `Revamp/core/Cargo.lock` 锁定
（许可证字段经 crates.io 核实，2026-10）：

| crate | 版本 | 直接/传递 | 许可证 | 上游 |
|---|---|---|---|---|
| serde | 1.0.229 | 直接 | MIT OR Apache-2.0 | [serde-rs/serde](https://github.com/serde-rs/serde) |
| serde_json | 1.0.151 | 直接 | MIT OR Apache-2.0 | [serde-rs/json](https://github.com/serde-rs/json) |
| serde_core | 1.0.229 | 传递 | MIT OR Apache-2.0 | [serde-rs/serde](https://github.com/serde-rs/serde) |
| serde_derive | 1.0.229 | 传递 | MIT OR Apache-2.0 | [serde-rs/serde](https://github.com/serde-rs/serde) |
| proc-macro2 | 1.0.107 | 传递 | MIT OR Apache-2.0 | [dtolnay/proc-macro2](https://github.com/dtolnay/proc-macro2) |
| quote | 1.0.47 | 传递 | MIT OR Apache-2.0 | [dtolnay/quote](https://github.com/dtolnay/quote) |
| syn | 3.0.6 | 传递 | MIT OR Apache-2.0 | [dtolnay/syn](https://github.com/dtolnay/syn) |
| itoa | 1.0.18 | 传递 | MIT OR Apache-2.0 | [dtolnay/itoa](https://github.com/dtolnay/itoa) |
| memchr | 2.8.3 | 传递 | Unlicense OR MIT | [BurntSushi/memchr](https://github.com/BurntSushi/memchr) |
| unicode-ident | 1.0.26 | 传递 | (MIT OR Apache-2.0) AND Unicode-3.0 | [dtolnay/unicode-ident](https://github.com/dtolnay/unicode-ident) |
| zmij | 1.0.23 | 传递 | MIT | [dtolnay/zmij](https://github.com/dtolnay/zmij) |

## 许可证全文

上述许可证的标准全文见 [`licenses/`](./licenses/) 目录：

- [`LGPL-3.0.txt`](./licenses/LGPL-3.0.txt) / [`GPL-3.0.txt`](./licenses/GPL-3.0.txt) —
  GNU 官方文本（LGPL-3.0 由 GPL-3.0 附加补充条款构成，故两份并附）
- [`MIT.txt`](./licenses/MIT.txt)、[`Apache-2.0.txt`](./licenses/Apache-2.0.txt)、
  [`BSD-3-Clause.txt`](./licenses/BSD-3-Clause.txt)、[`BSD-2-Clause.txt`](./licenses/BSD-2-Clause.txt)、
  [`Unicode-3.0.txt`](./licenses/Unicode-3.0.txt)、[`Unlicense.txt`](./licenses/Unlicense.txt) —
  SPDX License List 标准文本（Unicode-3.0 取自 unicode.org 官方）
- [`PSF-2.0.txt`](./licenses/PSF-2.0.txt) — 取自 typing_extensions 发行包内官方文本

Rust 采用 Rust 社区惯例的"OR"双许可（MIT OR Apache-2.0），下游可任选其一适用。
