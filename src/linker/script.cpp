// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/script.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace cross::linker::script {
namespace {

bool symbol_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}

bool symbol_char(char c) {
    return symbol_start(c) || std::isdigit(static_cast<unsigned char>(c));
}

// Characters of file names, section names, and wildcard patterns.
bool name_char(char c) {
    if (std::isalnum(static_cast<unsigned char>(c))) return true;
    switch (c) {
    case '_': case '.': case '$': case '/': case '\\': case '~': case '*': case '?':
    case '[': case ']': case '!': case '^': case '-': case '+': case '@': case '#':
        return true;
    default:
        return false;
    }
}

struct PhdrType {
    std::string_view name;
    std::uint32_t value;
};

constexpr std::array phdr_types{
    PhdrType{"PT_NULL", 0},          PhdrType{"PT_LOAD", 1},
    PhdrType{"PT_DYNAMIC", 2},       PhdrType{"PT_INTERP", 3},
    PhdrType{"PT_NOTE", 4},          PhdrType{"PT_SHLIB", 5},
    PhdrType{"PT_PHDR", 6},          PhdrType{"PT_TLS", 7},
    PhdrType{"PT_GNU_EH_FRAME", 0x6474e550}, PhdrType{"PT_GNU_STACK", 0x6474e551},
    PhdrType{"PT_GNU_RELRO", 0x6474e552},
};

class Parser {
public:
    Parser(const SourceFile& file, Script& script, const std::vector<std::filesystem::path>& search,
           SourceManager& manager, Diagnostics& diagnostics, unsigned depth)
        : file_(file), text_(file.text), script_(script), search_(search), manager_(manager),
          diagnostics_(diagnostics), depth_(depth) {}

    bool run(bool in_sections) { return commands(in_sections, false) && !failed_; }

    bool definition() {
        auto assignment = try_assignment();
        if (!assignment || !at_end()) {
            if (!failed_) error(0, "expected SYMBOL=EXPRESSION");
            return false;
        }
        script_.commands.insert(script_.commands.begin(), std::move(*assignment));
        return !failed_;
    }

private:
    SourceLocation location(std::size_t offset) const {
        const auto& starts = file_.line_starts;
        const auto line = std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin();
        SourceLocation result;
        result.file = &file_;
        result.offset = offset;
        result.line = static_cast<unsigned>(line);
        result.column = static_cast<unsigned>(offset - starts[static_cast<std::size_t>(line) - 1] + 1);
        return result;
    }

    void error(std::size_t offset, std::string_view message) {
        if (failed_) return;
        failed_ = true;
        diagnostics_.error(location(offset), message);
    }

    void skip() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
                const auto end = text_.find("*/", pos_ + 2);
                if (end == std::string::npos) {
                    error(pos_, "unterminated comment");
                    pos_ = text_.size();
                    return;
                }
                pos_ = end + 2;
            } else {
                return;
            }
        }
    }

    bool at_end() {
        skip();
        return pos_ >= text_.size();
    }

    char peek() {
        skip();
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    char peek_next() const { return pos_ + 1 < text_.size() ? text_[pos_ + 1] : '\0'; }

    bool consume(std::string_view token) {
        skip();
        if (text_.compare(pos_, token.size(), token) != 0) return false;
        pos_ += token.size();
        return true;
    }

    bool expect(std::string_view token) {
        if (consume(token)) return true;
        error(pos_, "expected '" + std::string(token) + "'");
        return false;
    }

    // A keyword, which must not continue as a name.
    bool keyword(std::string_view word) {
        skip();
        if (text_.compare(pos_, word.size(), word) != 0) return false;
        const auto end = pos_ + word.size();
        if (end < text_.size() && name_char(text_[end])) return false;
        pos_ = end;
        return true;
    }

    std::optional<std::string> quoted() {
        if (peek() != '"') return std::nullopt;
        const auto end = text_.find('"', pos_ + 1);
        if (end == std::string::npos) {
            error(pos_, "unterminated string");
            pos_ = text_.size();
            return std::string();
        }
        std::string result = text_.substr(pos_ + 1, end - pos_ - 1);
        pos_ = end + 1;
        return result;
    }

    // A file name, section name, or pattern; empty when none is next.
    std::string word() {
        if (auto text = quoted()) return *text;
        const auto start = pos_;
        while (pos_ < text_.size() && name_char(text_[pos_])) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    std::string required_word(std::string_view what) {
        const auto start = pos_;
        auto result = word();
        if (result.empty()) error(start, "expected " + std::string(what));
        return result;
    }

    std::optional<std::string> symbol_name() {
        if (auto text = quoted()) return text;
        if (!symbol_start(peek())) return std::nullopt;
        const auto start = pos_;
        while (pos_ < text_.size() && symbol_char(text_[pos_])) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    std::optional<std::string> assignment_operator() {
        skip();
        static constexpr std::array operators{"<<=", ">>=", "+=", "-=", "*=", "/=", "&=", "|="};
        for (const std::string_view op : operators) {
            if (text_.compare(pos_, op.size(), op) == 0) {
                pos_ += op.size();
                return std::string(op);
            }
        }
        if (pos_ < text_.size() && text_[pos_] == '=' && peek_next() != '=') {
            ++pos_;
            return std::string("=");
        }
        return std::nullopt;
    }

    std::optional<Assignment> try_assignment() {
        const auto start = pos_;
        Assignment assignment;
        assignment.location = location(start);
        bool wrapped = false;
        if (keyword("PROVIDE_HIDDEN")) {
            assignment.provide = assignment.hidden = wrapped = true;
        } else if (keyword("PROVIDE")) {
            assignment.provide = wrapped = true;
        } else if (keyword("HIDDEN")) {
            assignment.hidden = wrapped = true;
        }
        if (wrapped && !expect("(")) return std::nullopt;
        skip();
        assignment.location = location(pos_);
        auto name = symbol_name();
        if (!name) {
            if (wrapped) error(pos_, "expected a symbol name");
            pos_ = start;
            return std::nullopt;
        }
        auto op = assignment_operator();
        if (!op) {
            if (wrapped) error(pos_, "expected '='");
            pos_ = start;
            return std::nullopt;
        }
        if (wrapped && (*op != "=" || *name == ".")) {
            error(assignment.location.offset, "PROVIDE and HIDDEN take a plain symbol assignment");
            return std::nullopt;
        }
        assignment.symbol = std::move(*name);
        assignment.op = std::move(*op);
        assignment.value = expression();
        if (!assignment.value) return std::nullopt;
        if (wrapped && !expect(")")) return std::nullopt;
        return assignment;
    }

    bool end_statement() {
        if (consume(";") || consume(",")) return true;
        error(pos_, "expected ';'");
        return false;
    }

    std::optional<Assert> assertion(std::size_t start) {
        Assert result;
        result.location = location(start);
        if (!expect("(")) return std::nullopt;
        result.condition = expression();
        if (!result.condition || !expect(",")) return std::nullopt;
        auto message = quoted();
        if (!message) {
            result.message = required_word("an assertion message");
        } else {
            result.message = std::move(*message);
        }
        if (!expect(")")) return std::nullopt;
        consume(";");
        return result;
    }

    // The script an INCLUDE names: the current directory, the -L and
    // SEARCH_DIR directories, then the including script's directory.
    const SourceFile* included_file(std::size_t start) {
        skip();
        const auto name = required_word("a file name");
        if (failed_) return nullptr;
        if (depth_ >= 16) {
            error(start, "INCLUDE nests too deeply");
            return nullptr;
        }
        std::vector<std::filesystem::path> candidates{name};
        for (const auto& directory : search_) candidates.push_back(directory / name);
        for (const auto& directory : script_.search_directories) candidates.push_back(directory / name);
        candidates.push_back(file_.path.parent_path() / name);
        for (const auto& candidate : candidates) {
            std::error_code code;
            if (!std::filesystem::is_regular_file(candidate, code)) continue;
            std::string message;
            const auto* source = manager_.load(candidate, message);
            if (!source) error(start, message);
            return source;
        }
        error(start, "cannot find included script '" + name + "'");
        return nullptr;
    }

    bool include(bool in_sections) {
        const auto start = pos_;
        const auto* source = included_file(start);
        if (!source) return false;
        Parser nested(*source, script_, search_, manager_, diagnostics_, depth_ + 1);
        if (!nested.commands(in_sections, true) || nested.failed_) {
            failed_ = true;
            return false;
        }
        return true;
    }

    bool commands(bool in_sections, bool included) {
        while (!failed_) {
            if (at_end()) {
                if (in_sections && !included) {
                    error(pos_, "expected '}'");
                    return false;
                }
                return true;
            }
            if (in_sections && !included && consume("}")) return true;
            if (consume(";")) continue;
            const auto start = pos_;
            if (keyword("ENTRY")) {
                if (!expect("(")) return false;
                script_.entry = required_word("a symbol name");
                if (!expect(")")) return false;
                consume(";");
                continue;
            }
            if (keyword("ASSERT")) {
                auto result = assertion(start);
                if (!result) return false;
                script_.commands.emplace_back(std::move(*result));
                continue;
            }
            if (keyword("INCLUDE")) {
                if (!include(in_sections)) return false;
                continue;
            }
            if (!in_sections) {
                if (keyword("SECTIONS")) {
                    if (!expect("{")) return false;
                    script_.has_sections = true;
                    if (!commands(true, false)) return false;
                    continue;
                }
                if (keyword("OUTPUT_ARCH")) {
                    if (!expect("(")) return false;
                    required_word("an architecture");
                    if (!expect(")")) return false;
                    continue;
                }
                if (keyword("OUTPUT_FORMAT")) {
                    if (!expect("(")) return false;
                    do {
                        script_.output_formats.push_back(required_word("an output format"));
                    } while (!failed_ && consume(","));
                    if (!expect(")")) return false;
                    continue;
                }
                if (keyword("SEARCH_DIR")) {
                    if (!expect("(")) return false;
                    script_.search_directories.emplace_back(required_word("a directory"));
                    if (!expect(")")) return false;
                    continue;
                }
                if (keyword("EXTERN")) {
                    if (!expect("(")) return false;
                    while (!failed_ && !consume(")")) {
                        script_.externs.push_back(required_word("a symbol name"));
                        consume(",");
                    }
                    continue;
                }
                if (keyword("PHDRS")) {
                    if (!phdrs()) return false;
                    continue;
                }
                if (keyword("MEMORY")) {
                    if (!memory()) return false;
                    continue;
                }
                if (keyword("INPUT") || keyword("GROUP")) {
                    if (!input_list()) return false;
                    continue;
                }
                if (keyword("REGION_ALIAS")) {
                    if (!region_alias(start)) return false;
                    continue;
                }
                if (keyword("INSERT")) {
                    Insert insert;
                    insert.location = location(start);
                    if (keyword("AFTER")) {
                        insert.before = false;
                    } else if (keyword("BEFORE")) {
                        insert.before = true;
                    } else {
                        error(pos_, "expected AFTER or BEFORE");
                        return false;
                    }
                    skip();
                    insert.section = required_word("an output section name");
                    if (failed_) return false;
                    script_.commands.emplace_back(std::move(insert));
                    script_.has_insert = true;
                    continue;
                }
                const bool to = keyword("NOCROSSREFS_TO");
                if (to || keyword("NOCROSSREFS")) {
                    CrossReferences references;
                    references.to = to;
                    references.location = location(start);
                    if (!expect("(") || !names(references.sections)) return false;
                    if (references.sections.size() < (to ? 2U : 1U)) {
                        error(start, to ? "NOCROSSREFS_TO needs two or more sections"
                                        : "NOCROSSREFS needs a section");
                        return false;
                    }
                    script_.cross_references.push_back(std::move(references));
                    continue;
                }
            }
            if (auto assignment = try_assignment()) {
                if (!end_statement()) return false;
                script_.commands.emplace_back(std::move(*assignment));
                continue;
            }
            if (failed_) return false;
            if (in_sections) {
                if (!output_section()) return false;
                continue;
            }
            const auto name = word();
            error(start, name.empty() ? std::string("unexpected character in linker script")
                                      : "unsupported linker script command '" + name + "'");
            return false;
        }
        return false;
    }

    // Whether `(TYPE)` follows: a parenthesized section type, or `()`.
    bool type_follows() {
        if (peek() != '(') return false;
        const auto save = pos_;
        ++pos_;
        skip();
        bool result = peek() == ')';
        if (!result) {
            const auto type = word();
            skip();
            result = (type == "NOLOAD" || type == "COPY" || type == "INFO" || type == "OVERLAY" ||
                      type == "DSECT" || type == "READONLY" || type == "TYPE") &&
                     (peek() == ')' || (type == "READONLY" && peek() == '(') ||
                      (type == "TYPE" && peek() == '='));
        }
        pos_ = save;
        return result;
    }

    bool section_type(OutputSection& section) {
        consume("(");
        if (consume(")")) return true;
        skip();
        const auto start = pos_;
        const auto type = word();
        if (type == "NOLOAD") {
            section.kind = OutputSection::Kind::NoLoad;
        } else if (type == "DSECT" || type == "COPY" || type == "INFO" || type == "OVERLAY") {
            section.kind = OutputSection::Kind::NoAlloc;
        } else if (type == "READONLY") {
            section.readonly = true;
            if (consume("(")) {
                skip();
                if (!keyword("TYPE") || !type_value(section) || !expect(")")) {
                    if (!failed_) error(pos_, "expected TYPE = ...");
                    return false;
                }
            }
        } else if (type == "TYPE") {
            if (!type_value(section)) return false;
        } else {
            error(start, "unsupported output section type '" + type + "'");
            return false;
        }
        return expect(")");
    }

    // `= TYPE` after the TYPE keyword: an ELF section type name or number.
    bool type_value(OutputSection& section) {
        if (!expect("=")) return false;
        section.type = expression();
        return section.type != nullptr;
    }

    // The attributes between ':' and '{': AT, ALIGN, ALIGN_WITH_INPUT,
    // SUBALIGN, and ONLY_IF_RO or ONLY_IF_RW.
    bool section_attributes(OutputSection& section) {
        while (true) {
            skip();
            const auto attribute = pos_;
            if (keyword("AT")) {
                if (!expect("(")) return false;
                section.load_address = expression();
                if (!section.load_address || !expect(")")) return false;
            } else if (keyword("ALIGN_WITH_INPUT")) {
                section.align_with_input = true;
            } else if (keyword("ALIGN")) {
                if (!expect("(")) return false;
                section.alignment = expression();
                if (!section.alignment || !expect(")")) return false;
            } else if (keyword("SUBALIGN")) {
                if (!expect("(")) return false;
                section.subalign = expression();
                if (!section.subalign || !expect(")")) return false;
            } else if (keyword("ONLY_IF_RO")) {
                section.constraint = OutputSection::Constraint::ReadOnly;
            } else if (keyword("ONLY_IF_RW")) {
                section.constraint = OutputSection::Constraint::ReadWrite;
            } else if (keyword("SPECIAL")) {
                error(attribute, "unsupported output section attribute");
                return false;
            } else {
                return true;
            }
        }
    }

    // `:PHDR` names and `=FILL` after a section's closing brace.
    bool phdrs_and_fill(std::vector<std::string>& phdrs, std::optional<Fill>& fill) {
        while (true) {
            if (peek() == ':') {
                ++pos_;
                skip();
                phdrs.push_back(required_word("a program header name"));
            } else if (peek() == '=' && peek_next() != '=') {
                ++pos_;
                skip();
                Fill value;
                value.location = location(pos_);
                value.value = expression();
                if (!value.value) return false;
                fill = std::move(value);
            } else {
                return true;
            }
            if (failed_) return false;
        }
    }

    // `>REGION` and `AT>REGION`.
    bool regions(std::string& region, std::string& load_region) {
        while (true) {
            if (consume(">")) {
                region = required_word("a memory region");
            } else if (keyword("AT")) {
                if (!expect(">")) return false;
                load_region = required_word("a memory region");
            } else {
                return true;
            }
            if (failed_) return false;
        }
    }

    bool output_section() {
        OutputSection section;
        section.location = location(pos_);
        const auto start = pos_;
        if (keyword("OVERLAY")) return overlay(start);
        section.name = required_word("an output section name");
        if (failed_) return false;
        if (peek() == '{') {
            error(pos_, "expected ':'");
            return false;
        }
        if (peek() != ':') {
            if (type_follows()) {
                if (!section_type(section)) return false;
            } else {
                section.address = expression();
                if (!section.address) return false;
                if (type_follows() && !section_type(section)) return false;
            }
        }
        if (!expect(":") || !section_attributes(section)) return false;
        if (!expect("{") || !section_items(section.items)) return false;
        while (true) {
            const auto before = pos_;
            if (!regions(section.region, section.load_region)) return false;
            if (!phdrs_and_fill(section.phdrs, section.fill)) return false;
            if (pos_ == before) break;
        }
        if (section.load_address && !section.load_region.empty()) {
            error(start, "section has both a load address and a load region");
            return false;
        }
        consume(",");
        script_.commands.emplace_back(std::move(section));
        return true;
    }

    ExpressionPtr section_function(std::string function, const std::string& section,
                                   const SourceLocation& where) {
        auto result = std::make_unique<Expression>();
        result->kind = Expression::Kind::Function;
        result->name = std::move(function);
        result->location = where;
        auto argument = std::make_unique<Expression>();
        argument->kind = Expression::Kind::Symbol;
        argument->name = section;
        argument->location = where;
        result->operands.push_back(std::move(argument));
        return result;
    }

    // OVERLAY becomes ordinary output sections, as in GNU ld: the first
    // at the overlay's address and the others at the first one's, each
    // loaded after the previous one, with __load_start_NAME and
    // __load_stop_NAME, and the location counter past the largest.
    bool overlay(std::size_t start) {
        const auto where = location(start);
        ExpressionPtr address;
        if (peek() != ':') {
            address = expression();
            if (!address) return false;
        }
        if (!expect(":")) return false;
        const bool nocrossrefs = keyword("NOCROSSREFS");
        ExpressionPtr load_address;
        ExpressionPtr subalign;
        while (true) {
            if (keyword("AT")) {
                if (!expect("(")) return false;
                load_address = expression();
                if (!load_address || !expect(")")) return false;
            } else if (keyword("SUBALIGN")) {
                if (!expect("(")) return false;
                subalign = expression();
                if (!subalign || !expect(")")) return false;
            } else {
                break;
            }
        }
        if (!expect("{")) return false;
        std::vector<OutputSection> sections;
        while (!failed_ && !consume("}")) {
            if (at_end()) {
                error(pos_, "expected '}'");
                return false;
            }
            OutputSection section;
            section.location = location(pos_);
            section.name = required_word("an overlay section name");
            if (failed_ || !expect("{") || !section_items(section.items)) return false;
            if (!phdrs_and_fill(section.phdrs, section.fill)) return false;
            consume(",");
            sections.push_back(std::move(section));
        }
        if (failed_) return false;
        std::string region;
        std::string load_region;
        std::vector<std::string> phdrs;
        std::optional<Fill> fill;
        while (true) {
            const auto before = pos_;
            if (!regions(region, load_region) || !phdrs_and_fill(phdrs, fill)) return false;
            if (pos_ == before) break;
        }
        consume(",");
        if (load_address && !load_region.empty()) {
            error(start, "section has both a load address and a load region");
            return false;
        }
        if (sections.empty()) return true;
        // Without a run-time region, the load region is also the run-time one.
        if (region.empty()) region = load_region;
        const std::string first = sections.front().name;
        ExpressionPtr largest;
        CrossReferences references;
        references.location = where;
        for (std::size_t index = 0; index < sections.size(); ++index) {
            auto& section = sections[index];
            auto size = section_function("SIZEOF", section.name, section.location);
            if (!largest) {
                largest = std::move(size);
            } else {
                auto both = std::make_unique<Expression>();
                both->kind = Expression::Kind::Function;
                both->name = "MAX";
                both->location = section.location;
                both->operands.push_back(std::move(largest));
                both->operands.push_back(std::move(size));
                largest = std::move(both);
            }
            references.sections.push_back(section.name);
        }
        for (std::size_t index = 0; index < sections.size(); ++index) {
            auto& section = sections[index];
            const auto here = section.location;
            if (index == 0) {
                section.overlay = OutputSection::Overlay::First;
                section.address = std::move(address);
                section.load_address = std::move(load_address);
            } else {
                section.overlay = OutputSection::Overlay::Later;
                section.address = section_function("ADDR", first, here);
            }
            if (subalign) section.subalign = clone(*subalign);
            section.region = region;
            section.load_region = load_region;
            if (section.phdrs.empty()) section.phdrs = phdrs;
            if (!section.fill && fill) section.fill = Fill{clone(*fill->value), fill->location};
            if (index + 1 == sections.size()) {
                section.update_dot = binary("+", section_function("ADDR", first, here),
                                            std::move(largest));
            }
            std::string clean;
            for (const char c : section.name) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') clean += c;
            }
            const auto name = section.name;
            script_.commands.emplace_back(std::move(section));
            for (const bool stop : {false, true}) {
                Assignment symbol;
                symbol.symbol = (stop ? "__load_stop_" : "__load_start_") + clean;
                symbol.op = "=";
                symbol.provide = true;
                symbol.location = here;
                symbol.value = section_function("LOADADDR", name, here);
                if (stop) symbol.value = binary("+", std::move(symbol.value), section_function("SIZEOF", name, here));
                script_.commands.emplace_back(std::move(symbol));
            }
        }
        if (nocrossrefs) script_.cross_references.push_back(std::move(references));
        return true;
    }

    static ExpressionPtr clone(const Expression& expression) {
        auto result = std::make_unique<Expression>();
        result->kind = expression.kind;
        result->number = expression.number;
        result->digits = expression.digits;
        result->name = expression.name;
        result->op = expression.op;
        result->location = expression.location;
        for (const auto& operand : expression.operands) result->operands.push_back(clone(*operand));
        return result;
    }

    // Names up to a closing parenthesis.
    bool names(std::vector<std::string>& into) {
        while (!failed_) {
            if (consume(")")) return true;
            const auto start = pos_;
            auto name = word();
            if (name.empty()) {
                error(start, at_end() ? "expected ')'" : "expected a name pattern");
                return false;
            }
            into.push_back(std::move(name));
            consume(",");
        }
        return false;
    }

    std::optional<InputPattern> input_pattern() {
        InputPattern pattern;
        pattern.location = location(pos_);
        if (keyword("EXCLUDE_FILE")) {
            if (!expect("(") || !names(pattern.exclude_files)) return std::nullopt;
        }
        pattern.file = required_word("an input section description");
        if (failed_) return std::nullopt;
        if (!consume("(")) return pattern;
        while (!failed_ && !consume(")")) {
            const auto start = pos_;
            if (keyword("EXCLUDE_FILE")) {
                if (!expect("(") || !names(pattern.exclude_files)) return std::nullopt;
                continue;
            }
            auto sort = InputPattern::Sort::None;
            bool sorted = false;
            if (keyword("SORT") || keyword("SORT_BY_NAME")) {
                sort = InputPattern::Sort::Name;
                sorted = true;
            } else if (keyword("SORT_BY_ALIGNMENT")) {
                sort = InputPattern::Sort::Alignment;
                sorted = true;
            } else if (keyword("SORT_BY_INIT_PRIORITY")) {
                sort = InputPattern::Sort::InitPriority;
                sorted = true;
            } else if (keyword("SORT_NONE")) {
                sorted = true;
            }
            if (sorted) {
                pattern.sort = sort;
                if (!expect("(") || !names(pattern.sections)) return std::nullopt;
                continue;
            }
            auto name = word();
            if (name.empty()) {
                error(start, at_end() ? "expected ')'" : "expected a section name pattern");
                return std::nullopt;
            }
            pattern.sections.push_back(std::move(name));
            consume(",");
        }
        if (failed_) return std::nullopt;
        return pattern;
    }

    // Output section commands up to the closing brace (or, in an included
    // file, the end).
    bool section_items(std::vector<SectionItem>& items, bool included = false) {
        static constexpr std::array<std::pair<std::string_view, unsigned>, 5> data_commands{{
            {"BYTE", 1}, {"SHORT", 2}, {"LONG", 4}, {"QUAD", 8}, {"SQUAD", 8}}};
        while (!failed_) {
            if (included && at_end()) return true;
            if (consume("}")) return true;
            if (at_end()) {
                error(pos_, "expected '}'");
                return false;
            }
            if (consume(";")) continue;
            const auto start = pos_;
            if (keyword("ASSERT")) {
                auto result = assertion(start);
                if (!result) return false;
                items.emplace_back(std::move(*result));
                continue;
            }
            bool data = false;
            for (const auto& [name, size] : data_commands) {
                if (!keyword(name)) continue;
                Data item;
                item.size = size;
                item.location = location(start);
                if (!expect("(")) return false;
                item.value = expression();
                if (!item.value || !expect(")")) return false;
                consume(";");
                items.emplace_back(std::move(item));
                data = true;
                break;
            }
            if (data) continue;
            if (keyword("FILL")) {
                Fill fill;
                fill.location = location(start);
                if (!expect("(")) return false;
                fill.value = expression();
                if (!fill.value || !expect(")")) return false;
                consume(";");
                items.emplace_back(std::move(fill));
                continue;
            }
            // ELF has no constructor sets, so CONSTRUCTORS places nothing;
            // LINKER_VERSION places nothing without --enable-linker-version.
            if (keyword("CONSTRUCTORS") || keyword("LINKER_VERSION")) {
                consume(";");
                continue;
            }
            if (sorted_constructors()) continue;
            if (keyword("INCLUDE")) {
                if (!include_items(items)) return false;
                continue;
            }
            if (keyword("CREATE_OBJECT_SYMBOLS") || keyword("INPUT_SECTION_FLAGS") || keyword("ASCIZ")) {
                error(start, "unsupported output section command");
                return false;
            }
            if (keyword("KEEP")) {
                if (!expect("(")) return false;
                auto pattern = input_pattern();
                if (!pattern || !expect(")")) return false;
                pattern->keep = true;
                items.emplace_back(std::move(*pattern));
                continue;
            }
            if (auto assignment = try_assignment()) {
                if (!end_statement()) return false;
                items.emplace_back(std::move(*assignment));
                continue;
            }
            if (failed_) return false;
            auto pattern = input_pattern();
            if (!pattern) return false;
            items.emplace_back(std::move(*pattern));
        }
        return false;
    }

    // SORT(CONSTRUCTORS) or SORT_BY_NAME(CONSTRUCTORS), which places nothing.
    bool sorted_constructors() {
        const auto save = pos_;
        if (!(keyword("SORT") || keyword("SORT_BY_NAME")) || !consume("(") || !keyword("CONSTRUCTORS") ||
            !consume(")")) {
            pos_ = save;
            return false;
        }
        return true;
    }

    // INCLUDE inside an output section: the file's output section commands.
    bool include_items(std::vector<SectionItem>& items) {
        const auto start = pos_;
        const auto source = included_file(start);
        if (!source) return false;
        Parser nested(*source, script_, search_, manager_, diagnostics_, depth_ + 1);
        if (!nested.section_items(items, true) || nested.failed_) {
            failed_ = true;
            return false;
        }
        return true;
    }

    // INPUT or GROUP: files to link, `-lNAME` libraries, and AS_NEEDED lists.
    bool input_list() {
        if (!expect("(")) return false;
        int depth = 1;
        while (!failed_) {
            if (consume(")")) {
                if (--depth == 0) return true;
                continue;
            }
            if (consume(",")) continue;
            skip();
            const auto start = pos_;
            if (keyword("AS_NEEDED")) {
                if (!expect("(")) return false;
                ++depth;
                continue;
            }
            auto name = word();
            if (name.empty()) {
                error(start, at_end() ? "expected ')'" : "expected a file name");
                return false;
            }
            ScriptInput input;
            input.location = location(start);
            input.directory = file_.path.parent_path();
            if (name.starts_with("-l")) {
                input.library = true;
                name = name.substr(2);
            }
            input.name = std::move(name);
            script_.inputs.push_back(std::move(input));
        }
        return false;
    }

    bool region_alias(std::size_t start) {
        if (!expect("(")) return false;
        skip();
        const auto alias = required_word("a region alias");
        if (failed_ || !expect(",")) return false;
        skip();
        const auto target = required_word("a memory region name");
        if (failed_ || !expect(")")) return false;
        Region* found = nullptr;
        for (auto& region : script_.regions) {
            if (region.name == target ||
                std::find(region.aliases.begin(), region.aliases.end(), target) != region.aliases.end()) {
                found = &region;
            }
            if (region.name == alias ||
                std::find(region.aliases.begin(), region.aliases.end(), alias) != region.aliases.end()) {
                error(start, "redefinition of memory region alias '" + alias + "'");
                return false;
            }
        }
        if (!found) {
            error(start, "memory region '" + target + "' for alias '" + alias + "' does not exist");
            return false;
        }
        found->aliases.push_back(alias);
        return true;
    }

    bool phdrs() {
        if (!expect("{")) return false;
        while (!failed_ && !consume("}")) {
            if (consume(";")) continue;
            if (at_end()) {
                error(pos_, "expected '}'");
                return false;
            }
            Phdr phdr;
            phdr.location = location(pos_);
            phdr.name = required_word("a program header name");
            skip();
            const auto type_start = pos_;
            bool named = false;
            for (const auto& type : phdr_types) {
                if (keyword(type.name)) {
                    phdr.type = type.value;
                    named = true;
                    break;
                }
            }
            if (!named) {
                const auto type = expression();
                if (!type) return false;
                if (type->kind != Expression::Kind::Number) {
                    error(type_start, "expected a program header type");
                    return false;
                }
                phdr.type = static_cast<std::uint32_t>(type->number);
            }
            while (!failed_ && !consume(";")) {
                const auto start = pos_;
                if (keyword("FILEHDR")) {
                    phdr.file_header = true;
                } else if (keyword("PHDRS")) {
                    phdr.program_headers = true;
                } else if (keyword("AT")) {
                    if (!expect("(")) return false;
                    phdr.address = expression();
                    if (!phdr.address || !expect(")")) return false;
                } else if (keyword("FLAGS")) {
                    if (!expect("(")) return false;
                    phdr.flags = expression();
                    if (!phdr.flags || !expect(")")) return false;
                } else {
                    error(start, at_end() ? "expected ';'" : "unsupported program header attribute");
                    return false;
                }
            }
            script_.phdrs.push_back(std::move(phdr));
        }
        return !failed_;
    }

    bool memory() {
        if (!expect("{")) return false;
        while (!failed_ && !consume("}")) {
            if (at_end()) {
                error(pos_, "expected '}'");
                return false;
            }
            Region region;
            region.location = location(pos_);
            region.name = required_word("a memory region name");
            for (const auto& other : script_.regions) {
                if (other.name == region.name) {
                    error(region.location.offset, "redeclaration of memory region '" + region.name + "'");
                    return false;
                }
            }
            if (consume("(") && !region_attributes(region)) return false;
            if (!expect(":")) return false;
            if (!(keyword("ORIGIN") || keyword("org") || keyword("o"))) {
                error(pos_, "expected ORIGIN");
                return false;
            }
            if (!expect("=")) return false;
            region.origin = expression();
            if (!region.origin) return false;
            consume(",");
            if (!(keyword("LENGTH") || keyword("len") || keyword("l"))) {
                error(pos_, "expected LENGTH");
                return false;
            }
            if (!expect("=")) return false;
            region.length = expression();
            if (!region.length) return false;
            script_.regions.push_back(std::move(region));
        }
        return !failed_;
    }

    // A region's attributes up to ')': R, W, X, A, and I or L, where '!'
    // switches between required and refused for the rest of the name.
    bool region_attributes(Region& region) {
        while (!failed_) {
            if (consume(")")) return true;
            bool invert = consume("!");
            skip();
            const auto start = pos_;
            while (pos_ < text_.size() &&
                   (std::isalpha(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '!')) {
                const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text_[pos_])));
                std::uint8_t bit = 0;
                switch (c) {
                case '!': invert = !invert; break;
                case 'r': bit = region_readonly; break;
                case 'w': bit = region_writable; break;
                case 'x': bit = region_executable; break;
                case 'a': bit = region_allocated; break;
                case 'i': case 'l': bit = region_loaded; break;
                default:
                    error(pos_, std::string("invalid character '") + text_[pos_] +
                                    "' in memory region attributes");
                    return false;
                }
                (invert ? region.not_flags : region.flags) |= bit;
                ++pos_;
            }
            if (pos_ == start) {
                error(pos_, at_end() ? "expected ')'" : "expected memory region attributes");
                return false;
            }
        }
        return false;
    }

    // Expressions, with C precedence.

    ExpressionPtr make(Expression::Kind kind, std::size_t offset) {
        auto result = std::make_unique<Expression>();
        result->kind = kind;
        result->location = location(offset);
        return result;
    }

    ExpressionPtr binary(std::string op, ExpressionPtr left, ExpressionPtr right) {
        auto result = std::make_unique<Expression>();
        result->kind = Expression::Kind::Binary;
        result->location = left->location;
        result->op = std::move(op);
        result->operands.push_back(std::move(left));
        result->operands.push_back(std::move(right));
        return result;
    }

    ExpressionPtr expression() {
        auto condition = binary_level(0);
        if (!condition || !consume("?")) return condition;
        auto yes = expression();
        if (!yes || !expect(":")) return nullptr;
        auto no = expression();
        if (!no) return nullptr;
        auto result = std::make_unique<Expression>();
        result->kind = Expression::Kind::Conditional;
        result->location = condition->location;
        result->operands.push_back(std::move(condition));
        result->operands.push_back(std::move(yes));
        result->operands.push_back(std::move(no));
        return result;
    }

    // The binary operator of `level` that comes next, consumed; empty when
    // there is none.
    std::string binary_operator(int level) {
        static const std::vector<std::vector<std::string_view>> levels{
            {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<=", ">=", "<", ">"},
            {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        skip();
        for (const auto op : levels[static_cast<std::size_t>(level)]) {
            if (text_.compare(pos_, op.size(), op) != 0) continue;
            const char after = pos_ + op.size() < text_.size() ? text_[pos_ + op.size()] : '\0';
            // Do not split `||`, `&&`, `<<`, `>>`, or an assignment operator.
            if ((op == "|" && after == '|') || (op == "&" && after == '&') ||
                (op == "<" && after == '<') || (op == ">" && after == '>') ||
                (op.size() == 1 && after == '=' && op != "<" && op != ">") ||
                ((op == "<<" || op == ">>") && after == '=')) {
                continue;
            }
            pos_ += op.size();
            return std::string(op);
        }
        return {};
    }

    ExpressionPtr binary_level(int level) {
        if (level == 10) return unary();
        auto left = binary_level(level + 1);
        while (left) {
            auto op = binary_operator(level);
            if (op.empty()) break;
            auto right = binary_level(level + 1);
            if (!right) return nullptr;
            left = binary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    ExpressionPtr unary() {
        skip();
        const auto start = pos_;
        for (const std::string_view op : {"-", "~", "!", "+"}) {
            if (text_.compare(pos_, 1, op) != 0) continue;
            if (op == "!" && peek_next() == '=') break;
            ++pos_;
            auto operand = unary();
            if (!operand) return nullptr;
            auto result = make(Expression::Kind::Unary, start);
            result->op = std::string(op);
            result->operands.push_back(std::move(operand));
            return result;
        }
        return primary();
    }

    ExpressionPtr number() {
        const auto start = pos_;
        std::uint64_t value = 0;
        unsigned base = 10;
        if (text_[pos_] == '0' && pos_ + 1 < text_.size() &&
            (text_[pos_ + 1] == 'x' || text_[pos_ + 1] == 'X')) {
            base = 16;
            pos_ += 2;
        } else if (text_[pos_] == '0') {
            base = 8;
        }
        const auto digits = pos_;
        while (pos_ < text_.size()) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text_[pos_])));
            unsigned digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned>(c - '0');
            } else if (base == 16 && c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned>(c - 'a' + 10);
            } else {
                break;
            }
            if (digit >= base) {
                error(pos_, "invalid digit in number");
                return nullptr;
            }
            value = value * base + digit;
            ++pos_;
        }
        if (pos_ == digits && base == 16) {
            error(start, "expected hexadecimal digits");
            return nullptr;
        }
        if (pos_ < text_.size()) {
            const char suffix = text_[pos_];
            if (suffix == 'K' || suffix == 'k') {
                value *= 1024;
                ++pos_;
            } else if (suffix == 'M' || suffix == 'm') {
                value *= 1024 * 1024;
                ++pos_;
            }
        }
        if (pos_ < text_.size() && symbol_char(text_[pos_])) {
            error(start, "invalid number");
            return nullptr;
        }
        auto result = make(Expression::Kind::Number, start);
        result->number = value;
        if (base == 16 && std::isxdigit(static_cast<unsigned char>(text_[pos_ - 1]))) {
            result->digits = text_.substr(digits, pos_ - digits);
        }
        return result;
    }

    ExpressionPtr argument_name(std::string_view what) {
        skip();
        const auto start = pos_;
        auto name = required_word(what);
        if (failed_) return nullptr;
        auto result = make(Expression::Kind::Symbol, start);
        result->name = std::move(name);
        return result;
    }

    ExpressionPtr function(std::string name, std::size_t start) {
        auto result = make(Expression::Kind::Function, start);
        result->name = std::move(name);
        const auto& fn = result->name;
        if (fn == "SIZEOF_HEADERS") return result;
        if (!expect("(")) return nullptr;
        if (fn == "ADDR" || fn == "LOADADDR" || fn == "SIZEOF" || fn == "ALIGNOF") {
            auto argument = argument_name("a section name");
            if (!argument) return nullptr;
            result->operands.push_back(std::move(argument));
        } else if (fn == "ORIGIN" || fn == "LENGTH") {
            auto argument = argument_name("a memory region name");
            if (!argument) return nullptr;
            result->operands.push_back(std::move(argument));
        } else if (fn == "DEFINED") {
            skip();
            const auto at = pos_;
            auto symbol = symbol_name();
            if (!symbol) {
                error(at, "expected a symbol name");
                return nullptr;
            }
            auto argument = make(Expression::Kind::Symbol, at);
            argument->name = std::move(*symbol);
            result->operands.push_back(std::move(argument));
        } else if (fn == "CONSTANT") {
            auto argument = argument_name("MAXPAGESIZE or COMMONPAGESIZE");
            if (!argument) return nullptr;
            if (argument->name != "MAXPAGESIZE" && argument->name != "COMMONPAGESIZE") {
                error(argument->location.offset, "unknown constant '" + argument->name + "'");
                return nullptr;
            }
            result->operands.push_back(std::move(argument));
        } else if (fn == "SEGMENT_START") {
            auto argument = argument_name("a segment name");
            if (!argument || !expect(",")) return nullptr;
            result->operands.push_back(std::move(argument));
            auto fallback = expression();
            if (!fallback) return nullptr;
            result->operands.push_back(std::move(fallback));
        } else {
            static constexpr std::array<std::pair<std::string_view, std::pair<unsigned, unsigned>>, 10>
                arities{{{"ABSOLUTE", {1, 1}}, {"ALIGN", {1, 2}}, {"BLOCK", {1, 1}},
                         {"NEXT", {1, 1}}, {"MAX", {2, 2}}, {"MIN", {2, 2}},
                         {"LOG2CEIL", {1, 1}}, {"DATA_SEGMENT_ALIGN", {2, 2}},
                         {"DATA_SEGMENT_RELRO_END", {2, 2}}, {"DATA_SEGMENT_END", {1, 1}}}};
            const auto arity = std::find_if(arities.begin(), arities.end(),
                                            [&](const auto& entry) { return entry.first == fn; });
            if (arity == arities.end()) {
                error(start, "unsupported function '" + fn + "'");
                return nullptr;
            }
            do {
                auto argument = expression();
                if (!argument) return nullptr;
                result->operands.push_back(std::move(argument));
            } while (consume(","));
            const auto count = result->operands.size();
            if (count < arity->second.first || count > arity->second.second) {
                error(start, "wrong number of arguments to '" + fn + "'");
                return nullptr;
            }
        }
        if (!expect(")")) return nullptr;
        return result;
    }

    ExpressionPtr primary() {
        skip();
        const auto start = pos_;
        if (pos_ >= text_.size()) {
            error(pos_, "expected an expression");
            return nullptr;
        }
        const char c = text_[pos_];
        if (c == '(') {
            ++pos_;
            auto inner = expression();
            if (!inner || !expect(")")) return nullptr;
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) return number();
        if (c == '"') {
            auto result = make(Expression::Kind::Symbol, start);
            result->name = *quoted();
            return result;
        }
        if (auto name = symbol_name()) {
            if (*name == ".") return make(Expression::Kind::Dot, start);
            const bool upper = std::all_of(name->begin(), name->end(), [](char ch) {
                return std::isupper(static_cast<unsigned char>(ch)) || ch == '_' ||
                       std::isdigit(static_cast<unsigned char>(ch));
            });
            if (upper && (peek() == '(' || *name == "SIZEOF_HEADERS")) {
                return function(std::move(*name), start);
            }
            auto result = make(Expression::Kind::Symbol, start);
            result->name = std::move(*name);
            return result;
        }
        error(start, "expected an expression");
        return nullptr;
    }

    const SourceFile& file_;
    const std::string& text_;
    Script& script_;
    const std::vector<std::filesystem::path>& search_;
    SourceManager& manager_;
    Diagnostics& diagnostics_;
    unsigned depth_{};
    std::size_t pos_{};
    bool failed_{};
};

// Matches `c` against the class at pattern[index], a '['; on a match,
// leaves `index` at the class's closing ']'. A '[' without one is literal.
bool class_match(std::string_view pattern, std::size_t& index, char c) {
    std::size_t at = index + 1;
    bool negate = false;
    if (at < pattern.size() && (pattern[at] == '!' || pattern[at] == '^')) {
        negate = true;
        ++at;
    }
    bool matched = false;
    bool first = true;
    while (at < pattern.size() && (first || pattern[at] != ']')) {
        first = false;
        const char low = pattern[at];
        char high = low;
        if (at + 2 < pattern.size() && pattern[at + 1] == '-' && pattern[at + 2] != ']') {
            high = pattern[at + 2];
            at += 2;
        }
        if (c >= low && c <= high) matched = true;
        ++at;
    }
    if (at >= pattern.size()) return c == '[';
    index = at;
    return matched != negate;
}

} // namespace

bool parse(const SourceFile& file, Script& script,
           const std::vector<std::filesystem::path>& search, SourceManager& manager,
           Diagnostics& diagnostics) {
    Parser parser(file, script, search, manager, diagnostics, 0);
    return parser.run(false);
}

bool parse_definition(const SourceFile& file, Script& script, SourceManager& manager,
                      Diagnostics& diagnostics) {
    static const std::vector<std::filesystem::path> none;
    Parser parser(file, script, none, manager, diagnostics, 0);
    return parser.definition();
}

bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t resume = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = t;
            continue;
        }
        if (p < pattern.size()) {
            std::size_t next = p;
            bool matched = false;
            if (pattern[p] == '?') {
                matched = true;
            } else if (pattern[p] == '[') {
                matched = class_match(pattern, next, text[t]);
            } else {
                matched = pattern[p] == text[t];
            }
            if (matched) {
                p = next + 1;
                ++t;
                continue;
            }
        }
        if (star == std::string_view::npos) return false;
        p = star + 1;
        t = ++resume;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

} // namespace cross::linker::script
