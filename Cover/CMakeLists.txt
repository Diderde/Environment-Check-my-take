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
  tests/test_python.cpp
  tests/test_report.cpp
  tests/test_toolchains.cpp)
target_link_libraries(envdoctor_tests
  PRIVATE envdoctor_core doctest::doctest envdoctor_flags)

# 命令行程序。
add_executable(envdoctor
  src/cli/cli.cpp
  src/main.cpp)
target_link_libraries(envdoctor
  PRIVATE envdoctor_core envdoctor_flags)

enable_testing()
add_test(NAME base COMMAND envdoctor_tests)
