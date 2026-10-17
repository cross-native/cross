// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/link.hpp"

#include <algorithm>
#include <sstream>

namespace cross::linker {
namespace {

constexpr std::uint32_t pt_load = 1;
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

bool matches_file(const script::InputPattern& pattern, const InputFile& file) {
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

// Orphan placement classes: code, read-only data, data, zero-filled data.
int section_class(std::uint64_t flags, bool nobits) {
    if ((flags & elf::shf_execinstr) != 0) return 0;
    if ((flags & elf::shf_write) == 0) return 1;
    return nobits ? 3 : 2;
}

} // namespace

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
                    if (pattern->keep) section->keep = true;
                    if (discard) section->discarded = true;
                }
                if (pattern->sort == script::InputPattern::Sort::Name) {
                    std::stable_sort(entry.sections.begin(), entry.sections.end(),
                                     [](const InputSection* a, const InputSection* b) {
                                         return a->name < b->name;
                                     });
                } else if (pattern->sort == script::InputPattern::Sort::Alignment) {
                    std::stable_sort(entry.sections.begin(), entry.sections.end(),
                                     [](const InputSection* a, const InputSection* b) {
                                         return a->alignment > b->alignment;
                                     });
                }
            }
            placement->items.push_back(std::move(entry));
        }
        steps_.push_back({&command, placement.get()});
        placements_.push_back(std::move(placement));
    }
    std::vector<InputSection*> orphans;
    for (auto* section : candidates) {
        if (!assigned.contains(section)) orphans.push_back(section);
    }
    place_orphans(orphans);
    return true;
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
                                    (placement.statement && placement.statement->noload);
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
        if (first->alloc()) {
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
            if (same) {
                position = steps_.begin() + static_cast<std::ptrdiff_t>(*same + 1);
            } else if (earlier) {
                position = steps_.begin() + static_cast<std::ptrdiff_t>(*earlier + 1);
            } else if (first_allocated) {
                position = steps_.begin() + static_cast<std::ptrdiff_t>(*first_allocated);
            }
        }
        steps_.insert(position, Step{nullptr, placement.get()});
        placements_.push_back(std::move(placement));
    }
}

void Link::expression_error(const script::Expression& expression, std::string_view message) {
    expression_failed_ = true;
    if (final_) diagnostics_.error(expression.location, message);
}

std::optional<Link::Value> Link::evaluate(const script::Expression& expression) {
    using Kind = script::Expression::Kind;
    switch (expression.kind) {
    case Kind::Number:
        return Value{expression.number, false};
    case Kind::Dot:
        return Value{dot_, true, current_ && current_->output ? current_->output : last_allocated_};
    case Kind::Symbol: {
        const auto* target = find(expression.name);
        if (!target || !target->defined()) {
            expression_error(expression, "undefined symbol '" + expression.name +
                                             "' referenced in expression");
            return std::nullopt;
        }
        if (target->script) return Value{target->value, target->output != nullptr, target->output};
        if (target->section) {
            return Value{section_address(*target->section, target->value), true, target->section->output};
        }
        return Value{target->value, false};
    }
    case Kind::Unary: {
        const auto operand = evaluate(*expression.operands[0]);
        if (!operand) return std::nullopt;
        const auto value = operand->value;
        if (expression.op == "-") return Value{~value + 1, false};
        if (expression.op == "~") return Value{~value, false};
        if (expression.op == "!") return Value{value == 0 ? 1U : 0U, false};
        return operand;
    }
    case Kind::Conditional: {
        const auto condition = evaluate(*expression.operands[0]);
        if (!condition) return std::nullopt;
        return evaluate(*expression.operands[condition->value != 0 ? 1 : 2]);
    }
    case Kind::Binary: {
        const auto left = evaluate(*expression.operands[0]);
        const auto right = evaluate(*expression.operands[1]);
        if (!left || !right) return std::nullopt;
        const auto a = left->value;
        const auto b = right->value;
        const auto& op = expression.op;
        const bool relative = left->relative || right->relative;
        auto* section = left->relative ? left->section : right->section;
        if (op == "+") return Value{a + b, relative, section};
        if (op == "-") {
            const bool difference = left->relative && !right->relative;
            return Value{a - b, difference, difference ? left->section : nullptr};
        }
        if (op == "*") return Value{a * b, false};
        if (op == "/" || op == "%") {
            if (b == 0) {
                expression_error(expression, "division by zero");
                return std::nullopt;
            }
            return Value{op == "/" ? a / b : a % b, false};
        }
        if (op == "<<") return Value{b >= 64 ? 0 : a << b, false};
        if (op == ">>") return Value{b >= 64 ? 0 : a >> b, false};
        if (op == "&") return Value{a & b, relative, section};
        if (op == "|") return Value{a | b, relative, section};
        if (op == "^") return Value{a ^ b, relative, section};
        const auto truth = [](bool value) { return Value{value ? 1U : 0U, false}; };
        if (op == "<") return truth(a < b);
        if (op == "<=") return truth(a <= b);
        if (op == ">") return truth(a > b);
        if (op == ">=") return truth(a >= b);
        if (op == "==") return truth(a == b);
        if (op == "!=") return truth(a != b);
        if (op == "&&") return truth(a != 0 && b != 0);
        if (op == "||") return truth(a != 0 || b != 0);
        expression_error(expression, "unsupported operator '" + op + "'");
        return std::nullopt;
    }
    case Kind::Function:
        break;
    }
    const auto& name = expression.name;
    const auto argument = [&](std::size_t index) { return evaluate(*expression.operands[index]); };
    const auto section = [&]() -> OutputSection* {
        const auto& section_name = expression.operands[0]->name;
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
            return Value{mask(align_up(value->value, alignment->value)), value->relative, value->section};
        }
        const auto alignment = argument(0);
        if (!alignment) return std::nullopt;
        return Value{mask(align_up(dot_, alignment->value)), true,
                     current_ && current_->output ? current_->output : last_allocated_};
    }
    if (name == "ABSOLUTE") {
        const auto value = argument(0);
        if (!value) return std::nullopt;
        return Value{value->value, false};
    }
    if (name == "MAX" || name == "MIN") {
        const auto a = argument(0);
        const auto b = argument(1);
        if (!a || !b) return std::nullopt;
        const bool first = name == "MAX" ? a->value >= b->value : a->value <= b->value;
        return first ? a : b;
    }
    if (name == "LOG2CEIL") {
        const auto value = argument(0);
        if (!value) return std::nullopt;
        std::uint64_t result = 0;
        while (result < 64 && (std::uint64_t{1} << result) < value->value) ++result;
        return Value{result, false};
    }
    if (name == "ADDR" || name == "LOADADDR" || name == "SIZEOF" || name == "ALIGNOF") {
        auto* output = section();
        if (!output) return std::nullopt;
        if (name == "ADDR") return Value{output->address, true, output};
        if (name == "LOADADDR") return Value{output->load_address, false};
        if (name == "SIZEOF") return Value{output->size, false};
        return Value{output->alignment, false};
    }
    if (name == "SIZEOF_HEADERS") {
        uses_headers_size_ = true;
        return Value{headers_size(), false};
    }
    if (name == "DEFINED") {
        const auto& symbol_name = expression.operands[0]->name;
        const auto* target = find(symbol_name);
        const bool defined = target && target->defined() &&
                             (!target->script || assigned_.contains(symbol_name));
        return Value{defined ? 1U : 0U, false};
    }
    if (name == "ORIGIN" || name == "LENGTH") {
        const auto& region_name = expression.operands[0]->name;
        for (const auto& region : script_.regions) {
            if (region.name != region_name) continue;
            const auto value = evaluate(name == "ORIGIN" ? *region.origin : *region.length);
            if (!value) return std::nullopt;
            return Value{value->value, false};
        }
        expression_error(expression, "undefined memory region '" + region_name + "'");
        return std::nullopt;
    }
    if (name == "CONSTANT") {
        const bool maximum = expression.operands[0]->name == "MAXPAGESIZE";
        return Value{maximum ? page_size_ : std::min<std::uint64_t>(page_size_, 0x1000), false};
    }
    if (name == "SEGMENT_START") return argument(1);
    expression_error(expression, "unsupported function '" + name + "'");
    return std::nullopt;
}

void Link::assign(const script::Assignment& assignment, OutputSection* section, bool final) {
    if (assignment.symbol != ".") {
        const auto* target = find(assignment.symbol);
        // An inactive PROVIDE.
        if (!target || !target->script) return;
    }
    const auto value = evaluate(*assignment.value);
    if (!value) return;
    std::uint64_t result = value->value;
    bool relative = value->relative;
    auto* owner = value->section;
    if (assignment.op != "=") {
        std::uint64_t current = dot_;
        if (assignment.symbol != ".") current = find(assignment.symbol)->value;
        const auto& op = assignment.op;
        if (op == "+=") result = current + result;
        else if (op == "-=") result = current - result;
        else if (op == "*=") result = current * result;
        else if (op == "/=") result = result == 0 ? 0 : current / result;
        else if (op == "<<=") result = result >= 64 ? 0 : current << result;
        else if (op == ">>=") result = result >= 64 ? 0 : current >> result;
        else if (op == "&=") result = current & result;
        else if (op == "|=") result = current | result;
        relative = true;
        owner = section ? section : last_allocated_;
    }
    if (assignment.symbol == ".") {
        // A plain number assigned to `.` inside an output section is an
        // offset from the section's start.
        std::uint64_t target = section && !relative ? section_base_ + result : result;
        target = mask(target);
        if (section && target < dot_) {
            if (final) {
                diagnostics_.error(assignment.location, "cannot move the location counter backwards (from " +
                                                            hex(dot_) + " to " + hex(target) + ")");
            }
            expression_failed_ = true;
            return;
        }
        dot_ = target;
        return;
    }
    auto& target = symbol(assignment.symbol);
    target.kind = Symbol::Kind::Defined;
    target.script = true;
    target.section = nullptr;
    target.value = mask(result);
    target.output = relative ? (owner ? owner : (section ? section : last_allocated_)) : nullptr;
    assigned_.insert(assignment.symbol);
}

std::uint64_t Link::assigned_value(const script::Assignment& assignment) const {
    if (assignment.symbol == ".") return dot_;
    const auto* target = find(assignment.symbol);
    return target ? target->value : 0;
}

void Link::lay_out_section(Placement& placement, bool final) {
    auto* output = placement.output;
    placement.emitted = false;
    if (!output) return;
    const auto* statement = placement.statement;
    bool content = false;
    bool symbols = false;
    for (const auto& item : placement.items) {
        if (!item.item || std::holds_alternative<script::InputPattern>(*item.item)) {
            for (const auto* section : item.sections) {
                if (section->size != 0) content = true;
            }
        } else if (std::holds_alternative<script::Data>(*item.item)) {
            content = true;
        } else if (const auto* assignment = std::get_if<script::Assignment>(item.item)) {
            if (assignment->symbol == ".") {
                content = true;
            } else {
                symbols = true;
            }
        }
    }
    // An output section without contents is not emitted; its address still
    // counts when it defines symbols.
    if (!content && !symbols) {
        output->size = 0;
        return;
    }
    const bool allocated = output->alloc() || !content;
    std::uint64_t alignment = output->alignment;
    if (statement && statement->alignment) {
        if (const auto value = evaluate(*statement->alignment)) {
            alignment = std::max<std::uint64_t>(alignment, value->value);
        }
    }
    std::optional<std::uint64_t> fixed;
    for (const auto& [name, start] : options_.section_starts) {
        if (name == output->name) fixed = start;
    }
    if (!fixed && statement && statement->address) {
        if (const auto value = evaluate(*statement->address)) fixed = value->value;
    }
    const std::string region = statement ? statement->region : std::string();
    std::string load_region = statement ? statement->load_region : std::string();
    // GNU ld carries an AT> region to a following section of the same
    // region that sets neither its address nor its load address.
    if (load_region.empty() && statement && !statement->address && !statement->load_address &&
        region == previous_region_) {
        load_region = previous_load_region_;
    }
    std::uint64_t start = 0;
    if (fixed) {
        start = *fixed;
    } else if (!allocated) {
        start = 0;
    } else if (!region.empty()) {
        start = align_up(region_cursors_[region], alignment);
    } else {
        start = align_up(dot_, alignment);
    }
    start = mask(start);
    // The load address: AT(), the AT> region's next address, or the
    // virtual address moved as far as the last section of the region was.
    std::uint64_t load = start;
    if (statement && statement->load_address) {
        if (const auto value = evaluate(*statement->load_address)) load = value->value;
    } else if (!load_region.empty()) {
        std::uint64_t load_alignment = alignment;
        if (load_region != region) {
            load_alignment = 1;
            if (statement->alignment) {
                if (const auto value = evaluate(*statement->alignment)) load_alignment = value->value;
            }
        }
        load = align_up(region_cursors_[load_region], load_alignment);
    } else if (allocated) {
        if (const auto last = region_deltas_.find(region); last != region_deltas_.end()) {
            load = align_up(start + last->second, alignment);
        }
    }
    const auto outer = dot_;
    dot_ = start;
    section_base_ = start;
    current_ = &placement;
    output->address = start;
    output->alignment = alignment;
    for (auto& item : placement.items) {
        if (!item.item || std::holds_alternative<script::InputPattern>(*item.item)) {
            for (auto* section : item.sections) {
                dot_ = align_up(dot_, section->alignment);
                section->output = output;
                section->offset = dot_ - start;
                dot_ += section->size;
            }
        } else if (const auto* data = std::get_if<script::Data>(item.item)) {
            item.offset = dot_ - start;
            if (const auto value = evaluate(*data->value)) item.value = value->value;
            dot_ += data->size;
        } else if (const auto* assignment = std::get_if<script::Assignment>(item.item)) {
            assign(*assignment, output, final);
            item.value = assigned_value(*assignment);
        } else if (const auto* check = std::get_if<script::Assert>(item.item)) {
            const auto value = evaluate(*check->condition);
            if (final && value && value->value == 0) diagnostics_.error(check->location, check->message);
        }
    }
    output->size = dot_ - start;
    output->load_address = mask(load);
    placement.emitted = content;
    current_ = nullptr;
    if (!allocated) {
        dot_ = outer;
        return;
    }
    previous_region_ = region;
    previous_load_region_ = load_region;
    if (load_region.empty()) region_deltas_[region] = output->load_address - start;
    if (content) last_allocated_ = output;
    if (!region.empty()) region_cursors_[region] = dot_;
    const bool loaded_elsewhere = !load_region.empty() && load_region != region && !output->nobits();
    if (loaded_elsewhere) region_cursors_[load_region] = output->load_address + output->size;
    if (!final) return;
    for (const auto& entry : script_.regions) {
        const bool vma = entry.name == region;
        const bool lma = loaded_elsewhere && entry.name == load_region;
        if (!vma && !lma) continue;
        const auto origin = evaluate(*entry.origin);
        const auto length = evaluate(*entry.length);
        if (!origin || !length) continue;
        const auto begin = vma ? start : output->load_address;
        if (begin < origin->value || begin + output->size > origin->value + length->value) {
            diagnostics_.error(statement->location, "section '" + output->name +
                                                        "' will not fit in region '" + entry.name + "'");
        }
    }
}

bool Link::layout_pass(bool final) {
    final_ = final;
    expression_failed_ = false;
    dot_ = 0;
    current_ = nullptr;
    last_allocated_ = nullptr;
    assigned_.clear();
    region_cursors_.clear();
    region_deltas_.clear();
    previous_region_.clear();
    previous_load_region_.clear();
    for (const auto& region : script_.regions) {
        const auto origin = evaluate(*region.origin);
        region_cursors_[region.name] = origin ? origin->value : 0;
    }
    for (auto& step : steps_) {
        if (step.placement) {
            lay_out_section(*step.placement, final);
            continue;
        }
        if (const auto* assignment = std::get_if<script::Assignment>(step.command)) {
            assign(*assignment, nullptr, final);
            step.value = assigned_value(*assignment);
        } else if (const auto* check = std::get_if<script::Assert>(step.command)) {
            const auto value = evaluate(*check->condition);
            if (final && value && value->value == 0) diagnostics_.error(check->location, check->message);
        }
    }
    return !(final && (expression_failed_ || diagnostics_.errors() != 0));
}

bool Link::layout() {
    for (auto& placement : placements_) {
        if (!placement->output) continue;
        auto& output = *placement->output;
        output.sections.clear();
        output.flags = 0;
        output.alignment = 1;
        bool progbits = false;
        bool data = false;
        std::optional<std::uint32_t> type;
        std::optional<std::uint64_t> entry_size;
        bool mixed = false;
        // Mergeable flags survive when every input has the same ones.
        std::optional<std::uint64_t> merge_flags;
        for (const auto& item : placement->items) {
            if (item.item && std::holds_alternative<script::Data>(*item.item)) data = true;
            for (auto* section : item.sections) {
                output.sections.push_back(section);
                output.flags |= section->flags;
                output.alignment = std::max(output.alignment, section->alignment);
                if (!section->nobits()) progbits = true;
                const auto merging = section->flags & (elf::shf_merge | elf::shf_strings);
                if (!type) {
                    type = section->type;
                    entry_size = section->entry_size;
                    merge_flags = merging;
                } else {
                    if (*type != section->type) mixed = true;
                    if (*entry_size != section->entry_size) entry_size = 0;
                    if (*merge_flags != merging) merge_flags = 0;
                }
            }
        }
        output.flags &= ~(elf::shf_group | elf::shf_merge | elf::shf_strings | elf::shf_info_link |
                          elf::shf_exclude | std::uint64_t{0x80});
        output.entry_size = entry_size.value_or(0);
        if (output.entry_size != 0) output.flags |= merge_flags.value_or(0);
        const bool noload = placement->statement && placement->statement->noload;
        if (output.sections.empty()) {
            output.flags = elf::shf_alloc | elf::shf_write;
            output.type = data ? elf::sht_progbits : elf::sht_nobits;
        } else if (noload || (!progbits && !data)) {
            output.type = elf::sht_nobits;
        } else if (type && !mixed && *type != elf::sht_nobits) {
            output.type = *type;
        } else {
            output.type = elf::sht_progbits;
        }
    }
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
    std::vector<std::uint64_t> previous;
    for (int pass = 0; pass < 16; ++pass) {
        layout_pass(false);
        auto current = snapshot();
        if (current == previous) return layout_pass(true);
        previous = std::move(current);
    }
    diagnostics_.command_error("the linker script's addresses do not settle");
    return false;
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
            } else if (statement && statement->noload) {
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
                } else if (last->size != 0 && ((last_end - 1) & ~(page - 1)) == (section->load_address & ~(page - 1))) {
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
        const auto* first = segment.sections.front();
        // The file and program headers share the first segment when the
        // script left room for them before the first section.
        bool with_headers = segment.file_header || segment.program_headers;
        if (script_.phdrs.empty() && first_load && (first->address & (page - 1)) >= headers) {
            with_headers = true;
        }
        first_load = false;
        if (with_headers && (first->address & (page - 1)) >= headers) {
            segment.offset = 0;
            segment.address = first->address & ~(page - 1);
        } else {
            segment.offset = offset + ((first->address - offset) & (page - 1));
            segment.address = first->address;
        }
        segment.physical_address = segment.physical.value_or(first->load_address -
                                                             (first->address - segment.address));
        std::uint64_t file_end = segment.offset;
        std::uint64_t memory_end = segment.address;
        for (auto* section : segment.sections) {
            if (!placed.insert(section).second) continue;
            section->offset = segment.offset + (section->address - segment.address);
            if (!section->nobits()) file_end = std::max(file_end, section->offset + section->size);
            memory_end = std::max(memory_end, section->address + section->size);
        }
        segment.file_size = file_end - segment.offset;
        segment.memory_size = memory_end - segment.address;
        offset = std::max(offset, file_end);
    }
    for (const auto& step : steps_) {
        if (!step.placement || !step.placement->output || !step.placement->emitted) continue;
        auto* section = step.placement->output;
        if (placed.contains(section)) continue;
        if (section->nobits()) {
            section->offset = offset;
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
        segment.file_size = section->nobits() ? 0 : section->size;
        segment.memory_size = section->size;
        segment.alignment = section->alignment;
    }
}

} // namespace cross::linker
