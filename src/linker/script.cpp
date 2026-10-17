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

    bool include(bool in_sections) {
        const auto start = pos_;
        const auto name = required_word("a file name");
        if (failed_) return false;
        if (depth_ >= 16) {
            error(start, "INCLUDE nests too deeply");
            return false;
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
            if (!source) {
                error(start, message);
                return false;
            }
            Parser nested(*source, script_, search_, manager_, diagnostics_, depth_ + 1);
            if (!nested.commands(in_sections, true) || nested.failed_) {
                failed_ = true;
                return false;
            }
            return true;
        }
        error(start, "cannot find included script '" + name + "'");
        return false;
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

    bool type_follows() {
        if (peek() != '(') return false;
        const auto save = pos_;
        ++pos_;
        skip();
        const auto type = word();
        const bool result = !type.empty() && peek() == ')' &&
                            (type == "NOLOAD" || type == "COPY" || type == "INFO" ||
                             type == "OVERLAY" || type == "DSECT" || type == "READONLY");
        pos_ = save;
        return result;
    }

    bool section_type(OutputSection& section) {
        consume("(");
        const auto start = pos_;
        const auto type = word();
        if (!expect(")")) return false;
        if (type != "NOLOAD") {
            error(start, "unsupported output section type '" + type + "'");
            return false;
        }
        section.noload = true;
        return true;
    }

    bool output_section() {
        OutputSection section;
        section.location = location(pos_);
        const auto start = pos_;
        section.name = required_word("an output section name");
        if (failed_) return false;
        if (section.name == "OVERLAY") {
            error(start, "OVERLAY is not supported");
            return false;
        }
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
        if (!expect(":")) return false;
        while (true) {
            skip();
            const auto attribute = pos_;
            if (keyword("AT")) {
                if (!expect("(")) return false;
                section.load_address = expression();
                if (!section.load_address || !expect(")")) return false;
            } else if (keyword("ALIGN")) {
                if (!expect("(")) return false;
                section.alignment = expression();
                if (!section.alignment || !expect(")")) return false;
            } else if (keyword("SUBALIGN") || keyword("ONLY_IF_RO") || keyword("ONLY_IF_RW")) {
                error(attribute, "unsupported output section attribute");
                return false;
            } else {
                break;
            }
        }
        if (!expect("{") || !section_items(section)) return false;
        while (true) {
            if (consume(">")) {
                section.region = required_word("a memory region");
            } else if (keyword("AT")) {
                if (!expect(">")) return false;
                section.load_region = required_word("a memory region");
            } else if (peek() == ':') {
                ++pos_;
                skip();
                section.phdrs.push_back(required_word("a program header name"));
            } else if (peek() == '=' && peek_next() != '=') {
                ++pos_;
                section.fill = expression();
                if (!section.fill) return false;
            } else {
                break;
            }
            if (failed_) return false;
        }
        consume(",");
        script_.commands.emplace_back(std::move(section));
        return true;
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

    bool section_items(OutputSection& section) {
        static constexpr std::array<std::pair<std::string_view, unsigned>, 5> data_commands{{
            {"BYTE", 1}, {"SHORT", 2}, {"LONG", 4}, {"QUAD", 8}, {"SQUAD", 8}}};
        while (!failed_) {
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
                section.items.emplace_back(std::move(*result));
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
                section.items.emplace_back(std::move(item));
                data = true;
                break;
            }
            if (data) continue;
            if (keyword("FILL") || keyword("INCLUDE") || keyword("CONSTRUCTORS") ||
                keyword("CREATE_OBJECT_SYMBOLS") || keyword("INPUT_SECTION_FLAGS")) {
                error(start, "unsupported output section command");
                return false;
            }
            if (keyword("KEEP")) {
                if (!expect("(")) return false;
                auto pattern = input_pattern();
                if (!pattern || !expect(")")) return false;
                pattern->keep = true;
                section.items.emplace_back(std::move(*pattern));
                continue;
            }
            if (auto assignment = try_assignment()) {
                if (!end_statement()) return false;
                section.items.emplace_back(std::move(*assignment));
                continue;
            }
            if (failed_) return false;
            auto pattern = input_pattern();
            if (!pattern) return false;
            section.items.emplace_back(std::move(*pattern));
        }
        return false;
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
            if (consume("(")) {
                while (!failed_ && !consume(")")) {
                    if (word().empty()) {
                        error(pos_, "expected ')'");
                        return false;
                    }
                }
            }
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
            static constexpr std::array<std::pair<std::string_view, std::pair<unsigned, unsigned>>, 7>
                arities{{{"ABSOLUTE", {1, 1}}, {"ALIGN", {1, 2}}, {"BLOCK", {1, 1}},
                         {"NEXT", {1, 1}}, {"MAX", {2, 2}}, {"MIN", {2, 2}},
                         {"LOG2CEIL", {1, 1}}}};
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
