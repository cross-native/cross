// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace cross::x86_64 {

// Values match the feature-table order in target.cpp. Keeping this enum
// target-local lets every architecture own a dense independent feature space.
enum class Feature : std::uint16_t {
#define X(name, spelling) name,
#include "target/x86_64/features.def"
#undef X
    Count,
};

inline constexpr auto feature_names = std::array{
#define X(name, spelling) std::string_view{spelling},
#include "target/x86_64/features.def"
#undef X
};

static_assert(feature_names.size() ==
              static_cast<std::size_t>(Feature::Count));

[[nodiscard]] constexpr std::string_view feature_name(Feature feature) {
    const auto index = static_cast<std::size_t>(feature);
    return index < feature_names.size() ? feature_names[index]
                                        : std::string_view{};
}

} // namespace cross::x86_64
