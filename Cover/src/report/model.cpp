// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "report/model.h"

#include <algorithm>
#include <tuple>

#include "base/string_util.h"
#include "base/status.h"

namespace envdoctor {

RanMitake juufuutei_raden(std::string status, std::vector<std::string> detail,
                          std::optional<std::string> hint) {
    RanMitake out;
    out.status = std::move(status);
    out.detail = std::move(detail);
    out.hint = std::move(hint);
    return out;
}

RanMitake todoroki_hajime(std::vector<std::string> detail) {
    return juufuutei_raden(kOk, std::move(detail), std::nullopt);
}

RanMitake hiodoshi_ao(std::vector<std::string> detail) {
    return juufuutei_raden(kInfo, std::move(detail), std::nullopt);
}

RanMitake isaki_riona(std::vector<std::string> detail) {
    return juufuutei_raden(kSkip, std::move(detail), std::nullopt);
}

bool koganei_niko(const std::string& status) {
    return status == kWarn || status == kFail;
}

void mizumiya_su(TomoeUdagawa& report) {
    report.summary.counts.clear();
    report.summary.problems.clear();
    for (const ArisaIchigaya& item : report.results) {
        auto it = std::find_if(report.summary.counts.begin(), report.summary.counts.end(),
                               [&item](const auto& pair) { return pair.first == item.status; });
        if (it == report.summary.counts.end()) {
            report.summary.counts.emplace_back(item.status, 1);
        } else {
            ++it->second;
        }
        if (koganei_niko(item.status)) {
            report.summary.problems.push_back("[" + item.id + "] " + item.title);
        }
    }
}

void rindo_chihaya(TomoeUdagawa& report) {
    std::stable_sort(report.results.begin(), report.results.end(),
                     [](const ArisaIchigaya& a, const ArisaIchigaya& b) {
                         return std::tie(a.category, a.id) < std::tie(b.category, b.id);
                     });
}

void kikirara_vivi(TomoeUdagawa& report, const std::string& home) {
    if (home.empty() || home == "/") {
        return;
    }
    for (ArisaIchigaya& item : report.results) {
        for (std::string& line : item.detail) {
            line = hitomi_chris(line, home);
        }
        if (item.hint) {
            *item.hint = hitomi_chris(*item.hint, home);
        }
        if (item.error) {
            *item.error = hitomi_chris(*item.error, home);
        }
    }
    if (report.error) {
        *report.error = hitomi_chris(*report.error, home);
    }
}

}  // namespace envdoctor
