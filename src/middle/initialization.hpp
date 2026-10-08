// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/hir.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace cross::mir {

struct InitializedBitRange {
    std::uint64_t begin{}, end{};
    friend bool operator==(const InitializedBitRange&, const InitializedBitRange&) = default;
};

using TypeStorageSize = std::function<std::uint64_t(hir::TypeId)>;

// Ranges are normalized, non-overlapping initialized intervals. HIR owns member
// layout; the caller supplies selected-target storage sizes, never ABI carriers.
// Records need every semantic field; unions need one complete alternative.
[[nodiscard]] bool initialized_type(const hir::Module& module, hir::TypeId type,
                                    std::uint64_t base_bits,
                                    std::span<const InitializedBitRange> assigned,
                                    const TypeStorageSize& storage_size);

} // namespace cross::mir
