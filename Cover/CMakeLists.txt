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

# 基础层：编码转换、字符串工具、结果类型、文件系统、外部命令执行、Win32 小工具。
# 不依赖任何 Windows UI/网络库。
add_library(envdoctor_base STATIC
  src/base/encoding.cpp
  src/base/fs.cpp
  src/base/process.cpp
  src/base/string_util.cpp
  src/base/win32.cpp)
target_include_directories(envdoctor_base PUBLIC src)
target_link_libraries(envdoctor_base
  PUBLIC fmt::fmt
  PRIVATE envdoctor_flags)

add_executable(envdoctor_tests
  tests/main.cpp
  tests/test_base.cpp
  tests/test_fs.cpp
  tests/test_process.cpp)
target_link_libraries(envdoctor_tests
  PRIVATE envdoctor_base doctest::doctest envdoctor_flags)

enable_testing()
add_test(NAME base COMMAND envdoctor_tests)
