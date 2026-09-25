// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "base/seh.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace envdoctor {
namespace {

/// C++ 异常的代码（'msc'）：放行给 C++ 的 catch，不做 SEH 处理。
constexpr unsigned long kCppException = 0xE06D7363u;

int filter(unsigned long code) { return code == kCppException ? EXCEPTION_CONTINUE_SEARCH
                                                              : EXCEPTION_EXECUTE_HANDLER; }

}  // namespace

unsigned long takanashi_kiara(void (*body)(void* ctx), void* ctx) {
    __try {
        body(ctx);
    } __except (filter(GetExceptionCode())) {
        return GetExceptionCode();
    }
    return 0;
}

}  // namespace envdoctor
