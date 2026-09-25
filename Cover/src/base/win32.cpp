// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "base/win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#include "base/encoding.h"
#include "base/string_util.h"

namespace envdoctor {

std::string houshou_marine(unsigned long code) {
    std::string text;
    LPWSTR raw = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, static_cast<DWORD>(code),
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
    if (n != 0 && raw != nullptr) {
        text = azki(robocosan(std::wstring(raw, n)));
        LocalFree(raw);
    }
    if (text.empty()) {
        text = "未知错误";
    }
    return text + " (os error " + std::to_string(code) + ")";
}

std::optional<std::string> uruha_rushia(const std::wstring& name) {
    const DWORD need = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (need == 0) {
        return std::nullopt;
    }
    std::wstring buf(static_cast<size_t>(need), L'\0');
    const DWORD got = GetEnvironmentVariableW(name.c_str(), buf.data(), need);
    if (got == 0 || got >= need) {
        return std::nullopt;
    }
    buf.resize(got);
    std::string value = robocosan(buf);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace envdoctor
