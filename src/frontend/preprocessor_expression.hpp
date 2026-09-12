// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"

#include <string_view>

namespace cross {

// Operates on macro-expanded text. Unlike ordinary constant evaluation,
// preprocessing uses fixed i64/u64 arithmetic with wrapping signed overflow.
bool evaluate_preprocessing_condition(std::string_view text,
                                     SourceLocation location,
                                     Diagnostics& diagnostics,
                                     unsigned address_bits);

} // namespace cross
