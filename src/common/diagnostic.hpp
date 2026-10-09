// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <iosfwd>
#include <string>
#include <string_view>

namespace cross {

enum class DiagnosticLevel { Note, Warning, Error };

class Diagnostics {
public:
    // `tool` prefixes diagnostics that have no source position.
    explicit Diagnostics(std::ostream& stream, std::string_view tool = "cc");

    void report(DiagnosticLevel level, SourceLocation location,
                std::string_view message);
    void error(SourceLocation location, std::string_view message);
    void warning(SourceLocation location, std::string_view message);
    void note(SourceLocation location, std::string_view message);
    void command_error(std::string_view message);
    // An error at a "file:line" position of a file that is not Cross source,
    // such as a model file.
    void file_error(std::string_view position, std::string_view message);

    [[nodiscard]] unsigned errors() const { return errors_; }
    [[nodiscard]] unsigned warnings() const { return warnings_; }

private:
    std::ostream& stream_;
    std::string tool_;
    unsigned errors_{};
    unsigned warnings_{};
};

} // namespace cross
