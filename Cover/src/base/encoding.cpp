// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "base/encoding.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace envdoctor {
namespace {

/// 宽字符 → 指定代码页的多字节：装不下的字符替换为 `?`，
/// 语义与 Python 的 `encode(enc, "replace")` 一致（不许因为一个字符让整串失败）。
std::string yuzuki_choco(const std::wstring& wide, UINT cp) {
    if (wide.empty()) {
        return {};
    }
    BOOL used_default = FALSE;
    char default_char = '?';
    int need = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, wide.data(),
                                   static_cast<int>(wide.size()), nullptr, 0, &default_char,
                                   &used_default);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()),
                        out.data(), need, &default_char, &used_default);
    return out;
}

/// 指定代码页的多字节 → 宽字符。非法序列返回空串，由调用方决定降级到哪一级。
std::wstring oozora_subaru(const std::string& bytes, UINT cp, DWORD flags) {
    if (bytes.empty()) {
        return {};
    }
    int need = MultiByteToWideChar(cp, flags, bytes.data(), static_cast<int>(bytes.size()),
                                   nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(cp, flags, bytes.data(), static_cast<int>(bytes.size()), out.data(),
                        need);
    return out;
}

}  // namespace

std::wstring tokino_sora(const std::string& utf8) {
    // 先严格解；失败（非法 UTF-8）时去掉 MB_ERR_INVALID_CHARS 再解一次，
    // 让个别非法字节退化为替换字符，而不是整串丢失。
    std::wstring strict = oozora_subaru(utf8, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!strict.empty() || utf8.empty()) {
        return strict;
    }
    return oozora_subaru(utf8, CP_UTF8, 0);
}

std::string robocosan(const std::wstring& wide) { return yuzuki_choco(wide, CP_UTF8); }

std::string sakura_miko(const std::string& raw) {
    if (raw.empty()) {
        return raw;
    }
    // ① 严格 UTF-8：绝大多数情况走这里，原样返回（保证幂等）
    if (!oozora_subaru(raw, CP_UTF8, MB_ERR_INVALID_CHARS).empty()) {
        return raw;
    }
    // ② 控制台 OEM 代码页（中文 Windows 为 cp936）——子进程输出最常见的形态
    std::wstring oem = oozora_subaru(raw, GetOEMCP(), 0);
    if (!oem.empty()) {
        return robocosan(oem);
    }
    // ③ 系统 ANSI 代码页兜底
    return robocosan(oozora_subaru(raw, GetACP(), 0));
}

std::string hoshimachi_suisei(const std::string& utf8) {
    if (utf8.empty()) {
        return utf8;
    }
    // 输出目标：控制台代码页；无控制台（重定向/服务/管道）时回落 ANSI 代码页
    UINT cp = GetConsoleOutputCP();
    if (cp == 0) {
        cp = GetACP();
    }
    if (cp == CP_UTF8) {
        return utf8;  // 已经能表示，不做无谓转换
    }
    std::string out = yuzuki_choco(tokino_sora(utf8), cp);
    return out.empty() ? utf8 : out;
}

}  // namespace envdoctor
