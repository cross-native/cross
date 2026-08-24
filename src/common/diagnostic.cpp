// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/diagnostic.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace cross {

Diagnostics::Diagnostics(std::ostream& stream) : stream_(stream) {}

void Diagnostics::report(DiagnosticLevel level, SourceLocation location,
                         std::string_view message) {
    const char* label = "note";
    if (level == DiagnosticLevel::Warning) {
        label = "warning";
        ++warnings_;
    } else if (level == DiagnosticLevel::Error) {
        label = "error";
        ++errors_;
    }

    const auto emit = [&](SourceLocation item, std::string_view item_label,
                          std::string_view item_message) {
        if (!item.valid()) {
            stream_ << "cc: " << item_label << ": " << item_message << '\n';
            return;
        }
        stream_ << item.file->path.string() << ':' << item.line << ':'
                << item.column << ": " << item_label << ": "
                << item_message << '\n';
        const auto source_line = item.file->line(item.line);
        if (!source_line.empty()) {
            stream_ << source_line << '\n';
            const auto column = std::max(1U, item.column);
            stream_ << std::string(column - 1, ' ') << "^\n";
        }
    };

    emit(location, label, message);
    auto origin = location;
    for (unsigned depth = 0; origin.valid() && depth < 64; ++depth) {
        const auto* expansion = origin.file->expansion_at(origin.offset);
        if (!expansion) break;
        emit(expansion->invocation, "note",
             "in expansion of procedural macro '" +
                 expansion->macro_name + "'");
        emit(expansion->definition, "note",
             "procedural macro '" + expansion->macro_name +
                 "' defined here");
        origin = expansion->invocation;
    }
}

void Diagnostics::error(SourceLocation location, std::string_view message) {
    report(DiagnosticLevel::Error, location, message);
}

void Diagnostics::warning(SourceLocation location, std::string_view message) {
    report(DiagnosticLevel::Warning, location, message);
}

void Diagnostics::note(SourceLocation location, std::string_view message) {
    report(DiagnosticLevel::Note, location, message);
}

void Diagnostics::command_error(std::string_view message) {
    error({}, message);
}

} // namespace cross
