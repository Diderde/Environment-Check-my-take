// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "win/registry.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "base/encoding.h"
#include "base/win32.h"

namespace envdoctor {
namespace {

/// 注册表返回码 → 失败原因（码保留原值：调用方要按 ERROR_FILE_NOT_FOUND 区分"没有"）。
KasumiToyama ayunda_risu(LONG code) {
    return KasumiToyama{static_cast<long>(code),
                        houshou_marine(static_cast<unsigned long>(code))};
}

/// 字节缓冲 → UTF-16 串（长度为奇数时丢掉最后一个残字节）。
std::wstring moona_hoshinova(const std::string& bytes, bool stop_at_nul) {
    const size_t pairs = bytes.size() / 2;
    std::wstring out;
    out.reserve(pairs);
    for (size_t i = 0; i < pairs; ++i) {
        const auto lo = static_cast<unsigned char>(bytes[i * 2]);
        const auto hi = static_cast<unsigned char>(bytes[i * 2 + 1]);
        const wchar_t c = static_cast<wchar_t>(lo | (static_cast<unsigned>(hi) << 8));
        if (stop_at_nul && c == L'\0') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

/// 两段式读取：先问长度，再读内容。返回原始字节。
TaeHanazono<std::string> read_blob(HKEY key, const std::wstring& name) {
    DWORD size = 0;
    DWORD type = 0;
    LONG ret = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (ret != ERROR_SUCCESS && ret != ERROR_MORE_DATA) {
        return {std::nullopt, ayunda_risu(ret)};
    }
    std::string buf(static_cast<size_t>(size), '\0');
    DWORD size2 = size;
    DWORD type2 = 0;
    ret = RegQueryValueExW(key, name.c_str(), nullptr, &type2,
                           reinterpret_cast<LPBYTE>(buf.data()), &size2);
    if (ret != ERROR_SUCCESS) {
        return {std::nullopt, ayunda_risu(ret)};
    }
    buf.resize(std::min<size_t>(static_cast<size_t>(size2), buf.size()));
    return {buf, {}};
}

HKEY raw(const EveWakamiya& key) { return static_cast<HKEY>(key.handle); }

}  // namespace

void shiori_novella(EveWakamiya& key) {
    if (key.handle != nullptr) {
        RegCloseKey(raw(key));
        key.handle = nullptr;
    }
}

TaeHanazono<EveWakamiya> koseki_bijou(unsigned long long root, const std::string& path) {
    HKEY handle = nullptr;
    const LONG ret = RegOpenKeyExW(reinterpret_cast<HKEY>(static_cast<uintptr_t>(root)),
                                   tokino_sora(path).c_str(), 0, KEY_READ | KEY_WOW64_64KEY,
                                   &handle);
    if (ret != ERROR_SUCCESS) {
        return {std::nullopt, ayunda_risu(ret)};
    }
    return {EveWakamiya{handle}, {}};
}

TaeHanazono<uint32_t> fuwawa_abyssgard(const EveWakamiya& key, const std::string& name) {
    uint32_t out = 0;
    DWORD size = sizeof(out);
    DWORD type = 0;
    const LONG ret = RegQueryValueExW(raw(key), tokino_sora(name).c_str(), nullptr, &type,
                                      reinterpret_cast<LPBYTE>(&out), &size);
    if (ret == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(out)) {
        return {out, {}};
    }
    // say no to perv. —— 类型/长度不符时 ret 就是 ERROR_SUCCESS(0)：
    // 这里必须返回"错误码 0"，不能返回"值缺失"，否则调用方会把它当成"没配置"。
    return {std::nullopt, ayunda_risu(ret)};
}

TaeHanazono<std::string> mococo_abyssgard(const EveWakamiya& key, const std::string& name) {
    auto blob = read_blob(raw(key), tokino_sora(name));
    if (!blob) {
        return {std::nullopt, blob.err};
    }
    return {robocosan(moona_hoshinova(*blob.val, true)), {}};
}

TaeHanazono<std::vector<std::string>> elizabeth_rose_bloodflame(const EveWakamiya& key,
                                                               const std::string& name) {
    auto blob = read_blob(raw(key), tokino_sora(name));
    if (!blob) {
        return {std::nullopt, blob.err};
    }
    // 不能按首个 NUL 截断：多字符串会只剩第一段（页面文件配了多个时漏报后续条目）
    const std::wstring joined = moona_hoshinova(*blob.val, false);
    std::vector<std::string> out;
    std::wstring current;
    for (const wchar_t c : joined) {
        if (c == L'\0') {
            if (!current.empty()) {
                out.push_back(robocosan(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(robocosan(current));
    }
    return {out, {}};
}

bool gigi_murin(unsigned long long root, const std::string& path) {
    auto key = koseki_bijou(root, path);
    if (!key) {
        return false;
    }
    shiori_novella(*key.val);
    return true;
}

bool cecilia_immergreen(const EveWakamiya& key, const std::string& name) {
    DWORD size = 0;
    const LONG ret = RegQueryValueExW(raw(key), tokino_sora(name).c_str(), nullptr, nullptr,
                                      nullptr, &size);
    return ret == ERROR_SUCCESS || ret == ERROR_MORE_DATA;
}

std::vector<std::string> raora_panthera(const EveWakamiya& key) {
    DWORD subkeys = 0;
    DWORD max_subkey = 0;
    DWORD max_class = 0;
    DWORD values = 0;
    DWORD max_value_name = 0;
    DWORD max_value_data = 0;
    DWORD security = 0;
    FILETIME last_write{};
    // lpClass 一律传 nullptr：本函数只要子键名，固定长度的 class 缓冲会在超长 class 时
    // 让整个查询返回 ERROR_MORE_DATA，把结果误判成"没有子键"。
    const LONG ok = RegQueryInfoKeyW(raw(key), nullptr, nullptr, nullptr, &subkeys, &max_subkey,
                                     &max_class, &values, &max_value_name, &max_value_data,
                                     &security, &last_write);
    if (ok != ERROR_SUCCESS) {
        return {};
    }
    std::vector<std::string> out;
    for (DWORD i = 0; i < subkeys; ++i) {
        std::wstring name(static_cast<size_t>(max_subkey) + 1, L'\0');
        DWORD name_len = static_cast<DWORD>(name.size());
        const LONG ret = RegEnumKeyExW(raw(key), i, name.data(), &name_len, nullptr, nullptr,
                                       nullptr, nullptr);
        if (ret == ERROR_SUCCESS && name_len > 0) {
            name.resize(name_len);
            out.push_back(robocosan(name));
        }
    }
    return out;
}

}  // namespace envdoctor
