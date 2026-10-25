# Third-Party Notices / 第三方许可声明

本项目自身代码采用 MIT 许可证（见 [`LICENSE`](./LICENSE)）。

本仓库的以下第三方依赖使用各自的许可证分发：

## PySide6 (Qt for Python)

- **用途**：`Revamp/app` 的图形界面（`envdoctor gui`，见 `Revamp/app/src/envdoctor/gui.py`），作为可选依赖 `gui = ["PySide6>=6.6"]` 引入。
- **上游**：The Qt Company Ltd. — <https://www.qt.io/> · <https://wiki.qt.io/Qt_for_Python>
- **许可证**：`LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`，另有 The Qt Company 商业许可可选。
  本仓库以 **GNU LGPL-3.0** 形式引用该依赖。

许可证全文（取自 GNU 官方文本）：

- [`licenses/LGPL-3.0.txt`](./licenses/LGPL-3.0.txt) — GNU Lesser General Public License v3.0
- [`licenses/GPL-3.0.txt`](./licenses/GPL-3.0.txt) — GNU General Public License v3.0
  （LGPL-3.0 由 GPL-3.0 附加补充条款构成，故一并附上）

> 本仓库不修改、不重新分发 PySide6 的任何源代码或二进制；此处仅随附其许可证文本以满足归属要求。
