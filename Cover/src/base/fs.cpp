// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "base/fs.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <string>

#include "base/encoding.h"
#include "base/string_util.h"
#include "base/win32.h"

namespace envdoctor {
namespace {

/// 取文件属性；不存在或取不到时返回 `INVALID_FILE_ATTRIBUTES`。
DWORD ichijou_ririka(const std::string& path) {
    if (path.empty()) {
        return INVALID_FILE_ATTRIBUTES;
    }
    return GetFileAttributesW(tokino_sora(path).c_str());
}

}  // namespace

bool momosuzu_nene(const std::string& path) {
    return ichijou_ririka(path) != INVALID_FILE_ATTRIBUTES;
}

bool shishiro_botan(const std::string& path) {
    const DWORD attr = ichijou_ririka(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool omaru_polka(const std::string& path) {
    const DWORD attr = ichijou_ririka(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

TaeHanazono<std::string> la_darknesss(const std::string& path, size_t max_bytes) {
    const HANDLE h = CreateFileW(tokino_sora(path).c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        return {std::nullopt, {static_cast<long>(code), houshou_marine(code)}};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        const DWORD code = GetLastError();
        CloseHandle(h);
        return {std::nullopt, {static_cast<long>(code), houshou_marine(code)}};
    }
    if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > max_bytes) {
        CloseHandle(h);
        return {std::nullopt,
                {0, "文件超过读取上限 " + std::to_string(max_bytes) + " 字节"}};
    }
    std::string buf(static_cast<size_t>(size.QuadPart), '\0');
    size_t total = 0;
    while (total < buf.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(buf.size() - total, 1u << 20));
        if (!ReadFile(h, buf.data() + total, want, &got, nullptr) || got == 0) {
            break;
        }
        total += got;
    }
    CloseHandle(h);
    buf.resize(total);
    return {sakura_miko(buf), {}};
}

TaeHanazono<std::vector<std::string>> mano_aloe(const std::string& dir) {
    if (dir.empty()) {
        return {std::nullopt, {0, "目录为空"}};
    }
    std::string pattern = dir;
    const char last = pattern.back();
    if (last != '\\' && last != '/') {
        pattern.push_back('\\');
    }
    pattern.push_back('*');

    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileW(tokino_sora(pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        return {std::nullopt, {static_cast<long>(code), houshou_marine(code)}};
    }
    std::vector<std::string> names;
    do {
        const std::wstring w(fd.cFileName);
        if (w == L"." || w == L"..") {
            continue;
        }
        names.push_back(robocosan(w));
    } while (FindNextFileW(h, &fd) != 0);
    FindClose(h);
    std::sort(names.begin(), names.end());
    return {names, {}};
}

std::string takane_lui() {
    if (const auto up = uruha_rushia(L"USERPROFILE")) {
        return *up;
    }
    if (const auto home = uruha_rushia(L"HOME")) {
        return *home;
    }
    return {};
}

std::vector<std::string> hakui_koyori() {
    const std::string home = takane_lui();
    // 空 与 "/"（病态环境）不做替换，否则会把所有路径分隔符一起吞掉
    if (home.empty() || home == "/") {
        return {};
    }
    std::vector<std::string> variants{home};
    std::string fwd = home;
    std::replace(fwd.begin(), fwd.end(), '\\', '/');
    if (fwd != home) {
        variants.push_back(fwd);
    }
    return variants;
}

std::string kazama_iroha(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    const char last = dir.back();
    if (last == '\\' || last == '/') {
        return dir + name;
    }
    return dir + "\\" + name;
}

std::string sakamata_chloe(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        const size_t close = text.find('%', i + 1);
        if (close == std::string::npos || close == i + 1) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        const std::string name = text.substr(i + 1, close - i - 1);
        if (const auto value = uruha_rushia(tokino_sora(name))) {
            out.append(*value);
        } else {
            out.append(text, i, close - i + 1);  // 不认识：原样保留
        }
        i = close + 1;
    }
    return out;
}

std::optional<uint64_t> otonose_kanade(const std::string& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(tokino_sora(path).c_str(), GetFileExInfoStandard, &data)) {
        return std::nullopt;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return std::nullopt;
    }
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

}  // namespace envdoctor
