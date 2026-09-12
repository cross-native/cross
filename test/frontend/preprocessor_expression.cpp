// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/preprocessor_expression.hpp"

#include <iostream>
#include <sstream>
#include <string_view>

int main() {
    struct Case { std::string_view text; bool expected; };
    constexpr Case cases[] = {
        {"0", false}, {"1", true}, {"UNKNOWN", false},
        {"unknown::name", false}, {"0 && 1", false}, {"0 || 1", true},
        {"1 || 1 / 0", true}, {"0 && 1 / 0", false},
        {"1 || 1 << 64", true}, {"0 && 1 >> -1", false},
        {"(2 + 3 * 4) == 14", true}, {"(2 + 3) * 4 == 20", true},
        {"20 / 3 == 6 && 20 % 3 == 2", true},
        {"-20 / 3 == -6 && -20 % 3 == -2", true},
        {"20 / -3 == -6 && 20 % -3 == 2", true},
        {"-20 / -3 == 6 && -20 % -3 == -2", true},
        {"1 + 2 << 3 == 24", true}, {"32 >> 2 + 1 == 4", true},
        {"2 < 3 == 1", true}, {"2 <= 2 && 3 > 2 && 3 >= 3", true},
        {"2 != 3 && !(2 == 3)", true}, {"1 | 2 ^ 3 & 4", true},
        {"1 | 2 ^ 3 & 4 == 1", true},
        {"(1 | (2 ^ (3 & 4))) == 3", true},
        {"-1 < 0", true}, {"-1 < 0u64", false}, {"-1 > 0u64", true},
        {"-1 == 0xffffffffffffffffu64", true},
        {"0xffffffffffffffffu64 + 1u64 == 0", true},
        {"9223372036854775807i64 + 1 < 0", true},
        {"(-9223372036854775807 - 1) / -1 == (-9223372036854775807 - 1)", true},
        {"(-9223372036854775807 - 1) % -1 == 0", true},
        {"9223372036854775807i64 * 2 == -2", true},
        {"-1 >> 63 == -1", true}, {"0xffffffffffffffffu64 >> 63 == 1", true},
        {"1 << 63 < 0", true}, {"1u64 << 63 > 0", true},
        {"0b1010 == 012 && 012 == 0xau32", true},
        {"1_000 == 1000i64", true}, {"~0u64 == 0xffffffffffffffffu64", true},
        {"+1 && !!2 && !0", true}, {"'A' == 65", true},
        {"'\\n' == 10 && '\\x41' == 65", true},
        {"255u8 + 1 == 256", true}, {"127i8 + 1 == 128", true},
        {"(1 < 2) - 2 < 0", true}, {"(1u64 && 1u64) - 2 < 0", true},
    };
    for (const auto& test : cases) {
        std::ostringstream errors;
        cross::Diagnostics diagnostics(errors);
        const auto actual = cross::evaluate_preprocessing_condition(test.text, {}, diagnostics, 64);
        if (diagnostics.errors() || actual != test.expected) {
            std::cerr << "condition failed: " << test.text << '\n' << errors.str();
            return 1;
        }
    }
    constexpr std::string_view invalid[] = {
        "", "()", "1 +", "(1", "1)", "1 2", "1 ? 2 : 3", "1 = 2",
        "1.5", "1f64", "1u", "1i64u64", "0x", "0b2", "09", "18446744073709551616u64",
        "128i8", "256u8", "9223372036854775808i64", "1 / 0", "1 % 0",
        "1 << 64", "1 >> -1", "0 || 1 / 0", "1 && 1 / 0", "1 || (1 +)",
        "++1", "--1", "'ab'", "\"text\"", "name(1)", "name:other", "$bad", "$::", "name::",
    };
    for (const auto text : invalid) {
        std::ostringstream errors;
        cross::Diagnostics diagnostics(errors);
        cross::evaluate_preprocessing_condition(text, {}, diagnostics, 64);
        if (!diagnostics.errors()) {
            std::cerr << "condition unexpectedly accepted: " << text << '\n';
            return 1;
        }
    }
    for (const auto address_bits : {32U, 64U}) {
        std::ostringstream errors;
        cross::Diagnostics diagnostics(errors);
        const auto actual = cross::evaluate_preprocessing_condition(
            "0x100000000uptr", {}, diagnostics, address_bits);
        if ((address_bits == 32 && !diagnostics.errors()) ||
            (address_bits == 64 && (diagnostics.errors() || !actual))) {
            std::cerr << "incorrect target pointer-sized literal range\n";
            return 1;
        }
    }
    return 0;
}
