// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/subtarget.hpp"

#include <cstdint>
#include <string_view>

namespace cross::mips {

enum class Feature : std::uint16_t {
#define X(name, spelling) name,
#include "target/mips/features.def"
#undef X
    Count,
};

[[nodiscard]] constexpr TargetFeatureId feature_id(Feature feature) {
    return TargetFeatureId{feature};
}

[[nodiscard]] constexpr std::string_view feature_name(Feature feature) {
    switch (feature) {
#define X(name, spelling) case Feature::name: return spelling;
#include "target/mips/features.def"
#undef X
    case Feature::Count: break;
    }
    return {};
}

} // namespace cross::mips
