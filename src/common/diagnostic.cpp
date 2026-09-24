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
        if (item.line != 0 && item.line <= item.file->line_origins.size()) {
            const auto origin = item.file->line_origins[item.line - 1];
            if (origin.valid()) {
                const auto column = item.column;
                item = origin;
                item.column += std::max(1U, column) - 1;
            }
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
    if (location.file && location.file->expansion_at(location.offset)) {
        if (const auto* token = location.file->token_origin_at(location.offset);
            token && token->span.valid() && token->identity.expansion.value == 0 &&
            (token->span.file != location.file || token->span.offset != location.offset)) {
            emit(token->span, "note", "token supplied from here");
        }
    }
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
