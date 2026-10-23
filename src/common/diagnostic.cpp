// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/diagnostic.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace cross {

std::span<const WarningInfo, warning_count> warning_table() {
    static constexpr WarningInfo table[] = {
        {"address-of-packed-member", true,
         "taking the address of a packed member that may be under-aligned"},
        {"conversion", false,
         "implicit integer conversions that may change a value"},
        {"cpp", true, "#warning directives"},
        {"float-conversion", false,
         "implicit conversions that lose floating precision"},
        {"format", false, "arguments that do not match a call's format string"},
        {"implicit-fallthrough", false,
         "a switch case that falls through into the next label"},
        {"sign-compare", false,
         "comparisons of integers of different signedness"},
        {"sign-conversion", false,
         "implicit conversions that may change the sign of an integer"},
        {"unused-function", false, "static functions that nothing uses"},
        {"unused-parameter", false, "parameters that the function body does not use"},
        {"unused-variable", false, "local variables that nothing uses"},
    };
    static_assert(std::size(table) == warning_count);
    return std::span<const WarningInfo, warning_count>(table);
}

std::optional<Warning> find_warning(std::string_view name) {
    const auto table = warning_table();
    for (std::size_t index = 0; index < table.size(); ++index)
        if (table[index].name == name) return static_cast<Warning>(index);
    return std::nullopt;
}

WarningPolicy::WarningPolicy() {
    const auto table = warning_table();
    for (std::size_t index = 0; index < table.size(); ++index)
        enabled[index] = table[index].enabled_by_default;
}

Diagnostics::Diagnostics(std::ostream& stream, std::string_view tool)
    : stream_(stream), tool_(tool) {}

bool Diagnostics::first_report(SourceLocation location, std::string_view message) {
    if (!location.valid()) return true;
    return reported_.emplace(location.file, location.offset, std::string(message)).second;
}

void Diagnostics::report(DiagnosticLevel level, SourceLocation location,
                         std::string_view message) {
    if (level != DiagnosticLevel::Warning) {
        emit(level, location, message);
        return;
    }
    if (policy_.inhibit || !first_report(location, message)) {
        suppress_notes_ = true;
        return;
    }
    if (policy_.all_errors) {
        promoted_ = true;
        emit(DiagnosticLevel::Error, location, std::string(message) + " [-Werror]");
        return;
    }
    emit(level, location, message);
}

void Diagnostics::warning(Warning warning, SourceLocation location,
                          std::string_view message) {
    const auto index = static_cast<std::size_t>(warning);
    if (!enabled(warning) || !first_report(location, message)) {
        suppress_notes_ = true;
        return;
    }
    const auto name = warning_name(warning);
    const bool error = policy_.error[index].value_or(policy_.all_errors);
    if (error && !policy_.error[index]) promoted_ = true;
    emit(error ? DiagnosticLevel::Error : DiagnosticLevel::Warning, location,
         std::string(message) + (error ? " [-Werror=" : " [-W") + std::string(name) + "]");
}

void Diagnostics::emit(DiagnosticLevel level, SourceLocation location,
                       std::string_view message) {
    const char* label = "note";
    if (level == DiagnosticLevel::Note) {
        // The notes of a warning that was not reported go with it.
        if (suppress_notes_) return;
    } else {
        suppress_notes_ = false;
    }
    if (level == DiagnosticLevel::Warning) {
        label = "warning";
        ++warnings_;
    } else if (level == DiagnosticLevel::Error) {
        label = "error";
        ++errors_;
    }

    const auto emit_line = [&](SourceLocation item, std::string_view item_label,
                               std::string_view item_message) {
        if (!item.valid()) {
            stream_ << tool_ << ": " << item_label << ": " << item_message
                    << '\n';
            return;
        }
        item = item.file->logical_location(item);
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

    emit_line(location, label, message);
    if (location.file && location.file->expansion_at(location.offset)) {
        if (const auto* token = location.file->token_origin_at(location.offset);
            token && token->span.valid() && token->identity.expansion.value == 0 &&
            (token->span.file != location.file || token->span.offset != location.offset)) {
            emit_line(token->span, "note", "token supplied from here");
        }
    }
    auto origin = location;
    for (unsigned depth = 0; origin.valid() && depth < 64; ++depth) {
        const auto* expansion = origin.file->expansion_at(origin.offset);
        if (!expansion) break;
        if (expansion->kind == SourceExpansion::Kind::SyntaxExtension) {
            emit_line(expansion->invocation, "note",
                      "in expansion of syntax '" + expansion->macro_name + "'");
            emit_line(expansion->definition, "note",
                      "syntax '" + expansion->macro_name + "' defined here");
            if (expansion->expander.valid())
                emit_line(expansion->expander, "note", "syntax expander defined here");
        } else {
            emit_line(expansion->invocation, "note",
                      "in expansion of procedural macro '" +
                          expansion->macro_name + "'");
            emit_line(expansion->definition, "note",
                      "procedural macro '" + expansion->macro_name +
                          "' defined here");
        }
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

void Diagnostics::command_note(std::string_view message) {
    suppress_notes_ = false;
    note({}, message);
}

void Diagnostics::file_error(std::string_view position,
                             std::string_view message) {
    ++errors_;
    suppress_notes_ = false;
    stream_ << position << ": error: " << message << '\n';
}

} // namespace cross
