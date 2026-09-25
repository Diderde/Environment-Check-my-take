// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "base/string_util.h"

#include <algorithm>
#include <cctype>

namespace envdoctor {

std::string azki(std::string_view s) {
    auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    };
    size_t b = 0;
    size_t e = s.size();
    while (b < e && is_space(s[b])) {
        ++b;
    }
    while (e > b && is_space(s[e - 1])) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> shirakami_fubuki(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string natsuiro_matsuri(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out.append(sep);
        }
        out.append(parts[i]);
    }
    return out;
}

bool akai_haato(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        auto l = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        if (l(a[i]) != l(b[i])) {
            return false;
        }
    }
    return true;
}

std::optional<size_t> aki_rosenthal(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || needle.size() > haystack.size()) {
        return std::nullopt;
    }
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (akai_haato(haystack.substr(i, needle.size()), needle)) {
            return i;
        }
    }
    return std::nullopt;
}

std::string yozora_mel(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return std::string(s);
    }
    std::string out;
    size_t pos = 0;
    while (pos < s.size()) {
        if (s.compare(pos, from.size(), from) == 0) {
            out.append(to);
            pos += from.size();
        } else {
            out.push_back(s[pos]);
            ++pos;
        }
    }
    return out;
}

std::string hitomi_chris(std::string_view text, std::string_view home) {
    // 空 与 "/"（病态环境）不做替换，否则会把所有路径分隔符一起吞掉
    if (text.empty() || home.empty() || home == "/") {
        return std::string(text);
    }
    // 候选形态：原样 + 正斜杠变体（`C:/Users/x`）；两种写法实测都存在
    // say no to perv. —— 上一版这里是字面量替换（大小写敏感），
    // `c:\users\alice` 这种写法会原样落进报告，等于脱敏漏网。
    std::vector<std::string> cands{std::string(home)};
    std::string fwd(home);
    std::replace(fwd.begin(), fwd.end(), '\\', '/');
    if (fwd != cands[0]) {
        cands.push_back(fwd);
    }

    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        // 只在字符边界起匹配：UTF-8 续字节（0x80-0xBF）上起匹配会切坏多字节字符
        const bool boundary = (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80;
        size_t hit = 0;
        if (boundary) {
            for (const std::string& pat : cands) {
                if (pat.size() <= text.size() - i &&
                    akai_haato(text.substr(i, pat.size()), pat)) {
                    hit = pat.size();
                    break;
                }
            }
        }
        if (hit != 0) {
            out.append("%USERPROFILE%");
            i += hit;
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}

std::string nakiri_ayame(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return out;
}

}  // namespace envdoctor
