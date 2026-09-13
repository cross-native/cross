// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
enum expression_overflow [[underlying(u8)]] {
    expression_last = 255u8,
    expression_too_large,
};
