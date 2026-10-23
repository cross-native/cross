// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/link.hpp"

#include <algorithm>
#include <sstream>

namespace cross::linker {
namespace {

constexpr std::uint32_t pt_load = 1;
constexpr std::uint32_t pt_dynamic = 2;
constexpr std::uint32_t pt_tls = 7;
constexpr std::uint32_t pt_gnu_eh_frame = 0x6474e550;
constexpr std::uint32_t pt_gnu_stack = 0x6474e551;
constexpr std::uint32_t pf_x = 1;
constexpr std::uint32_t pf_w = 2;
constexpr std::uint32_t pf_r = 4;

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) / alignment * alignment;
}

std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

// A zero-filled TLS section takes no space in the image; each thread's
// block holds it (GNU ld's IS_TBSS).
bool tbss(const OutputSection& section) {
    return section.nobits() && (section.flags & elf::shf_tls) != 0;
}

// Orphan placement classes: code, read-only data, data, zero-filled data.
// TLS sections follow the data, where they stay together (GNU ld puts them
// after .tdata).
int section_class(std::uint64_t flags, bool nobits) {
    if ((flags & elf::shf_execinstr) != 0) return 0;
    if ((flags & elf::shf_tls) != 0) return 2;
    if ((flags & elf::shf_write) == 0) return 1;
    return nobits ? 3 : 2;
}

// The priority SORT_BY_INIT_PRIORITY orders by: the decimal suffix of
// `.init_array.N` and the like, inverted for `.ctors.N` and `.dtors.N`.
std::optional<std::uint64_t> init_priority(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : name.substr(dot + 1)) {
        if (c < '0' || c > '9' || value > 0x7fffffff) return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (dot == 6 && (name.starts_with(".ctors") || name.starts_with(".dtors"))) value = 65535 - value;
    if (value > 0x7fffffff) return std::nullopt;
    return value;
}

// ELF section type names that TYPE = accepts.
std::optional<std::uint32_t> section_type_name(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 7> names{{
        {"SHT_PROGBITS", elf::sht_progbits}, {"SHT_STRTAB", elf::sht_strtab}, {"SHT_NOTE", elf::sht_note},
        {"SHT_NOBITS", elf::sht_nobits}, {"SHT_INIT_ARRAY", elf::sht_init_array},
        {"SHT_FINI_ARRAY", elf::sht_fini_array}, {"SHT_PREINIT_ARRAY", elf::sht_preinit_array}}};
    for (const auto& [text, value] : names) {
        if (text == name) return value;
    }
    return std::nullopt;
}

// Whether a symbol name is `end` after leading underscores.
bool end_symbol(std::string_view name) {
    while (name.starts_with('_')) name.remove_prefix(1);
    return name == "end";
}

// GNU ld's assignments to `.` that do not keep an empty output section:
// `. = 0`, `. = . + 0`, and `. = ALIGN(. != 0 ? EXPRESSION : 1)`.
bool trivial_dot_assignment(const script::Assignment& assignment) {
    using Kind = script::Expression::Kind;
    const auto value = [](const script::Expression& expression, std::uint64_t number) {
        return expression.kind == Kind::Number && expression.number == number;
    };
    const auto& source = *assignment.value;
    if (assignment.op == "+=") return value(source, 0);
    if (assignment.op != "=") return false;
    if (value(source, 0)) return true;
    if (source.kind == Kind::Binary && source.op == "+") {
        return source.operands[0]->kind == Kind::Dot && value(*source.operands[1], 0);
    }
    if (source.kind != Kind::Function || source.name != "ALIGN" || source.operands.size() != 1) return false;
    const auto& choice = *source.operands[0];
    if (choice.kind != Kind::Conditional) return false;
    const auto& condition = *choice.operands[0];
    return condition.kind == Kind::Binary && condition.op == "!=" && condition.operands[0]->kind == Kind::Dot &&
           value(*condition.operands[1], 0) && value(*choice.operands[2], 1);
}

} // namespace

static bool matches_file(const script::InputPattern& pattern, const InputFile& file) {
    for (const auto& excluded : pattern.exclude_files) {
        if (script::glob_match(excluded, file.path)) return false;
    }
    const auto colon = pattern.file.find(':');
    if (colon == std::string::npos) return script::glob_match(pattern.file, file.path);
    // archive:member; an empty archive part matches files outside archives.
    const auto archive = std::string_view(pattern.file).substr(0, colon);
    const auto member = std::string_view(pattern.file).substr(colon + 1);
    if (archive.empty()) return file.member.empty() && script::glob_match(member, file.path);
    if (file.member.empty() || !script::glob_match(archive, file.path)) return false;
    return member.empty() || script::glob_match(member, file.member);
}

bool matches(const script::InputPattern& pattern, const InputSection& section) {
    if (!matches_file(pattern, *section.file)) return false;
    if (pattern.sections.empty()) return true;
    return std::any_of(pattern.sections.begin(), pattern.sections.end(), [&](const std::string& name) {
        return script::glob_match(name, section.name);
    });
}

OutputSection* Link::output_named(std::string_view name) const {
    for (const auto& output : outputs_) {
        if (output->name == name) return output.get();
    }
    return nullptr;
}

bool Link::place_sections() {
    auto candidates = included_sections();
    std::unordered_set<const InputSection*> assigned;
    for (const auto& command : script_.commands) {
        const auto* statement = std::get_if<script::OutputSection>(&command);
        if (!statement) {
            steps_.push_back({&command, nullptr});
            continue;
        }
        // ONLY_IF_RO and ONLY_IF_RW: a statement whose unassigned input
        // sections break the constraint is ignored, as if absent.
        if (statement->constraint != script::OutputSection::Constraint::None) {
            bool readonly = true;
            for (const auto& item : statement->items) {
                const auto* pattern = std::get_if<script::InputPattern>(&item);
                if (!pattern) continue;
                for (const auto* section : candidates) {
                    if (!assigned.contains(section) && matches(*pattern, *section) &&
                        (section->flags & elf::shf_write) != 0) {
                        readonly = false;
                    }
                }
            }
            if (readonly != (statement->constraint == script::OutputSection::Constraint::ReadOnly)) {
                steps_.push_back({&command, nullptr});
                continue;
            }
        }
        auto placement = std::make_unique<Placement>();
        placement->statement = statement;
        const bool discard = statement->name == "/DISCARD/";
        if (!discard) {
            auto output = std::make_unique<OutputSection>();
            output->name = statement->name;
            placement->output = output.get();
            outputs_.push_back(std::move(output));
        }
        for (const auto& item : statement->items) {
            Placement::Item entry;
            entry.item = &item;
            if (const auto* pattern = std::get_if<script::InputPattern>(&item)) {
                for (auto* section : candidates) {
                    if (assigned.contains(section) || !matches(*pattern, *section)) continue;
                    assigned.insert(section);
                    entry.sections.push_back(section);
                    if (pattern->keep) {
                        section->keep = true;
                        placement->keep = true;
                    }
                    if (discard) section->discarded = true;
                }
                using Sort = script::InputPattern::Sort;
                if (pattern->sort == Sort::Name) {
                    std::stable_sort(entry.sections.begin(), entry.sections.end(),
                                     [](const InputSection* a, const InputSection* b) {
                                         return a->name < b->name;
                                     });
                } else if (pattern->sort == Sort::Alignment) {
                    std::stable_sort(entry.sections.begin(), entry.sections.end(),
                                     [](const InputSection* a, const InputSection* b) {
                                         return a->alignment > b->alignment;
                                     });
                } else if (pattern->sort == Sort::InitPriority) {
                    std::stable_sort(entry.sections.begin(), entry.sections.end(),
                                     [](const InputSection* a, const InputSection* b) {
                                         const auto pa = init_priority(a->name);
                                         const auto pb = init_priority(b->name);
                                         if (pa && pb && *pa != *pb) return *pa < *pb;
                                         return a->name < b->name;
                                     });
                }
            }
            placement->items.push_back(std::move(entry));
        }
        steps_.push_back({&command, placement.get()});
        placements_.push_back(std::move(placement));
    }
    resolve_inserts();
    if (diagnostics_.errors() != 0) return false;
    std::vector<InputSection*> orphans;
    for (auto* section : candidates) {
        if (!assigned.contains(section)) orphans.push_back(section);
    }
    place_orphans(orphans);
    propagate_load_regions();
    return diagnostics_.errors() == 0;
}

void Link::resolve_inserts() {
    // GNU ld's INSERT: the statements before each INSERT move after the
    // named output section (before it, with BEFORE), ahead of the next
    // assignment to `.` that precedes the following output section.
    const auto output_statement = [](const Step& step) {
        return step.command && std::holds_alternative<script::OutputSection>(*step.command);
    };
    std::size_t start = 0;
    for (std::size_t index = 0; index < steps_.size(); ++index) {
        const auto* insert = steps_[index].command ? std::get_if<script::Insert>(steps_[index].command) : nullptr;
        if (!insert) continue;
        // The named section, outside the statements that move.
        std::optional<std::size_t> where;
        for (std::size_t other = index + 1; other < steps_.size(); ++other) {
            const auto& step = steps_[other];
            if (step.placement && step.placement->output && step.placement->output->name == insert->section) {
                where = other;
                break;
            }
        }
        if (!where) {
            diagnostics_.error(insert->location, "section '" + insert->section + "' not found for INSERT");
            return;
        }
        bool head = false;
        if (insert->before) {
            std::optional<std::size_t> previous;
            for (std::size_t other = index + 1; other < *where; ++other) {
                if (steps_[other].placement && steps_[other].placement->output) previous = other;
            }
            if (previous) {
                where = previous;
            } else {
                head = true;
            }
        }
        // The insertion point after `where` (or from the start of what
        // remains, ignoring its first assignment to `.`).
        std::size_t position = head ? index + 1 : *where + 1;
        std::optional<std::size_t> assignment;
        bool ignore_first = head;
        for (; position < steps_.size(); ++position) {
            const auto& step = steps_[position];
            if (const auto* value = step.command ? std::get_if<script::Assignment>(step.command) : nullptr;
                value && !step.placement) {
                if (!assignment && value->symbol == ".") {
                    if (!ignore_first) assignment = position;
                    ignore_first = false;
                }
                continue;
            }
            if (output_statement(step)) {
                const auto* output = step.placement ? step.placement->output : nullptr;
                if (assignment && (!output || !step.placement->created || output->alloc())) position = *assignment;
                break;
            }
        }
        std::vector<Step> moved(steps_.begin() + static_cast<std::ptrdiff_t>(start),
                                steps_.begin() + static_cast<std::ptrdiff_t>(index));
        steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(position), moved.begin(), moved.end());
        steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(start),
                     steps_.begin() + static_cast<std::ptrdiff_t>(index + 1));
        index = start;
        --index;
    }
}

void Link::place_orphans(std::vector<InputSection*>& orphans) {
    // Orphans of one name form one output section, after the last output
    // section of the same class (code, read-only data, data, zero-filled
    // data), or after the last of an earlier class; others go last.
    std::vector<std::pair<std::string, std::vector<InputSection*>>> groups;
    for (auto* section : orphans) {
        const std::string name = section->name == "COMMON" ? ".bss" : section->name;
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&](const auto& entry) { return entry.first == name; });
        if (group == groups.end()) {
            groups.emplace_back(name, std::vector<InputSection*>{});
            group = std::prev(groups.end());
        }
        group->second.push_back(section);
    }
    const auto placement_class = [](const Placement& placement) -> std::optional<int> {
        if (!placement.output) return std::nullopt;
        for (const auto& item : placement.items) {
            for (const auto* section : item.sections) {
                if (!section->alloc()) return std::nullopt;
                const bool nobits = section->nobits() ||
                                    (placement.statement &&
                                     placement.statement->kind == script::OutputSection::Kind::NoLoad);
                return section_class(section->flags, nobits);
            }
        }
        return std::nullopt;
    };
    for (auto& [name, sections] : groups) {
        Placement::Item item;
        item.sections = sections;
        auto existing = std::find_if(placements_.begin(), placements_.end(), [&](const auto& placement) {
            return placement->output && placement->output->name == name;
        });
        if (existing != placements_.end()) {
            (*existing)->items.push_back(std::move(item));
            continue;
        }
        auto output = std::make_unique<OutputSection>();
        output->name = name;
        auto placement = std::make_unique<Placement>();
        placement->output = output.get();
        placement->items.push_back(std::move(item));
        outputs_.push_back(std::move(output));
        auto position = steps_.end();
        const auto* first = sections.front();
        if (!first->alloc()) {
            // After .comment and the orphans already placed there, as in GNU
            // ld, or last.
            for (std::size_t index = 0; index < steps_.size(); ++index) {
                const auto* other = steps_[index].placement;
                if (other && other->output && other->output->name == ".comment") {
                    position = steps_.begin() + static_cast<std::ptrdiff_t>(index + 1);
                    while (position != steps_.end() && position->placement && !position->placement->statement &&
                           position->placement->output && !position->placement->items.empty() &&
                           !position->placement->items.front().sections.empty() &&
                           !position->placement->items.front().sections.front()->alloc()) {
                        ++position;
                    }
                }
            }
        } else {
            const int wanted = section_class(first->flags, first->nobits());
            std::optional<std::size_t> same;
            std::optional<std::size_t> earlier;
            std::optional<std::size_t> first_allocated;
            for (std::size_t index = 0; index < steps_.size(); ++index) {
                if (!steps_[index].placement) continue;
                const auto kind = placement_class(*steps_[index].placement);
                if (!kind) continue;
                if (!first_allocated) first_allocated = index;
                if (*kind == wanted) same = index;
                if (*kind < wanted) earlier = index;
            }
            // An orphan takes the regions of the section it follows.
            const auto follow = [&](std::size_t index) {
                if (const auto* previous = steps_[index].placement->statement) {
                    placement->region = previous->region.empty() && !previous->address
                                            ? previous->load_region
                                            : previous->region;
                    placement->explicit_load_region = previous->load_region;
                }
                position = steps_.begin() + static_cast<std::ptrdiff_t>(index + 1);
            };
            if (same) {
                follow(*same);
            } else if (earlier) {
                follow(*earlier);
            } else if (first_allocated) {
                position = steps_.begin() + static_cast<std::ptrdiff_t>(*first_allocated);
            }
        }
        steps_.insert(position, Step{nullptr, placement.get()});
        placements_.push_back(std::move(placement));
    }
}

void Link::propagate_load_regions() {
    // GNU ld carries an AT> region to a following section of the same
    // region that sets neither its address nor its load address.
    regions_.assign(script_.regions.size() + 1, {});
    const auto same_region = [&](const std::string& a, const std::string& b) {
        return a == b || (region_index(a) == region_index(b) && region_index(a) < regions_.size());
    };
    std::string previous_region;
    std::string previous_load;
    bool first = true;
    for (const auto& step : steps_) {
        const auto* statement = step.placement ? step.placement->statement
                                               : (step.command ? std::get_if<script::OutputSection>(step.command)
                                                               : nullptr);
        if (!step.placement && !statement) continue;
        // The region a statement names, or its AT> region without >REGION
        // or an address.
        std::string region = statement ? statement->region : step.placement->region;
        if (statement && region.empty() && !statement->address) region = statement->load_region;
        std::string load = statement ? statement->load_region : step.placement->explicit_load_region;
        bool fixed = statement && (statement->address || statement->load_address);
        if (step.placement && step.placement->output) {
            for (const auto& [name, start] : options_.section_starts) {
                if (name == step.placement->output->name) fixed = true;
            }
        }
        if (!first && load.empty() && !fixed && same_region(region, previous_region)) load = previous_load;
        if (step.placement) step.placement->load_region = load;
        previous_region = region;
        previous_load = load;
        first = false;
    }
    // Regions that MEMORY does not declare start at zero (GNU ld warns).
    std::vector<std::string> undeclared;
    const auto check = [&](const std::string& name, const SourceLocation& where) {
        if (name.empty() || region_index(name) != regions_.size() ||
            std::find(undeclared.begin(), undeclared.end(), name) != undeclared.end()) {
            return;
        }
        undeclared.push_back(name);
        diagnostics_.warning(where, "memory region '" + name + "' not declared");
    };
    regions_.assign(script_.regions.size() + 1, {});
    for (const auto& placement : placements_) {
        if (!placement->statement) continue;
        check(placement->statement->region, placement->statement->location);
        check(placement->load_region, placement->statement->location);
    }
    for (const auto& name : undeclared) {
        script::Region region;
        region.name = name;
        auto origin = std::make_unique<script::Expression>();
        origin->kind = script::Expression::Kind::Number;
        auto length = std::make_unique<script::Expression>();
        length->kind = script::Expression::Kind::Number;
        length->number = ~std::uint64_t{0};
        region.origin = std::move(origin);
        region.length = std::move(length);
        script_.regions.push_back(std::move(region));
    }
    regions_.assign(script_.regions.size() + 1, {});
}

std::size_t Link::region_index(std::string_view name) const {
    if (name.empty()) return regions_.size() - 1;
    for (std::size_t index = 0; index < script_.regions.size(); ++index) {
        const auto& region = script_.regions[index];
        if (region.name == name ||
            std::find(region.aliases.begin(), region.aliases.end(), name) != region.aliases.end()) {
            return index;
        }
    }
    return regions_.size();
}

std::size_t Link::default_region(const Placement& placement) const {
    // GNU ld's choice for a section without >REGION: the first region whose
    // attributes the section has one of and none of whose refused ones.
    auto attributes = placement.attributes;
    for (std::size_t index = 0; index < script_.regions.size(); ++index) {
        const auto& region = script_.regions[index];
        if ((region.flags & attributes) != 0 && (region.not_flags & attributes) == 0) return index;
    }
    return regions_.size() - 1;
}

void Link::expression_error(const script::Expression& expression, std::string_view message) {
    expression_failed_ = true;
    if (final_) diagnostics_.error(expression.location, message);
}

Link::Value Link::location_counter() {
    if (current_ && current_->output) return Value{dot_, Value::Kind::Relative, current_->output};
    from_dot_ = true;
    return Value{dot_, Value::Kind::Absolute, nullptr};
}

std::optional<Link::Value> Link::combine(std::string_view op, const Value& left, const Value& right,
                                         const script::Expression& where) {
    using V = Value::Kind;
    // GNU ld's rules, with a relative value as an offset into its section:
    // values of two sections are both made absolute; a plain number takes
    // the other value's section; arithmetic on values of one section gives
    // a plain number (absolute outside output sections), comparisons give
    // a plain number, and ALIGN, MAX, and MIN keep the section.
    auto a = raw(left);
    auto b = raw(right);
    Value section = left.kind == V::Number ? right : left;
    bool same = left.kind == right.kind && left.section == right.section;
    if (!same && left.kind != V::Number && right.kind != V::Number) {
        a = left.value;
        b = right.value;
        section = Value{0, V::Absolute, nullptr};
        same = true;
    }
    const auto in_section = [&](std::uint64_t value) {
        if (section.kind == V::Relative && section.section) value += section.section->address;
        return Value{value, section.kind, section.section};
    };
    const auto arithmetic = [&](std::uint64_t value) {
        if (same) return Value{value, current_ ? V::Number : V::Absolute, nullptr};
        return in_section(value);
    };
    const auto truth = [](bool value) { return Value{value ? 1U : 0U, V::Number, nullptr}; };
    if (op == "+") return arithmetic(a + b);
    if (op == "-") return arithmetic(a - b);
    if (op == "*") return arithmetic(a * b);
    if (op == "/" || op == "%") {
        if (b == 0) {
            expression_error(where, "division by zero");
            return std::nullopt;
        }
        const auto sa = static_cast<std::int64_t>(a);
        const auto sb = static_cast<std::int64_t>(b);
        return arithmetic(static_cast<std::uint64_t>(op == "/" ? sa / sb : sa % sb));
    }
    if (op == "<<") return arithmetic(b >= 64 ? 0 : a << b);
    if (op == ">>") return arithmetic(b >= 64 ? 0 : a >> b);
    if (op == "&") return arithmetic(a & b);
    if (op == "|") return arithmetic(a | b);
    if (op == "^") return arithmetic(a ^ b);
    if (op == "<") return truth(a < b);
    if (op == "<=") return truth(a <= b);
    if (op == ">") return truth(a > b);
    if (op == ">=") return truth(a >= b);
    if (op == "==") return truth(a == b);
    if (op == "!=") return truth(a != b);
    if (op == "&&") return truth(a != 0 && b != 0);
    if (op == "||") return truth(a != 0 || b != 0);
    if (op == "ALIGN") return in_section(align_up(a, b));
    if (op == "MAX") return in_section(std::max(a, b));
    if (op == "MIN") return in_section(std::min(a, b));
    expression_error(where, "unsupported operator '" + std::string(op) + "'");
    return std::nullopt;
}

std::optional<Link::Value> Link::evaluate(const script::Expression& expression) {
    using Kind = script::Expression::Kind;
    using V = Value::Kind;
    switch (expression.kind) {
    case Kind::Number:
        // A number is absolute outside output sections and plain inside.
        return Value{expression.number, current_ ? V::Number : V::Absolute, nullptr};
    case Kind::Dot:
        return location_counter();
    case Kind::Symbol: {
        const auto* target = find(expression.name);
        if (!target || !target->defined()) {
            expression_error(expression, "undefined symbol '" + expression.name +
                                             "' referenced in expression");
            return std::nullopt;
        }
        if (target->script && target->output && !target->absolute) {
            return Value{target->value, V::Relative, target->output};
        }
        if (!target->script && target->section) {
            return Value{section_address(*target->section, target->value), V::Relative, target->section->output};
        }
        // An absolute symbol is a plain number inside an output section.
        return Value{target->value, current_ ? V::Number : V::Absolute, nullptr};
    }
    case Kind::Unary: {
        auto operand = evaluate(*expression.operands[0]);
        if (!operand) return std::nullopt;
        // GNU ld applies the operator to the value (an offset for a relative
        // one) and keeps its section.
        const auto value = raw(*operand);
        auto result = value;
        if (expression.op == "-") result = ~value + 1;
        if (expression.op == "~") result = ~value;
        if (expression.op == "!") result = value == 0 ? 1U : 0U;
        operand->value += result - value;
        return operand;
    }
    case Kind::Conditional: {
        const auto condition = evaluate(*expression.operands[0]);
        if (!condition) return std::nullopt;
        return evaluate(*expression.operands[raw(*condition) != 0 ? 1 : 2]);
    }
    case Kind::Binary: {
        const auto left = evaluate(*expression.operands[0]);
        const auto right = evaluate(*expression.operands[1]);
        if (!left || !right) return std::nullopt;
        return combine(expression.op, *left, *right, expression);
    }
    case Kind::Function:
        break;
    }
    const auto& name = expression.name;
    const auto argument = [&](std::size_t index) { return evaluate(*expression.operands[index]); };
    const auto section = [&]() -> OutputSection* {
        const auto& section_name = expression.operands[0]->name;
        if (section_name == "NEXT_SECTION") {
            // The next output section statement that exists.
            for (std::size_t index = step_index_ + 1; index < steps_.size(); ++index) {
                const auto* placement = steps_[index].placement;
                if (placement && placement->output && placement->created) return placement->output;
            }
            return nullptr;
        }
        auto* output = output_named(section_name);
        if (!output) {
            expression_error(expression, "undefined section '" + section_name +
                                             "' referenced in expression");
        }
        return output;
    };
    if (name == "ALIGN" || name == "NEXT" || name == "BLOCK") {
        if (expression.operands.size() == 2) {
            const auto value = argument(0);
            const auto alignment = argument(1);
            if (!value || !alignment) return std::nullopt;
            auto result = combine("ALIGN", *value, *alignment, expression);
            if (result) result->value = mask(result->value);
            return result;
        }
        const auto alignment = argument(0);
        if (!alignment) return std::nullopt;
        if (name == "NEXT") return Value{mask(align_up(dot_, alignment->value)), Value::Kind::Absolute, nullptr};
        auto result = location_counter();
        result.value = mask(align_up(dot_, alignment->value));
        return result;
    }
    if (name == "ABSOLUTE") {
        const auto value = argument(0);
        if (!value) return std::nullopt;
        from_dot_ = false;
        return Value{value->value, Value::Kind::Absolute, nullptr};
    }
    if (name == "MAX" || name == "MIN") {
        const auto a = argument(0);
        const auto b = argument(1);
        if (!a || !b) return std::nullopt;
        return combine(name, *a, *b, expression);
    }
    if (name == "LOG2CEIL") {
        const auto value = argument(0);
        if (!value) return std::nullopt;
        std::uint64_t result = 0;
        while (result < 64 && (std::uint64_t{1} << result) < raw(*value)) ++result;
        return Value{result, Value::Kind::Number, nullptr};
    }
    if (name == "ADDR" || name == "LOADADDR" || name == "SIZEOF" || name == "ALIGNOF") {
        auto* output = section();
        if (!output) {
            if (expression.operands[0]->name == "NEXT_SECTION") {
                return Value{0, Value::Kind::Number, nullptr};
            }
            return std::nullopt;
        }
        if (name == "ADDR") return Value{output->address, Value::Kind::Relative, output};
        if (name == "LOADADDR") return Value{output->load_address, Value::Kind::Absolute, nullptr};
        if (name == "SIZEOF") return Value{output->size, Value::Kind::Number, nullptr};
        return Value{output->alignment, Value::Kind::Number, nullptr};
    }
    if (name == "SIZEOF_HEADERS") {
        uses_headers_size_ = true;
        return Value{headers_size(), Value::Kind::Number, nullptr};
    }
    if (name == "DEFINED") {
        const auto& symbol_name = expression.operands[0]->name;
        const auto* target = find(symbol_name);
        const bool defined = target && target->defined() &&
                             (!target->script || assigned_.contains(symbol_name));
        return Value{defined ? 1U : 0U, Value::Kind::Number, nullptr};
    }
    if (name == "ORIGIN" || name == "LENGTH") {
        const auto& region_name = expression.operands[0]->name;
        const auto index = region_index(region_name);
        if (index >= script_.regions.size()) {
            expression_error(expression, "undefined memory region '" + region_name + "'");
            return std::nullopt;
        }
        const auto& region = script_.regions[index];
        const auto value = evaluate(name == "ORIGIN" ? *region.origin : *region.length);
        if (!value) return std::nullopt;
        if (name == "LENGTH") return Value{value->value, Value::Kind::Number, nullptr};
        auto result = location_counter();
        result.value = value->value;
        return result;
    }
    if (name == "CONSTANT") {
        const bool maximum = expression.operands[0]->name == "MAXPAGESIZE";
        return Value{maximum ? page_size_ : std::min<std::uint64_t>(page_size_, 0x1000), Value::Kind::Number,
                     nullptr};
    }
    if (name == "SEGMENT_START") {
        const auto value = argument(1);
        if (!value) return std::nullopt;
        auto result = location_counter();
        result.value = value->value;
        return result;
    }
    if (name == "DATA_SEGMENT_ALIGN" || name == "DATA_SEGMENT_RELRO_END" || name == "DATA_SEGMENT_END") {
        if (current_) {
            expression_error(expression, name + " is only allowed outside output sections");
            return std::nullopt;
        }
        if (name == "DATA_SEGMENT_END") {
            const auto value = argument(0);
            if (!value) return std::nullopt;
            if (segment_phase_ == SegmentPhase::AlignSeen) {
                segment_phase_ = SegmentPhase::EndSeen;
                segment_end_ = value->value;
            }
            return value;
        }
        const auto first = argument(0);
        const auto second = argument(1);
        if (!first || !second) return std::nullopt;
        if (name == "DATA_SEGMENT_RELRO_END") {
            // Without -z relro, the expression (the second operand) itself.
            if (segment_phase_ == SegmentPhase::None && !segment_adjust_) {
                expression_error(expression, "DATA_SEGMENT_RELRO_END without DATA_SEGMENT_ALIGN");
                return std::nullopt;
            }
            return second;
        }
        // GNU ld's choice between the next page at the same offset and the
        // form that saves a common page.
        const auto maximum = first->value;
        const auto common = second->value;
        if (maximum == 0 || (maximum & (maximum - 1)) != 0 || common == 0 || (common & (common - 1)) != 0) {
            expression_error(expression, "DATA_SEGMENT_ALIGN takes powers of two");
            return std::nullopt;
        }
        if (segment_phase_ != SegmentPhase::None) {
            expression_error(expression, "DATA_SEGMENT_ALIGN is used more than once");
            return std::nullopt;
        }
        std::uint64_t result = align_up(dot_, maximum);
        if (segment_adjust_) {
            if (common < maximum) result += (dot_ + common - 1) & (maximum - common);
        } else {
            result += dot_ & (maximum - 1);
        }
        segment_phase_ = SegmentPhase::AlignSeen;
        segment_base_ = result;
        segment_common_page_ = common;
        return Value{mask(result), Value::Kind::Absolute, nullptr};
    }
    expression_error(expression, "unsupported function '" + name + "'");
    return std::nullopt;
}

OutputSection* Link::section_for_dot() const {
    // GNU ld's rule for a symbol assigned from `.` outside output sections:
    // the last allocated section entered, or after an assignment to `.` the
    // next existing one (unless that follows `_end`), each moved back to the
    // nearest allocated section in the output.
    const auto emitted = [&](std::size_t index) {
        const auto* placement = steps_[index].placement;
        return placement && placement->output && placement->emitted;
    };
    const auto statement = [&](std::size_t index) {
        return steps_[index].placement ||
               (steps_[index].command && std::holds_alternative<script::OutputSection>(*steps_[index].command));
    };
    // The nearest emitted allocated section at or before step `index`.
    const auto at_or_before = [&](std::size_t index) -> OutputSection* {
        for (std::size_t other = std::min(index + 1, steps_.size()); other-- > 0;) {
            if (emitted(other) && steps_[other].placement->output->alloc()) return steps_[other].placement->output;
        }
        return nullptr;
    };
    if (!current_section_ || prefer_next_) {
        std::optional<std::size_t> next;
        for (std::size_t index = step_index_ + 1; index < steps_.size(); ++index) {
            if (statement(index) && (after_end_[index] || emitted(index))) {
                next = index;
                break;
            }
        }
        if (!current_section_ || !next || !after_end_[*next]) {
            if (!next) return at_or_before(steps_.size() - 1);
            if (!emitted(*next)) return nullptr;
            return at_or_before(*next);
        }
    }
    std::size_t current = 0;
    while (current < steps_.size() && steps_[current].placement != current_section_) ++current;
    if (auto* section = at_or_before(current)) return section;
    for (std::size_t index = 0; index < steps_.size(); ++index) {
        if (emitted(index) && steps_[index].placement->output->alloc()) return steps_[index].placement->output;
    }
    return nullptr;
}

const OutputSection* Link::nearby_section(const OutputSection& section, std::uint64_t address) const {
    // GNU ld's home for a symbol of an output section that was left out:
    // the neighbor in the same region (else any) most like the section, as
    // GNU ld judges by flags, or the following one when the symbol's address
    // is not below it.
    std::size_t position = steps_.size();
    for (std::size_t index = 0; index < steps_.size(); ++index) {
        if (steps_[index].placement && steps_[index].placement->output == &section) position = index;
    }
    if (position == steps_.size()) return nullptr;
    const auto& removed = *steps_[position].placement;
    // GNU ld's view of the flags: a section of assignments alone has none.
    const bool inputs = std::any_of(removed.items.begin(), removed.items.end(),
                                    [](const Placement::Item& item) { return !item.sections.empty(); });
    const auto flags = inputs ? section.flags : 0;
    const auto loaded = [](const Placement& placement) {
        return (placement.attributes & script::region_loaded) != 0;
    };
    for (const bool match : {true, false}) {
        const Placement* previous = nullptr;
        const Placement* next = nullptr;
        for (std::size_t index = position; index-- > 0;) {
            const auto* other = steps_[index].placement;
            if (other && other->output && other->emitted &&
                (!match || other->region_index == removed.region_index)) {
                previous = other;
                break;
            }
        }
        for (std::size_t index = position + 1; index < steps_.size(); ++index) {
            const auto* other = steps_[index].placement;
            if (other && other->output && other->emitted &&
                (!match || other->region_index == removed.region_index)) {
                next = other;
                break;
            }
        }
        const Placement* best = next;
        if (!previous) {
        } else if (!next) {
            best = previous;
        } else if (((previous->output->flags ^ next->output->flags) & elf::shf_alloc) != 0 ||
                   loaded(*previous) != loaded(*next)) {
            if (((next->output->flags ^ flags) & elf::shf_alloc) != 0 || (loaded(*previous) && !loaded(*next))) {
                best = previous;
            }
        } else if (((previous->output->flags ^ next->output->flags) & elf::shf_write) != 0) {
            if (((next->output->flags ^ flags) & elf::shf_write) != 0) best = previous;
        } else if (((previous->output->flags ^ next->output->flags) & elf::shf_execinstr) != 0) {
            if (((next->output->flags ^ flags) & elf::shf_execinstr) != 0) best = previous;
        } else if (address < next->output->address) {
            best = previous;
        }
        if (best) return best->output;
    }
    return nullptr;
}

void Link::assign(const script::Assignment& assignment, OutputSection* section, bool final) {
    if (assignment.symbol != ".") {
        const auto* target = find(assignment.symbol);
        // An inactive PROVIDE.
        if (!target || !target->script) return;
    }
    from_dot_ = false;
    auto value = evaluate(*assignment.value);
    if (!value) return;
    if (assignment.op != "=") {
        // SYMBOL op= VALUE is SYMBOL = SYMBOL op VALUE.
        script::Expression name;
        name.kind = assignment.symbol == "." ? script::Expression::Kind::Dot : script::Expression::Kind::Symbol;
        name.name = assignment.symbol;
        name.location = assignment.location;
        const auto current = evaluate(name);
        if (!current) return;
        const auto op = std::string_view(assignment.op).substr(0, assignment.op.size() - 1);
        value = combine(op, *current, *value, *assignment.value);
        if (!value) return;
    }
    // Inside an output section a plain number is an offset from its start.
    if (value->kind == Value::Kind::Number) {
        if (section) {
            value = Value{section_base_ + value->value, Value::Kind::Relative, section};
        } else {
            value->kind = Value::Kind::Absolute;
        }
    }
    if (assignment.symbol == ".") {
        const std::uint64_t target = mask(value->value);
        if (section && target < dot_) {
            if (final) {
                diagnostics_.error(assignment.location, "cannot move the location counter backwards (from " +
                                                            hex(dot_) + " to " + hex(target) + ")");
            }
            expression_failed_ = true;
            return;
        }
        if (!section) prefer_next_ = true;
        dot_ = target;
        return;
    }
    if (end_symbol(assignment.symbol)) found_end_ = true;
    auto& target = symbol(assignment.symbol);
    target.kind = Symbol::Kind::Defined;
    target.script = true;
    target.section = nullptr;
    target.value = mask(value->value);
    target.absolute = false;
    if (value->kind == Value::Kind::Relative) {
        target.output = value->section;
    } else if (!section && from_dot_) {
        // An absolute value from `.` outside output sections, listed in
        // a neighboring section.
        target.output = section_for_dot();
        target.absolute = target.output != nullptr;
    } else {
        target.output = nullptr;
    }
    assigned_.insert(assignment.symbol);
}

std::uint64_t Link::assigned_value(const script::Assignment& assignment) const {
    if (assignment.symbol == ".") return dot_;
    const auto* target = find(assignment.symbol);
    return target ? target->value : 0;
}

std::optional<std::vector<std::uint8_t>> Link::fill_pattern(const script::Fill& fill) {
    // A bare hexadecimal literal gives its digits as the pattern (GNU ld
    // takes it whole); any other value its low four bytes, big-endian.
    const auto& expression = *fill.value;
    std::vector<std::uint8_t> pattern;
    if (expression.kind == script::Expression::Kind::Number && !expression.digits.empty()) {
        const auto& digits = expression.digits;
        std::size_t remaining = digits.size();
        unsigned value = 0;
        for (const char c : digits) {
            const unsigned digit = c <= '9' ? static_cast<unsigned>(c - '0')
                                            : static_cast<unsigned>((std::tolower(static_cast<unsigned char>(c)) - 'a') + 10);
            value = (value << 4U) + digit;
            --remaining;
            if ((remaining & 1U) == 0) {
                pattern.push_back(static_cast<std::uint8_t>(value));
                value = 0;
            }
        }
        return pattern;
    }
    const auto saved = current_;
    current_ = nullptr;
    const auto value = evaluate(expression);
    current_ = saved;
    if (!value) return std::nullopt;
    for (const unsigned shift : {24U, 16U, 8U, 0U}) pattern.push_back(static_cast<std::uint8_t>(value->value >> shift));
    return pattern;
}

void Link::lay_out_section(Placement& placement, bool final) {
    // GNU ld's sizing of an output section statement.
    auto* output = placement.output;
    const auto* statement = placement.statement;
    placement.emitted = false;
    if (final) placement.padding.clear();
    // An address sets `.`, even for a section that does not exist.
    std::optional<std::uint64_t> fixed;
    if (output) {
        for (const auto& [name, start] : options_.section_starts) {
            if (name == output->name) fixed = start;
        }
    }
    if (!fixed && statement && statement->address) {
        if (const auto value = evaluate(*statement->address)) fixed = mask(value->value);
    }
    if (fixed) dot_ = *fixed;
    if (!output || !placement.created) return;
    const bool alloc = output->alloc();
    const bool loaded = (placement.attributes & script::region_loaded) != 0;
    bool has_inputs = false;
    for (const auto& item : placement.items) {
        if (!item.sections.empty()) has_inputs = true;
    }
    std::uint64_t statement_alignment = 1;
    if (statement && statement->alignment) {
        if (const auto value = evaluate(*statement->alignment)) statement_alignment = std::max<std::uint64_t>(value->value, 1);
    }
    std::optional<std::uint64_t> subalign;
    if (statement && statement->subalign) {
        if (const auto value = evaluate(*statement->subalign)) subalign = std::max<std::uint64_t>(value->value, 1);
    }
    // SUBALIGN replaces the alignment of the section and of its inputs.
    std::uint64_t alignment = std::max(output->alignment, statement_alignment);
    if (subalign && has_inputs) alignment = *subalign;
    const std::size_t default_index = regions_.size() - 1;
    std::size_t region = default_index;
    if (statement && !statement->region.empty()) {
        region = region_index(statement->region);
    } else if (!statement && !placement.region.empty()) {
        region = region_index(placement.region);
    } else if (statement && !fixed && !statement->load_region.empty()) {
        // An AT> region without >REGION or an address is also the region.
        region = region_index(statement->load_region);
    }
    // A non-allocated orphan sits at address 0; a non-allocated script
    // section without an address at its region's next address, as in GNU
    // ld (so debugging sections need an address of 0 in scripts).
    const bool unallocated = !alloc && has_inputs;
    std::uint64_t start = 0;
    std::uint64_t section_alignment = alignment;
    if (unallocated && !fixed && !statement) {
        start = 0;
    } else if (!fixed) {
        if (region == default_index && (alloc || loaded)) region = default_region(placement);
        start = regions_[region].current;
        if (const auto& run = regions_[region].tbss_run;
            run && tbss(*output) && start == run->first) {
            start = run->second;
        }
    } else {
        start = *fixed;
        section_alignment = statement_alignment;
    }
    placement.region_index = region;
    const auto unaligned = start;
    start = mask(align_up(start, section_alignment));
    const auto delta = start - unaligned;
    if (final && fixed && delta != 0) {
        diagnostics_.warning(statement ? statement->location : SourceLocation{},
                             "start of section " + output->name + " changed by " + std::to_string(delta));
    }
    output->address = start;
    output->alignment = alignment;
    const auto outer = dot_;
    dot_ = start;
    section_base_ = start;
    current_ = &placement;
    const script::Fill* fill = statement && statement->fill ? &*statement->fill : nullptr;
    const auto pad = [&](std::uint64_t from) {
        if (final && dot_ > from) placement.padding.push_back({from - start, dot_ - from, fill});
    };
    for (auto& item : placement.items) {
        if (!item.item || std::holds_alternative<script::InputPattern>(*item.item)) {
            for (auto* section : item.sections) {
                if (!section->included()) {
                    section->output = output;
                    section->offset = dot_ - start;
                    continue;
                }
                const auto from = dot_;
                dot_ = align_up(dot_, subalign ? *subalign : section->alignment);
                pad(from);
                section->output = output;
                section->offset = dot_ - start;
                dot_ += section->size;
            }
        } else if (const auto* data = std::get_if<script::Data>(item.item)) {
            item.offset = dot_ - start;
            if (const auto value = evaluate(*data->value)) item.value = value->value;
            dot_ += data->size;
        } else if (const auto* assignment = std::get_if<script::Assignment>(item.item)) {
            const auto from = dot_;
            assign(*assignment, output, final);
            pad(from);
            if (dot_ != from) placement.advanced = true;
            item.value = assigned_value(*assignment);
        } else if (const auto* check = std::get_if<script::Assert>(item.item)) {
            const auto value = evaluate(*check->condition);
            if (final && value && value->value == 0) diagnostics_.error(check->location, check->message);
        } else if (const auto* pattern = std::get_if<script::Fill>(item.item)) {
            fill = pattern;
        }
    }
    current_ = nullptr;
    const auto size = dot_ - start;
    output->size = size;
    // An empty section that nothing keeps is left out, and without symbol
    // assignments ignored: `.` stays.
    placement.emitted = size != 0 || placement.keep;
    const bool ignored = !placement.emitted && !placement.symbols && !(statement && statement->update_dot);
    if (ignored) dot_ = outer;
    if (unallocated) {
        output->load_address = start;
        if (!ignored) dot_ = start + size;
        return;
    }
    if (final && !ignored && !fixed && region == default_index && alloc && !script_.regions.empty()) {
        const auto message = "no memory region specified for loadable section '" + output->name + "'";
        if (statement) {
            diagnostics_.error(statement->location, message);
        } else {
            diagnostics_.command_error(message);
        }
    }
    // The load address: AT(), the AT> region's next address, after an
    // OVERLAY's previous section, or the virtual address moved as far as
    // the region's last section was.
    auto& state = regions_[region];
    const auto load_index = placement.load_region.empty() ? regions_.size() : region_index(placement.load_region);
    const bool load_region = load_index < regions_.size();
    std::uint64_t load = start;
    if (statement && statement->load_address) {
        if (const auto value = evaluate(*statement->load_address)) load = value->value;
    } else if (load_region) {
        load = regions_[load_index].current;
        if (statement && statement->align_with_input) {
            load += delta;
        } else {
            load = align_up(load, load_index != region ? statement_alignment : section_alignment);
        }
    } else if (state.has_last && alloc) {
        if (start < state.last_address && size != 0 && start + size <= state.last_address) {
            load = start;
        } else {
            if (statement && statement->overlay == script::OutputSection::Overlay::Later) {
                load = state.last_load + state.last_size;
            } else {
                load = start + state.last_load - state.last_address;
            }
            load = align_up(load, section_alignment);
        }
    }
    load = mask(load);
    output->load_address = load;
    const auto position = ignored ? dot_ : start;
    if (((alloc && (size != 0 || (!state.has_last && start != load) ||
                    (state.has_last && position >= state.last_address))) ||
         (statement && statement->overlay == script::OutputSection::Overlay::First)) &&
        !load_region) {
        state.has_last = true;
        state.last_address = start;
        state.last_load = load;
        state.last_size = size;
    }
    if (ignored) return;
    if (alloc) {
        current_section_ = &placement;
        prefer_next_ = false;
    }
    if (tbss(*output)) {
        if (!state.tbss_run || state.current != state.tbss_run->first) state.tbss_run.emplace(start, start);
        state.tbss_run->second = start + size;
        dot_ = state.tbss_run->first;
    } else {
        state.tbss_run.reset();
        dot_ = start + size;
    }
    if (statement && statement->update_dot) {
        if (const auto value = evaluate(*statement->update_dot)) dot_ = mask(value->value);
    }
    if (!alloc && !loaded) return;
    const auto check = [&](std::size_t index, std::uint64_t base, bool addressed) {
        if (!final || index >= script_.regions.size()) return;
        const auto& entry = script_.regions[index];
        const auto origin = evaluate(*entry.origin);
        const auto length = evaluate(*entry.length);
        if (!origin || !length) return;
        const auto current = regions_[index].current;
        const auto end = origin->value + length->value;
        if ((current < origin->value || current - origin->value > length->value) &&
            (current != end || base == 0)) {
            const auto where = statement ? statement->location : SourceLocation{};
            if (addressed) {
                diagnostics_.error(where, "address " + hex(current) + " of section '" + output->name +
                                              "' is not within region '" + entry.name + "'");
            } else {
                diagnostics_.error(where, "section '" + output->name + "' will not fit in region '" +
                                              entry.name + "'");
            }
        }
    };
    state.current = dot_;
    check(region, start, fixed.has_value());
    if (load_region && load_index != region && (loaded || (statement && statement->align_with_input))) {
        regions_[load_index].current = load + size;
        check(load_index, load, false);
    }
}

bool Link::layout_pass(bool final) {
    final_ = final;
    expression_failed_ = false;
    dot_ = 0;
    current_ = nullptr;
    assigned_.clear();
    current_section_ = nullptr;
    prefer_next_ = false;
    found_end_ = false;
    segment_phase_ = SegmentPhase::None;
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        regions_[index] = {};
        if (index < script_.regions.size()) {
            if (const auto origin = evaluate(*script_.regions[index].origin)) regions_[index].current = origin->value;
        }
    }
    for (std::size_t index = 0; index < steps_.size(); ++index) {
        step_index_ = index;
        auto& step = steps_[index];
        if (step.placement ||
            (step.command && std::holds_alternative<script::OutputSection>(*step.command))) {
            after_end_[index] = found_end_;
        }
        if (step.placement) {
            lay_out_section(*step.placement, final);
            continue;
        }
        if (!step.command) continue;
        if (const auto* assignment = std::get_if<script::Assignment>(step.command)) {
            assign(*assignment, nullptr, final);
            // A top-level assignment to `.` moves the default region.
            if (assignment->symbol == ".") regions_.back().current = dot_;
            step.value = assigned_value(*assignment);
        } else if (const auto* check = std::get_if<script::Assert>(step.command)) {
            const auto value = evaluate(*check->condition);
            if (final && value && value->value == 0) diagnostics_.error(check->location, check->message);
        }
    }
    return !(final && (expression_failed_ || diagnostics_.errors() != 0));
}

void Link::compute_output_attributes() {
    for (auto& placement : placements_) {
        placement->created = false;
        placement->symbols = false;
        placement->attributes = 0;
        if (!placement->output) continue;
        auto& output = *placement->output;
        const auto* statement = placement->statement;
        output.sections.clear();
        output.flags = 0;
        output.alignment = 1;
        bool progbits = false;
        bool data = false;
        bool data_attribute = false;
        bool loaded = false;
        std::optional<std::uint32_t> type;
        std::optional<std::uint64_t> entry_size;
        bool mixed = false;
        // Mergeable flags survive when every input has the same ones.
        std::optional<std::uint64_t> merge_flags;
        const auto absorb = [&](const InputSection& section) {
            placement->created = true;
            output.flags |= section.flags;
            output.alignment = std::max(output.alignment, section.alignment);
            if (!section.nobits()) progbits = true;
            if (section.alloc() && !section.nobits()) {
                loaded = true;
                if ((section.flags & elf::shf_execinstr) == 0) data_attribute = true;
            }
            const auto merging = section.flags & (elf::shf_merge | elf::shf_strings);
            if (!type) {
                type = section.type;
                entry_size = section.entry_size;
                merge_flags = merging;
            } else {
                if (*type != section.type) mixed = true;
                if (*entry_size != section.entry_size) entry_size = 0;
                if (*merge_flags != merging) merge_flags = 0;
            }
        };
        for (const auto& item : placement->items) {
            if (item.item && !std::holds_alternative<script::InputPattern>(*item.item)) {
                placement->created = true;
                if (std::holds_alternative<script::Data>(*item.item)) data = true;
                if (const auto* assignment = std::get_if<script::Assignment>(item.item)) {
                    if (assignment->symbol != ".") {
                        placement->symbols = true;
                    } else if (!trivial_dot_assignment(*assignment)) {
                        placement->keep = true;
                    }
                }
            }
            for (auto* section : item.sections) {
                if (section->included()) output.sections.push_back(section);
                absorb(*section);
            }
        }
        output.flags &= ~(elf::shf_group | elf::shf_merge | elf::shf_strings | elf::shf_info_link |
                          elf::shf_exclude | std::uint64_t{0x80});
        output.entry_size = entry_size.value_or(0);
        if (output.entry_size != 0) output.flags |= merge_flags.value_or(0);
        using Kind = script::OutputSection::Kind;
        const auto kind = statement ? statement->kind : Kind::Normal;
        if (!type) {
            // Data alone gives read-only contents (GNU ld); assignments to
            // `.` alone give writable zero-filled space.
            output.flags = data ? elf::shf_alloc : elf::shf_alloc | elf::shf_write;
            output.type = data ? elf::sht_progbits : elf::sht_nobits;
            loaded = data;
        } else if (kind == Kind::NoLoad || (!progbits && !data)) {
            output.type = elf::sht_nobits;
        } else if (type && !mixed && *type != elf::sht_nobits) {
            output.type = *type;
        } else {
            output.type = elf::sht_progbits;
        }
        if (data) loaded = true;
        if (kind == Kind::NoLoad) loaded = false;
        if (kind == Kind::NoAlloc) {
            output.flags &= ~elf::shf_alloc;
            loaded = false;
        }
        if (statement && statement->readonly) output.flags &= ~elf::shf_write;
        if (statement && statement->type) {
            const auto& expression = *statement->type;
            std::optional<std::uint32_t> value;
            if (expression.kind == script::Expression::Kind::Symbol) {
                value = section_type_name(expression.name);
            } else {
                final_ = true;
                if (const auto result = evaluate(expression)) value = static_cast<std::uint32_t>(result->value);
            }
            if (!value) {
                diagnostics_.error(expression.location, "invalid type for output section '" + output.name + "'");
            } else {
                output.type = *value;
            }
        }
        // The attributes MEMORY regions select by (GNU ld's section flags).
        const bool alloc = output.alloc();
        const bool readonly = (output.flags & elf::shf_write) == 0;
        const bool code = (output.flags & elf::shf_execinstr) != 0;
        std::uint8_t attributes = 0;
        if (alloc) attributes |= script::region_allocated;
        if (readonly) attributes |= script::region_readonly;
        if (code) attributes |= script::region_executable;
        if (data_attribute || (alloc && !readonly && !code)) attributes |= script::region_writable;
        if (loaded && alloc) attributes |= script::region_loaded;
        placement->attributes = attributes;
        if (!statement) placement->created = type.has_value();
    }
    // The first TLS output section takes the largest alignment of the TLS
    // sections that follow it, so the TLS segment starts aligned (GNU ld's
    // _bfd_elf_tls_setup).
    OutputSection* first_tls = nullptr;
    for (const auto& placement : placements_) {
        if (!placement->output || !placement->created) continue;
        auto& output = *placement->output;
        if ((output.flags & elf::shf_tls) == 0) {
            if (first_tls) break;
            continue;
        }
        if (!first_tls) first_tls = &output;
        first_tls->alignment = std::max(first_tls->alignment, output.alignment);
    }
    final_ = false;
}

bool Link::layout() {
    for (const auto& placement : placements_) placement->advanced = false;
    compute_output_attributes();
    if (diagnostics_.errors() != 0) return false;
    after_end_.assign(steps_.size(), false);
    const auto snapshot = [&] {
        std::vector<std::uint64_t> values;
        for (const auto& output : outputs_) {
            values.push_back(output->address);
            values.push_back(output->load_address);
            values.push_back(output->size);
        }
        for (const auto& entry : symbols_) {
            if (entry.script) values.push_back(entry.value);
        }
        return values;
    };
    const auto settle = [&] {
        std::vector<std::uint64_t> previous;
        for (int pass = 0; pass < 16; ++pass) {
            layout_pass(false);
            auto current = snapshot();
            if (current == previous) return true;
            previous = std::move(current);
        }
        diagnostics_.command_error("the linker script's addresses do not settle");
        return false;
    };
    segment_adjust_ = false;
    if (!settle()) return false;
    if (segment_phase_ == SegmentPhase::EndSeen) {
        // GNU ld's DATA_SEGMENT_ALIGN choice: save a common page when the
        // data segment's partial first and last pages fit in one.
        const auto common = segment_common_page_;
        const auto first = (~segment_base_ + 1) & (common - 1);
        const auto last = segment_end_ & (common - 1);
        if (first != 0 && last != 0 && (segment_base_ & ~(common - 1)) != (segment_end_ & ~(common - 1)) &&
            first + last <= common) {
            segment_adjust_ = true;
            if (!settle()) return false;
        }
    }
    if (!layout_pass(true)) return false;
    // A section of assignments alone that never advances `.` stays
    // unallocated, writable, and without contents (GNU ld).
    for (const auto& placement : placements_) {
        auto* output = placement->output;
        if (!output || placement->advanced) continue;
        if (std::ranges::any_of(placement->items, [](const Placement::Item& item) {
                return !item.sections.empty() || (item.item && std::holds_alternative<script::Data>(*item.item));
            })) {
            continue;
        }
        output->flags = elf::shf_write;
        output->type = elf::sht_progbits;
    }
    return true;
}

std::uint64_t Link::headers_size() const {
    const std::uint64_t header = object_.elf64 ? 64 : 52;
    const std::uint64_t entry = object_.elf64 ? 56 : 32;
    return header + entry * segment_estimate_;
}
bool Link::build_segments() {
    segments_.clear();
    std::vector<OutputSection*> allocated;
    for (const auto& step : steps_) {
        if (step.placement && step.placement->output && step.placement->emitted &&
            step.placement->output->alloc()) {
            allocated.push_back(step.placement->output);
        }
    }
    for (const auto& [type, section] : target_->extra_segments(allocated)) {
        Segment segment;
        segment.type = type;
        segment.flags = pf_r;
        segment.sections.push_back(section);
        segments_.push_back(std::move(segment));
    }
    const auto loaded = [](const OutputSection* section) { return !section->nobits(); };
    const auto writable = [](const OutputSection* section) {
        return (section->flags & elf::shf_write) != 0;
    };
    if (!script_.phdrs.empty()) {
        const auto first = segments_.size();
        for (const auto& phdr : script_.phdrs) {
            Segment segment;
            segment.type = phdr.type;
            segment.file_header = phdr.file_header;
            segment.program_headers = phdr.program_headers;
            if (phdr.flags) {
                final_ = true;
                if (const auto value = evaluate(*phdr.flags)) segment.flags = static_cast<std::uint32_t>(value->value);
            }
            if (phdr.address) {
                final_ = true;
                if (const auto value = evaluate(*phdr.address)) segment.physical = value->value;
            }
            segments_.push_back(std::move(segment));
        }
        // GNU ld's rule: a section without :PHDR takes the program headers
        // of the previous section that names some (or, before any, of the
        // next one), unless it is NOLOAD.
        std::vector<std::string> current;
        for (const auto& step : steps_) {
            if (step.placement && step.placement->statement &&
                !step.placement->statement->phdrs.empty()) {
                current = step.placement->statement->phdrs;
                break;
            }
        }
        for (const auto& step : steps_) {
            if (!step.placement || !step.placement->output || !step.placement->emitted ||
                !step.placement->output->alloc()) {
                continue;
            }
            const auto* statement = step.placement->statement;
            if (statement && !statement->phdrs.empty()) {
                current = statement->phdrs;
            } else if (statement && statement->kind == script::OutputSection::Kind::NoLoad) {
                continue;
            }
            for (const auto& name : current) {
                if (name == "NONE") continue;
                const auto found = std::find_if(script_.phdrs.begin(), script_.phdrs.end(),
                                                [&](const script::Phdr& phdr) { return phdr.name == name; });
                if (found == script_.phdrs.end()) {
                    diagnostics_.error(statement ? statement->location : SourceLocation{},
                                       "program header '" + name + "' is not defined");
                    return false;
                }
                auto& segment = segments_[first + static_cast<std::size_t>(found - script_.phdrs.begin())];
                segment.sections.push_back(step.placement->output);
            }
        }
        for (std::size_t index = first; index < segments_.size(); ++index) {
            auto& segment = segments_[index];
            if (script_.phdrs[index - first].flags) continue;
            segment.flags = pf_r;
            for (const auto* section : segment.sections) {
                if (writable(section)) segment.flags |= pf_w;
                if ((section->flags & elf::shf_execinstr) != 0) segment.flags |= pf_x;
            }
        }
    } else {
        for (const auto& step : steps_) {
            const auto* statement = step.placement ? step.placement->statement : nullptr;
            if (statement && !statement->phdrs.empty() && statement->phdrs.front() != "NONE") {
                diagnostics_.error(statement->location, "program header '" + statement->phdrs.front() +
                                                            "' is not defined");
                return false;
            }
        }
        // GNU ld's rule: a section joins the previous segment unless its
        // load and virtual addresses differ by another amount, it overlaps
        // or skips a page, it is loaded after a zero-filled section, or it
        // is writable after read-only sections on another page.
        auto sorted = allocated;
        std::erase_if(sorted, [](const OutputSection* section) { return tbss(*section); });
        std::stable_sort(sorted.begin(), sorted.end(), [](const OutputSection* a, const OutputSection* b) {
            return a->load_address < b->load_address;
        });
        const OutputSection* last = nullptr;
        bool segment_writable = false;
        const auto page = page_size_;
        for (auto* section : sorted) {
            bool start = last == nullptr;
            if (last) {
                const auto last_end = last->load_address + last->size;
                if (last->load_address - last->address != section->load_address - section->address) {
                    start = true;
                } else if (section->load_address < last_end) {
                    start = true;
                } else if (((last_end - 1) & ~(page - 1)) == (section->load_address & ~(page - 1))) {
                    start = false;
                } else if (align_up(last_end, page) + page <= section->load_address) {
                    start = true;
                } else if (!loaded(last) && loaded(section)) {
                    start = true;
                } else if (!segment_writable && writable(section)) {
                    start = true;
                }
            }
            if (start) {
                Segment segment;
                segment.type = pt_load;
                segments_.push_back(std::move(segment));
                segment_writable = false;
            }
            auto& segment = segments_.back();
            segment.sections.push_back(section);
            segment_writable = segment_writable || writable(section);
            last = section;
        }
        for (auto& segment : segments_) {
            if (segment.type != pt_load) continue;
            segment.flags = pf_r;
            for (const auto* section : segment.sections) {
                if (writable(section)) segment.flags |= pf_w;
                if ((section->flags & elf::shf_execinstr) != 0) segment.flags |= pf_x;
            }
        }
        // PT_DYNAMIC for the output section holding .dynamic.
        if (dynamic_ && dynamic_->included() && dynamic_->output &&
            std::find(allocated.begin(), allocated.end(), dynamic_->output) != allocated.end()) {
            Segment segment;
            segment.type = pt_dynamic;
            segment.flags = pf_r | pf_w;
            segment.sections.push_back(dynamic_->output);
            segments_.push_back(std::move(segment));
        }
        // PT_TLS spans the TLS sections, which must be adjacent.
        std::vector<OutputSection*> tls;
        std::size_t first_tls = 0;
        for (std::size_t index = 0; index < allocated.size(); ++index) {
            if ((allocated[index]->flags & elf::shf_tls) == 0) continue;
            if (tls.empty()) first_tls = index;
            tls.push_back(allocated[index]);
            if (index - first_tls + 1 != tls.size()) {
                diagnostics_.command_error("TLS output sections '" + tls.front()->name +
                                           "' and '" + tls.back()->name + "' are not adjacent");
                return false;
            }
        }
        if (!tls.empty()) {
            Segment segment;
            segment.type = pt_tls;
            segment.flags = pf_r;
            segment.sections = tls;
            segments_.push_back(std::move(segment));
        }
        // GNU ld's PT_GNU_STACK once an input has .note.GNU-stack: executable
        // when an input asks for it or has no note and the target's default
        // is an executable stack (MIPS).
        bool notes = false;
        bool executable = false;
        for (const auto& file : files_) {
            if (file->raw || file->just_symbols) continue;
            if (file->stack_note) notes = true;
            if (file->stack_note ? file->executable_stack : target_->default_executable_stack()) executable = true;
        }
        // PT_GNU_EH_FRAME for the output section holding .eh_frame_hdr.
        if (frame_header_ && frame_header_->included() && frame_header_->size != 0 && frame_header_->output &&
            std::find(allocated.begin(), allocated.end(), frame_header_->output) != allocated.end()) {
            Segment segment;
            segment.type = pt_gnu_eh_frame;
            segment.flags = pf_r;
            segment.sections.push_back(frame_header_->output);
            segments_.push_back(std::move(segment));
        }
        if (notes) {
            Segment segment;
            segment.type = pt_gnu_stack;
            segment.flags = pf_r | pf_w | (executable ? pf_x : 0);
            segment.alignment = 16;
            segments_.push_back(std::move(segment));
        }
    }
    return true;
}

void Link::assign_offsets() {
    const std::uint64_t header = object_.elf64 ? 64 : 52;
    const std::uint64_t entry = object_.elf64 ? 56 : 32;
    const std::uint64_t headers = header + entry * segments_.size();
    const auto page = page_size_;
    std::uint64_t offset = headers;
    std::unordered_set<const OutputSection*> placed;
    bool first_load = true;
    for (auto& segment : segments_) {
        if (segment.type != pt_load) continue;
        segment.alignment = page;
        if (segment.sections.empty()) {
            segment.offset = offset;
            if (segment.physical) segment.physical_address = *segment.physical;
            continue;
        }
        // GNU ld's p_align: the page size, or a larger section alignment.
        for (const auto* section : segment.sections) {
            segment.alignment = std::max(segment.alignment, section->alignment);
        }
        const auto alignment = segment.alignment;
        const auto* first = segment.sections.front();
        // The file and program headers share the first segment when the
        // script left room for them before the first section.
        bool with_headers = segment.file_header || segment.program_headers;
        if (script_.phdrs.empty() && first_load && (first->address & (page - 1)) >= headers) {
            with_headers = true;
        }
        first_load = false;
        const bool contents = std::any_of(segment.sections.begin(), segment.sections.end(),
                                          [](const OutputSection* section) { return !section->nobits(); });
        if (with_headers && (first->address & (page - 1)) >= headers) {
            segment.offset = 0;
            segment.address = first->address & ~(page - 1);
        } else {
            segment.offset = offset + ((first->address - offset) & (alignment - 1));
            segment.address = first->address;
        }
        segment.physical_address = segment.physical.value_or(first->load_address -
                                                             (first->address - segment.address));
        std::uint64_t file_end = segment.offset;
        std::uint64_t memory_end = segment.address;
        bool first_section = true;
        for (auto* section : segment.sections) {
            if (!placed.insert(section).second) continue;
            // A zero-filled section after the first sits where the file
            // stands, as in GNU ld.
            if (section->nobits() && !first_section) {
                section->offset = file_end;
            } else {
                section->offset = segment.offset + (section->address - segment.address);
            }
            first_section = false;
            if (!section->nobits()) file_end = std::max(file_end, section->offset + section->size);
            if (!tbss(*section)) memory_end = std::max(memory_end, section->address + section->size);
        }
        segment.file_size = file_end - segment.offset;
        segment.memory_size = memory_end - segment.address;
        if (!contents) {
            // A segment without file contents gets an offset within the
            // first page and takes no file space (GNU ld).
            segment.offset = (segment.offset + alignment - 1) % alignment + 1;
            segment.file_size = 0;
            continue;
        }
        offset = std::max(offset, file_end);
    }
    for (const auto& step : steps_) {
        if (!step.placement || !step.placement->output || !step.placement->emitted) continue;
        auto* section = step.placement->output;
        if (placed.contains(section)) continue;
        if (section->nobits()) {
            section->offset = offset;
            // A zero-filled TLS section takes the file position of its
            // address in the loadable segment there (GNU ld).
            for (const auto& segment : segments_) {
                if (tbss(*section) && segment.type == pt_load && !segment.sections.empty() &&
                    section->address >= segment.address &&
                    section->address <= segment.address + segment.memory_size) {
                    section->offset = segment.offset + (section->address - segment.address);
                    break;
                }
            }
            continue;
        }
        offset = align_up(offset, section->alignment);
        section->offset = offset;
        offset += section->size;
    }
    for (auto& segment : segments_) {
        if (segment.type == pt_load || segment.sections.empty()) continue;
        const auto* section = segment.sections.front();
        segment.offset = section->offset;
        segment.address = section->address;
        segment.physical_address = section->load_address;
        segment.file_size = 0;
        segment.memory_size = 0;
        segment.alignment = 1;
        // A segment of several sections, such as PT_TLS, spans them.
        for (const auto* member : segment.sections) {
            const auto end = member->address + member->size - segment.address;
            if (!member->nobits()) segment.file_size = std::max(segment.file_size, end);
            segment.memory_size = std::max(segment.memory_size, end);
            segment.alignment = std::max(segment.alignment, member->alignment);
        }
    }
}

} // namespace cross::linker
