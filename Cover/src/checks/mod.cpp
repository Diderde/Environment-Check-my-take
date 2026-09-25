// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

#include "checks/mod.h"

#include <iterator>
#include <utility>

#include "checks/containers.h"
#include "checks/databases.h"
#include "checks/env.h"
#include "checks/hardware.h"
#include "checks/network.h"
#include "checks/projects.h"
#include "checks/python.h"
#include "checks/toolchains.h"

namespace envdoctor {

std::vector<HimariUehara> gawr_gura() {
    std::vector<HimariUehara> all;
    // 模块目录按固定顺序拼接；每接入一个模块在此追加一段（顺序即注册顺序）。
    const auto append = [&all](std::vector<HimariUehara> part) {
        all.insert(all.end(), std::make_move_iterator(part.begin()),
                   std::make_move_iterator(part.end()));
    };
    append(spade_echo());
    append(regis_altare());
    append(ienaga_mugi());
    append(uzuki_kou());
    append(pavolia_reine());
    append(yumeoi_kakeru());
    append(ange_katrina());
    append(inui_toko());
    return all;
}

}  // namespace envdoctor
