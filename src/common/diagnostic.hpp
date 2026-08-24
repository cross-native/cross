// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <iosfwd>
#include <string_view>

namespace cross {

enum class DiagnosticLevel { Note, Warning, Error };

class Diagnostics {
public:
    explicit Diagnostics(std::ostream& stream);

    void report(DiagnosticLevel level, SourceLocation location,
                std::string_view message);
    void error(SourceLocation location, std::string_view message);
    void warning(SourceLocation location, std::string_view message);
    void note(SourceLocation location, std::string_view message);
    void command_error(std::string_view message);

    [[nodiscard]] unsigned errors() const { return errors_; }
    [[nodiscard]] unsigned warnings() const { return warnings_; }

private:
    std::ostream& stream_;
    unsigned errors_{};
    unsigned warnings_{};
};

} // namespace cross
