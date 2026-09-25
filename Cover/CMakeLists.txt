# Copyright (C) 2026 Diderde
# SPDX-License-Identifier: MIT
#
# envdoctor 的 C++ 实现（Cover 线）。与 Revamp/ 下的 Rust+Python 版并存，迁移完成后后者删除。
# 全部依赖限 MIT 兼容许可，清单见仓库根 THIRD_PARTY_LICENSES.md。

cmake_minimum_required(VERSION 3.24)
project(envdoctor_cover LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

# 依赖缓存放在源码树的 .deps（已 gitignore）：首次配置拉取，之后离线可复用。
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "依赖缓存目录")

include(FetchContent)

FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        11.0.2
  GIT_SHALLOW    TRUE)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.11
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(fmt doctest)

# 统一编译选项。`/utf-8` 是必需的：源码里全是中文注释与中文字面量，
# MSVC 默认按系统 ANSI 代码页读源文件，不加会把中文读坏并报 C4819。
add_library(envdoctor_flags INTERFACE)
target_compile_options(envdoctor_flags INTERFACE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive- /Zc:__cplusplus>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

# 基础层：编码转换、字符串工具、结果类型、文件系统、外部命令执行、Win32 小工具、SEH 守卫。
# 不依赖任何 Windows UI/网络库。
add_library(envdoctor_base STATIC
  src/base/encoding.cpp
  src/base/fs.cpp
  src/base/process.cpp
  src/base/seh.cpp
  src/base/string_util.cpp
  src/base/win32.cpp)
target_include_directories(envdoctor_base PUBLIC src)
target_link_libraries(envdoctor_base
  PUBLIC fmt::fmt
  PRIVATE envdoctor_flags)

# Windows 平台层：注册表、外部工具白名单。
add_library(envdoctor_win STATIC
  src/win/registry.cpp
  src/win/tools.cpp)
target_link_libraries(envdoctor_win
  PUBLIC envdoctor_base
  PRIVATE envdoctor_flags)

# 报告与引擎：报告契约、JSON 序列化、检查项注册与调度。
add_library(envdoctor_core STATIC
  src/checks/containers.cpp
  src/checks/databases.cpp
  src/checks/env.cpp
  src/checks/hardware.cpp
  src/checks/mod.cpp
  src/checks/network.cpp
  src/checks/projects.cpp
  src/checks/python.cpp
  src/checks/toolchains.cpp
  src/engine/engine.cpp
  src/report/json.cpp
  src/report/model.cpp)
target_link_libraries(envdoctor_core
  PUBLIC envdoctor_win
  PRIVATE envdoctor_flags)

add_executable(envdoctor_tests
  tests/main.cpp
  tests/test_base.cpp
  tests/test_checks.cpp
  tests/test_cli.cpp
  tests/test_containers.cpp
  tests/test_databases.cpp
  tests/test_env.cpp
  tests/test_fs.cpp
  tests/test_hardware.cpp
  tests/test_network.cpp
  tests/test_process.cpp
  tests/test_projects.cpp
  tests/test_python.cpp
  tests/test_report.cpp
  tests/test_toolchains.cpp)
target_link_libraries(envdoctor_tests
  PRIVATE envdoctor_cli doctest::doctest envdoctor_flags)

# 命令行层做成静态库：命令行程序与测试都要链它
#（测试直接调参数解析与格式化函数，只把它编进 exe 会让测试链接失败）。
add_library(envdoctor_cli STATIC
  src/cli/cli.cpp)
target_link_libraries(envdoctor_cli
  PUBLIC envdoctor_core
  PRIVATE envdoctor_flags)

# 命令行程序。
add_executable(envdoctor
  src/main.cpp)
target_link_libraries(envdoctor
  PRIVATE envdoctor_cli envdoctor_flags)

enable_testing()
add_test(NAME base COMMAND envdoctor_tests)

# 前端（TUI/GUI）。默认关闭：它要额外拉取三个依赖，日常改检查项时不必付这份代价；
# 出正式产物时用 -DENVDECTOR_WITH_UI=ON（发布构建即为默认打开）。
option(ENVDECTOR_WITH_UI "构建 TUI/GUI 前端（拉取 ftxui / Dear ImGui / WIL）" OFF)

if(ENVDECTOR_WITH_UI)
  set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v6.1.9
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(wil
    GIT_REPOSITORY https://github.com/microsoft/wil.git
    GIT_TAG        v1.0.260126.7
    GIT_SHALLOW    TRUE)

  # ftxui 自带可用的 CMake 工程；Dear ImGui 与 WIL 只是"源码 / 头文件投放"：
  # 它们仓库里的 CMakeLists.txt 服务于各自的测试与文档（WIL 的测试还要 nuget），
  # 直接 add_subdirectory 会在配置阶段就失败，所以只下载、不接入构建。
  FetchContent_MakeAvailable(ftxui)
  if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
  endif()
  FetchContent_GetProperties(imgui)
  if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
  endif()
  FetchContent_GetProperties(wil)
  if(NOT wil_POPULATED)
    FetchContent_Populate(wil)
  endif()
  if(POLICY CMP0169)
    cmake_policy(POP)
  endif()

  # Dear ImGui 本体 + Win32/D3D11 后端。刻意独立成一个目标：不套本项目的 /W4 等编译选项
  #（第三方源码的告警不归我们修，也不该淹掉自己的告警）。
  add_library(envdoctor_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
  target_include_directories(envdoctor_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
  target_compile_definitions(envdoctor_imgui PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(envdoctor_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

  add_library(envdoctor_ui STATIC
    src/gui/gui.cpp
    src/tui/tui.cpp)
  target_include_directories(envdoctor_ui PUBLIC src PRIVATE "${wil_SOURCE_DIR}/include")
  target_compile_definitions(envdoctor_ui PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(envdoctor_ui
    PUBLIC envdoctor_core
    PRIVATE ftxui::screen ftxui::dom ftxui::component envdoctor_imgui
    PRIVATE user32 gdi32 comdlg32
    PRIVATE envdoctor_flags)
endif()
