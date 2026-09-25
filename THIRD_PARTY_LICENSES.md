# 第三方依赖与许可

本文件登记 envdoctor 的**全部第三方依赖**及其许可。收录原则：只允许 **MIT 兼容**的许可
（MIT / BSD / Apache-2.0 / BSL-1.0 这类）；携带传染性或弱传染性条款的组件（GPL / LGPL / MPL 等）
一律不进入本实现——这也是本实现不再使用 Qt/PySide6 的原因（PySide6 是 LGPL-3.0）。

本项目自身以 MIT 发布，见根目录 `LICENSE`。

## C++ 实现（`Cover/`，当前实现）

依赖在**构建期**由 CMake `FetchContent` 从上游仓库拉取并编译，仓库内不包含其源码副本；
版本号在 `Cover/CMakeLists.txt` 中逐个钉死（不用浮动分支）。

| 组件 | 版本 | 许可 | 用途 | 上游 |
|---|---|---|---|---|
| {fmt} | 11.0.2 | MIT | 文本格式化（构建期拉取） | https://github.com/fmtlib/fmt |
| doctest | v2.4.11 | MIT | 单元测试框架（仅测试目标链接，不进产物） | https://github.com/doctest/doctest |
| FTXUI | v6.1.9 | MIT | 终端界面（TUI） | https://github.com/ArthurSonzogni/FTXUI |
| Dear ImGui | v1.92.9b | MIT | 图形界面（GUI）的即时模式控件库 | https://github.com/ocornut/imgui |
| Windows Implementation Library (WIL) | v1.0.260126.7 | MIT | Win32/COM 资源包装（头文件only） | https://github.com/microsoft/wil |

说明：

- FTXUI / Dear ImGui / WIL 仅在开启 `-DENVDECTOR_WITH_UI=ON` 时参与构建；关掉时产物里不含它们。
- Dear ImGui 的 Win32 与 D3D11 后端（`backends/imgui_impl_win32.*`、`backends/imgui_impl_dx11.*`）
  与本体同许可（MIT）。
- 图形界面使用的 D3D11 / DXGI / D3DCompiler / comdlg32 / user32 / gdi32 / dwmapi 均随 Windows SDK
  提供，属操作系统组件，不单独列出。
- MSVC 运行时（`vcruntime`/`msvcp` 等）随 Visual Studio 分发，按微软的再分发条款使用。

## 关于被替换掉的旧实现

本仓库早期版本由 Rust 核心（C ABI 的 cdylib）+ Python 层（Typer CLI / Textual TUI / PySide6 GUI）组成，
现已整体替换为上面的 C++ 实现；旧代码不在当前工作树里，只存在于提交历史（以及本地的 `backup-pre-rewrite` 分支）。

替换的直接理由之一就是**许可口径**：旧实现的 `PySide6` 是 **LGPL-3.0**，是本项目唯一的非 MIT 兼容依赖
（LGPL-3.0 允许动态链接使用，但要求随分发附带许可文本与可替换性说明）。C++ 实现不再引入它，
图形界面改用 Dear ImGui（MIT），因此**当前实现不含任何非 MIT 兼容依赖**。

## 维护约定

- 新增依赖前先确认许可属 MIT 兼容；不确认就不引入。
- 引入或升级依赖时，同步更新本文件的版本号与许可栏。
- 本文件不记录测试数量、代码行数这类会过期的数字。
