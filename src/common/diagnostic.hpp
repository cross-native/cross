// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace cross {

enum class DiagnosticLevel { Note, Warning, Error };

// The selectable warnings, in the order of warning_table().
enum class Warning : std::uint8_t {
    AddressOfPackedMember,
    Conversion,
    Cpp,
    FloatConversion,
    Format,
    ImplicitFallthrough,
    MaybeUninitialized,
    SignCompare,
    SignConversion,
    Uninitialized,
    UnusedFunction,
    UnusedParameter,
    UnusedVariable,
};

struct WarningInfo {
    // The -W spelling without "-W".
    std::string_view name;
    bool enabled_by_default{};
    std::string_view description;
};
inline constexpr std::size_t warning_count = 13;
std::span<const WarningInfo, warning_count> warning_table();
inline std::string_view warning_name(Warning warning) {
    return warning_table()[static_cast<std::size_t>(warning)].name;
}
std::optional<Warning> find_warning(std::string_view name);

// The resolved -W, -Werror, and -w settings.
struct WarningPolicy {
    WarningPolicy();
    std::array<bool, warning_count> enabled{};
    // -Werror=NAME (true) and -Wno-error=NAME (false) override all_errors.
    std::array<std::optional<bool>, warning_count> error{};
    // -Werror: every warning is an error.
    bool all_errors{};
    // -w: no warnings at all.
    bool inhibit{};
};

class Diagnostics {
public:
    // `tool` prefixes diagnostics that have no source position.
    explicit Diagnostics(std::ostream& stream, std::string_view tool = "cc");

    void report(DiagnosticLevel level, SourceLocation location,
                std::string_view message);
    void error(SourceLocation location, std::string_view message);
    // An uncategorized warning: -w drops it and -Werror makes it an error.
    void warning(SourceLocation location, std::string_view message);
    // A selectable warning, reported only while it is enabled.
    void warning(Warning warning, SourceLocation location, std::string_view message);
    void note(SourceLocation location, std::string_view message);
    void command_error(std::string_view message);
    // A note that belongs to no source position.
    void command_note(std::string_view message);
    // An error at a "file:line" position of a file that is not Cross source,
    // such as a model file.
    void file_error(std::string_view position, std::string_view message);

    void set_warning_policy(const WarningPolicy& policy) { policy_ = policy; }
    [[nodiscard]] bool enabled(Warning warning) const {
        return !policy_.inhibit && policy_.enabled[static_cast<std::size_t>(warning)];
    }
    // Whether -Werror made a warning an error.
    [[nodiscard]] bool promoted_warnings() const { return promoted_; }

    [[nodiscard]] unsigned errors() const { return errors_; }
    [[nodiscard]] unsigned warnings() const { return warnings_; }

private:
    void emit(DiagnosticLevel level, SourceLocation location, std::string_view message);
    // False for a warning already reported at this position, such as one in
    // each instance of a generic function.
    bool first_report(SourceLocation location, std::string_view message);

    std::ostream& stream_;
    std::string tool_;
    WarningPolicy policy_;
    std::set<std::tuple<const SourceFile*, std::size_t, std::string>> reported_;
    unsigned errors_{};
    unsigned warnings_{};
    bool promoted_{};
    // Notes after a warning that was not reported are dropped with it.
    bool suppress_notes_{};
};

} // namespace cross
