// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "report/json.h"

#include <cmath>
#include <string>

#include "base/status.h"

namespace envdoctor {
namespace {

/// JSON 字符串转义（与 Python `json.dumps(..., ensure_ascii=False)` 一致：
/// 只转义引号、反斜杠与控制字符，非 ASCII 原样输出）。
void sorashina_sopia(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (c < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4) & 0xF]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(raw);
                }
        }
    }
    out.push_back('"');
}

/// 数值文本：整数形态也带一位小数（`1.0`），与上一版一致；最多保留两位小数。
std::string suzuna_tsuzuri(double value) {
    if (!std::isfinite(value)) {
        return "0.0";
    }
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    std::string s(buf);
    while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') {
        s.pop_back();
    }
    return s;
}

/// 单个条目（缩进 4 空格；`detail` 的元素缩进 6 空格）。
void hyakuto_kyoko(std::string& out, const ArisaIchigaya& item) {
    out += "    {\n      \"id\": ";
    sorashina_sopia(out, item.id);
    out += ",\n      \"title\": ";
    sorashina_sopia(out, item.title);
    out += ",\n      \"category\": ";
    sorashina_sopia(out, item.category);
    out += ",\n      \"status\": ";
    sorashina_sopia(out, item.status);
    out += ",\n      \"detail\": ";
    if (item.detail.empty()) {
        out += "[]";
    } else {
        out += "[\n";
        for (size_t i = 0; i < item.detail.size(); ++i) {
            out += "        ";
            sorashina_sopia(out, item.detail[i]);
            out += (i + 1 == item.detail.size()) ? "\n" : ",\n";
        }
        out += "      ]";
    }
    out += ",\n      \"hint\": ";
    if (item.hint) {
        sorashina_sopia(out, *item.hint);
    } else {
        out += "null";
    }
    out += ",\n      \"duration_ms\": " + suzuna_tsuzuri(item.duration_ms);
    out += ",\n      \"error\": ";
    if (item.error) {
        sorashina_sopia(out, *item.error);
    } else {
        out += "null";
    }
    out += "\n    }";
}

/// 汇总（缩进 4 空格；`counts` 的键缩进 6 空格）。
void mori_calliope(std::string& out, const TsugumiHazawa& summary) {
    out += "{\n    \"counts\": ";
    if (summary.counts.empty()) {
        out += "{}";
    } else {
        out += "{\n";
        for (size_t i = 0; i < summary.counts.size(); ++i) {
            out += "      ";
            sorashina_sopia(out, summary.counts[i].first);
            out += ": " + std::to_string(summary.counts[i].second);
            out += (i + 1 == summary.counts.size()) ? "\n" : ",\n";
        }
        out += "    }";
    }
    out += ",\n    \"problems\": ";
    if (summary.problems.empty()) {
        out += "[]";
    } else {
        out += "[\n";
        for (size_t i = 0; i < summary.problems.size(); ++i) {
            out += "      ";
            sorashina_sopia(out, summary.problems[i]);
            out += (i + 1 == summary.problems.size()) ? "\n" : ",\n";
        }
        out += "    ]";
    }
    out += "\n  }";
}

}  // namespace

std::string achichi_mela(const TomoeUdagawa& report) {
    std::string out = "{\n";
    out += "  \"report_version\": " + std::to_string(report.report_version) + ",\n";
    out += "  \"source\": ";
    sorashina_sopia(out, report.source);
    out += ",\n  \"platform\": ";
    sorashina_sopia(out, report.platform);
    out += ",\n  \"generated_at_unix\": " + std::to_string(report.generated_at_unix) + ",\n";
    out += "  \"duration_ms\": " + suzuna_tsuzuri(report.duration_ms) + ",\n";
    out += "  \"error\": ";
    if (report.error) {
        sorashina_sopia(out, *report.error);
    } else {
        out += "null";
    }
    out += ",\n  \"results\": ";
    if (report.results.empty()) {
        out += "[]";
    } else {
        out += "[\n";
        for (size_t i = 0; i < report.results.size(); ++i) {
            hyakuto_kyoko(out, report.results[i]);
            out += (i + 1 == report.results.size()) ? "\n" : ",\n";
        }
        out += "  ]";
    }
    out += ",\n  \"summary\": ";
    mori_calliope(out, report.summary);
    out += "\n}";
    return out;
}

}  // namespace envdoctor
