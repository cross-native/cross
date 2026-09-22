// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/source.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace cross {

SourceFile::SourceFile(std::filesystem::path source_path, std::string source_text,
                       std::vector<SourceExpansion> source_expansions,
                       std::vector<SourceTokenOrigin> source_token_origins)
    : path(std::move(source_path)), text(std::move(source_text)),
      expansions(std::move(source_expansions)),
      token_origins(std::move(source_token_origins)) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            line_starts.push_back(i + 1);
        }
    }
}

const TokenOrigin* SourceFile::token_origin_at(std::size_t offset) const {
    const auto found = std::lower_bound(token_origins.begin(), token_origins.end(), offset,
        [](const SourceTokenOrigin& origin, std::size_t position) {
            return origin.end <= position;
        });
    return found != token_origins.end() && found->begin <= offset
        ? &found->origin : nullptr;
}

TokenOrigin token_origin(SourceLocation location) {
    if (location.file) {
        if (const auto* origin = location.file->token_origin_at(location.offset)) return *origin;
    }
    return {location, {location.file, location.offset, {}, 0}, {}};
}

const SourceExpansion* SourceFile::expansion_at(std::size_t offset) const {
    const SourceExpansion* best{};
    for (const auto& expansion : expansions) {
        if (offset < expansion.begin || offset >= expansion.end) continue;
        if (!best ||
            expansion.end - expansion.begin < best->end - best->begin) {
            best = &expansion;
        }
    }
    return best;
}

std::string_view SourceFile::line(unsigned number) const {
    if (number == 0 || number > line_starts.size()) {
        return {};
    }
    const auto begin = line_starts[number - 1];
    auto end = text.find('\n', begin);
    if (end == std::string::npos) {
        end = text.size();
    }
    if (end > begin && text[end - 1] == '\r') {
        --end;
    }
    return std::string_view(text).substr(begin, end - begin);
}

const SourceFile* SourceManager::load(const std::filesystem::path& path,
                                      std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open input file '" + path.string() + "'";
        return nullptr;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error = "cannot read input file '" + path.string() + "'";
        return nullptr;
    }
    return add(path, buffer.str());
}

const SourceFile* SourceManager::add(std::filesystem::path path, std::string text) {
    return add(std::move(path), std::move(text), {});
}

const SourceFile* SourceManager::add(
    std::filesystem::path path, std::string text,
    std::vector<SourceExpansion> expansions,
    std::vector<SourceTokenOrigin> token_origins) {
    files_.push_back(std::make_unique<SourceFile>(
        std::move(path), std::move(text), std::move(expansions),
        std::move(token_origins)));
    return files_.back().get();
}

bool write_file(const std::filesystem::path& path, std::string_view text,
                std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open output file '" + path.string() + "'";
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        error = "cannot write output file '" + path.string() + "'";
        return false;
    }
    return true;
}

std::string quote_command_arg(std::string_view value) {
    const auto safe = [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '+' ||
               ch == '=' || ch == ',' || ch == '.' || ch == '/' || ch == '\\' ||
               ch == ':' || ch == '<' || ch == '>';
    };
    if (!value.empty() && std::all_of(value.begin(), value.end(), safe)) {
        return std::string(value);
    }
    std::string result = "\"";
    unsigned slashes = 0;
    for (char ch : value) {
        if (ch == '\\') {
            ++slashes;
            continue;
        }
        if (ch == '"') {
            result.append(slashes * 2 + 1, '\\');
            result.push_back('"');
            slashes = 0;
            continue;
        }
        result.append(slashes, '\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, '\\');
    result.push_back('"');
    return result;
}

} // namespace cross
