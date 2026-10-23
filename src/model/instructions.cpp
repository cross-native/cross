// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/model.hpp"
#include "model/model_syntax.hpp"

#include "common/diagnostic.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <limits>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace cross {
namespace {

bool identifier(std::string_view text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) != 0 ||
                          text[0] == '_')) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

// A resource name: letters, digits, '_', '-', and '.'.
bool resource_name(std::string_view text) {
    return !text.empty() && std::isalpha(static_cast<unsigned char>(text[0])) != 0 &&
           std::all_of(text.begin(), text.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
                      c == '-' || c == '.';
           });
}

std::optional<std::int64_t> integer_text(std::string_view text) {
    std::int64_t value{};
    if (text.empty()) return std::nullopt;
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

std::string trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return std::string(text);
}

// The bits that a value from `minimum` to `maximum` needs: unsigned for a
// range without negative values, else two's complement.
unsigned value_bits(std::int64_t minimum, std::int64_t maximum) {
    if (minimum >= 0) {
        return static_cast<unsigned>(std::bit_width(static_cast<std::uint64_t>(maximum)));
    }
    unsigned bits = 1;
    while (bits < 64 && (minimum < -(std::int64_t{1} << (bits - 1)) ||
                         maximum >= (std::int64_t{1} << (bits - 1)))) {
        ++bits;
    }
    return bits;
}

// Whether the low `width` bits of the values from `minimum` to `maximum`
// tell them apart.
bool fits_field(std::int64_t minimum, std::int64_t maximum, unsigned width) {
    if (width >= 63) return true;
    return static_cast<std::uint64_t>(maximum) - static_cast<std::uint64_t>(minimum) <
           (std::uint64_t{1} << width);
}

std::string_view control_name(InstructionControlEffect control) {
    switch (control) {
    case InstructionControlEffect::UnconditionalBranch: return "jump";
    case InstructionControlEffect::ConditionalBranch: return "branch";
    case InstructionControlEffect::RawReturn: return "exit";
    case InstructionControlEffect::Trap: return "trap";
    case InstructionControlEffect::None: break;
    }
    return {};
}

constexpr std::string_view instruction_properties[] = {
    "architecture", "mnemonic", "operands", "encoding", "features", "excludes",
    "reads", "writes", "keeps", "hazards", "control", "condition", "delay_slot",
    "likely", "privileged", "volatile", "hazard_uses", "hazard_writes", "latency",
    "issue", "builtin", "alias", "macro", "expands", "otherwise", "constraints"};

// The comparisons of `condition`, by InstructionBranchCondition.
constexpr std::pair<std::string_view, InstructionBranchCondition> branch_conditions[] = {
    {"equal", InstructionBranchCondition::Equal},
    {"not-equal", InstructionBranchCondition::NotEqual},
    {"less-than-zero", InstructionBranchCondition::LessThanZero},
    {"greater-equal-zero", InstructionBranchCondition::GreaterEqualZero},
    {"less-equal-zero", InstructionBranchCondition::LessEqualZero},
    {"greater-than-zero", InstructionBranchCondition::GreaterThanZero},
    {"less-than", InstructionBranchCondition::LessThan},
    {"greater-equal", InstructionBranchCondition::GreaterEqual},
    {"less-than-unsigned", InstructionBranchCondition::LessThanUnsigned},
    {"greater-equal-unsigned", InstructionBranchCondition::GreaterEqualUnsigned},
};

constexpr std::size_t property_index(std::string_view name) {
    std::size_t index = 0;
    while (index < std::size(instruction_properties) && instruction_properties[index] != name) {
        ++index;
    }
    return index;
}

class EntryReader {
public:
    EntryReader(const ModelProperties& properties, ModelProblem& problem) : problem_(problem) {
        for (const auto& [name, property] : properties) {
            const auto index = property_index(name);
            if (index == std::size(instruction_properties)) {
                fail(property.line, "unknown model property '" + name + "'");
            } else {
                properties_[index] = &property;
            }
        }
    }

    bool fail(unsigned line, std::string message) {
        if (problem_.message.empty()) problem_ = {line, std::move(message)};
        return false;
    }

    [[nodiscard]] bool failed() const { return !problem_.message.empty(); }

    const ModelProperty* find(std::string_view name) const {
        return properties_[property_index(name)];
    }

    unsigned line(std::string_view name) const {
        const auto* property = find(name);
        return property ? property->line : 0;
    }

    std::optional<std::string> text(std::string_view name) {
        const auto* property = find(name);
        if (!property) return std::nullopt;
        if (property->value.kind != ModelValue::Kind::Text) {
            fail(property->line, "model property '" + std::string(name) + "' must be text");
            return std::nullopt;
        }
        return property->value.text;
    }

    const std::vector<std::string>& list(std::string_view name) {
        static const std::vector<std::string> none;
        const auto* property = find(name);
        if (!property) return none;
        if (property->value.kind != ModelValue::Kind::List) {
            fail(property->line, "model property '" + std::string(name) + "' must be a list");
            return none;
        }
        return property->value.list;
    }

    bool boolean(std::string_view name) {
        const auto* property = find(name);
        if (!property) return false;
        if (property->value.kind != ModelValue::Kind::Boolean) {
            fail(property->line, "model property '" + std::string(name) + "' must be boolean");
            return false;
        }
        return property->value.boolean;
    }

    std::optional<unsigned> integer(std::string_view name, unsigned minimum,
                                    unsigned maximum) {
        const auto* property = find(name);
        if (!property) return std::nullopt;
        if (property->value.kind != ModelValue::Kind::Integer || property->value.negative ||
            property->value.integer < minimum || property->value.integer > maximum) {
            fail(property->line, "model property '" + std::string(name) +
                                     "' must be an integer from " + std::to_string(minimum) +
                                     " to " + std::to_string(maximum));
            return std::nullopt;
        }
        return static_cast<unsigned>(property->value.integer);
    }

private:
    // By index in instruction_properties; the last slot stays empty.
    std::array<const ModelProperty*, std::size(instruction_properties) + 1> properties_{};
    ModelProblem& problem_;
};

// Reads one entry against its architecture's instruction model.
class InstructionMaker {
public:
    InstructionMaker(EntryReader& reader, const TargetInfo& target, ModelInstruction& entry)
        : reader_(reader), target_(target), model_(*target.instruction_model),
          entry_(entry) {}

    bool operands() {
        const auto line = reader_.line("operands");
        bool optional = false;
        for (const auto& item : reader_.list("operands")) {
            ModelOperand operand;
            operand.spelling = item;
            if (item.starts_with('$')) {
                const auto name = item.substr(1);
                const auto* reg = find_register(target_, name);
                const auto number = model_.register_number(name);
                if (!reg || !number) return fail(line, "unknown register '" + item + "'");
                if (optional) {
                    return fail(line, "literal operand '" + item +
                                          "' follows an optional operand");
                }
                operand.type_name = name;
                operand.type.kind = OperandKind::Register;
                operand.type.register_class = reg->register_class;
                operand.literal_number = *number;
                entry_.operands.push_back(std::move(operand));
                continue;
            }
            const auto colon = item.find(':');
            if (colon == std::string::npos) {
                return fail(line, "operand '" + item + "' needs a type, as in 'NAME:TYPE'");
            }
            operand.name = item.substr(0, colon);
            if (!identifier(operand.name)) {
                return fail(line, "invalid operand name '" + operand.name + "'");
            }
            if (find_operand(operand.name)) {
                return fail(line, "duplicate operand '" + operand.name + "'");
            }
            const auto rest = item.substr(colon + 1);
            const auto equals = rest.find('=');
            operand.type_name = rest.substr(0, equals);
            const auto& type = operand_type(operand.type_name);
            if (!type) {
                return fail(line, "unknown " + std::string(target_.architecture) +
                                      " operand type '" + operand.type_name + "'");
            }
            operand.type = *type;
            if (equals != std::string::npos) {
                const auto value = rest.substr(equals + 1);
                if (const auto number = integer_text(value)) {
                    if (type->kind != OperandKind::Immediate || *number < type->minimum ||
                        *number > type->maximum) {
                        return fail(line, "default of operand '" + operand.name +
                                              "' is not a value of type '" +
                                              operand.type_name + "'");
                    }
                    operand.default_value = *number;
                } else if (const auto* other = find_operand(value);
                           other && other->type.kind == type->kind &&
                           other->type.register_class == type->register_class &&
                           type->kind == OperandKind::Register) {
                    operand.default_operand = static_cast<unsigned>(other - entry_.operands.data());
                } else {
                    return fail(line, "default of operand '" + operand.name +
                                          "' must be an integer or an earlier register operand");
                }
                optional = true;
            } else if (optional) {
                return fail(line, "operand '" + operand.name + "' follows an optional operand");
            }
            entry_.operands.push_back(std::move(operand));
        }
        return !reader_.failed();
    }

    // A term of an encoding field or constraint: NAME, NAME.base, or
    // NAME.offset.
    std::optional<ModelTerm> term(std::string_view text, unsigned line, bool negative) {
        const auto dot = text.find('.');
        const auto name = text.substr(0, dot);
        const auto* operand = find_operand(name);
        if (!operand) {
            fail(line, "unknown operand '" + std::string(name) + "'");
            return std::nullopt;
        }
        ModelTerm result{static_cast<unsigned>(operand - entry_.operands.data()), 0, negative};
        if (dot != std::string_view::npos) {
            const auto part = text.substr(dot + 1);
            if (operand->type.kind != OperandKind::Memory || (part != "base" && part != "offset")) {
                fail(line, "operand '" + std::string(name) + "' has no part '" +
                               std::string(part) + "'");
                return std::nullopt;
            }
            result.part = part == "base" ? 1U : 2U;
        } else if (operand->type.kind == OperandKind::Memory) {
            fail(line, "name the base or offset of memory operand '" + std::string(name) +
                           "', as in '" + std::string(name) + ".base'");
            return std::nullopt;
        }
        return result;
    }

    // `EXPR`: terms joined by '+' and '-', and integers.
    bool expression(std::string_view text, unsigned line, std::vector<ModelTerm>& terms,
                    std::int64_t& constant) {
        bool negative = false;
        std::size_t start = 0;
        if (!text.empty() && text.front() == '-') {
            negative = true;
            start = 1;
        }
        while (start <= text.size()) {
            const auto end = text.find_first_of("+-", start);
            const auto piece = text.substr(start, end == std::string_view::npos
                                                      ? std::string_view::npos
                                                      : end - start);
            if (const auto number = integer_text(piece)) {
                constant += negative ? -*number : *number;
            } else if (const auto item = term(piece, line, negative)) {
                terms.push_back(*item);
            } else {
                return false;
            }
            if (end == std::string_view::npos) break;
            negative = text[end] == '-';
            start = end + 1;
        }
        return true;
    }

    // Range of a term's values.
    std::pair<std::int64_t, std::int64_t> term_range(const ModelTerm& item) const {
        const auto& type = entry_.operands[item.operand].type;
        if (item.part == 2) return {type.offset_minimum, type.offset_maximum};
        return {type.minimum, type.maximum};
    }

    bool encoding() {
        const auto line = reader_.line("encoding");
        const auto text = reader_.text("encoding");
        if (!text) return !reader_.failed();
        struct Group {
            unsigned width{};
            bool fixed{};
            std::uint64_t bits{};
            ModelField field;
            bool slice{};
            std::string spelling;
        };
        std::vector<Group> groups;
        std::size_t position = 0;
        while (position < text->size()) {
            if ((*text)[position] == ' ') {
                ++position;
                continue;
            }
            const auto end = std::min(text->find(' ', position), text->size());
            const std::string_view item(text->data() + position, end - position);
            position = end;
            Group group;
            group.spelling = std::string(item);
            if (item.find_first_not_of("01") == std::string_view::npos) {
                group.fixed = true;
                group.width = static_cast<unsigned>(item.size());
                for (const char c : item) group.bits = (group.bits << 1) | (c == '1' ? 1U : 0U);
                groups.push_back(std::move(group));
                continue;
            }
            if (item.back() == ']') {
                const auto open = item.find('[');
                if (open == std::string_view::npos) {
                    return fail(line, "invalid encoding field '" + group.spelling + "'");
                }
                const auto bits = item.substr(open + 1, item.size() - open - 2);
                const auto colon = bits.find(':');
                const auto high = integer_text(bits.substr(0, colon));
                const auto low = colon == std::string_view::npos
                                     ? high
                                     : integer_text(bits.substr(colon + 1));
                if (!high || !low || *low < 0 || *high < *low || *high >= 64) {
                    return fail(line, "invalid bit range in encoding field '" +
                                          group.spelling + "'");
                }
                const auto item_term = term(item.substr(0, open), line, false);
                if (!item_term) return false;
                group.field.terms.push_back(*item_term);
                group.field.low = static_cast<unsigned>(*low);
                group.width = static_cast<unsigned>(*high - *low + 1);
                group.slice = true;
                groups.push_back(std::move(group));
                continue;
            }
            const auto colon = item.rfind(':');
            const auto width = colon == std::string_view::npos
                                   ? std::nullopt
                                   : integer_text(item.substr(colon + 1));
            if (!width || *width < 1 || *width > 64) {
                return fail(line, "encoding field '" + group.spelling +
                                      "' needs a width, as in 'NAME:5'");
            }
            group.width = static_cast<unsigned>(*width);
            const auto expression_text = item.substr(0, colon);
            if (!find_operand(expression_text.substr(0, expression_text.find('.'))) &&
                identifier(expression_text)) {
                const auto number = model_.register_number(expression_text);
                if (!number || !find_register(target_, expression_text)) {
                    return fail(line, "'" + std::string(expression_text) +
                                          "' is neither an operand nor a register");
                }
                if (*number >= (1U << std::min(group.width, 31U))) {
                    return fail(line, "register '" + std::string(expression_text) +
                                          "' does not fit its " + std::to_string(group.width) +
                                          "-bit field");
                }
                group.field.register_number = *number;
                group.field.register_class =
                    find_register(target_, expression_text)->register_class;
                groups.push_back(std::move(group));
                continue;
            }
            if (!expression(expression_text, line, group.field.terms, group.field.constant)) {
                return false;
            }
            groups.push_back(std::move(group));
        }
        unsigned total = 0;
        for (const auto& group : groups) total += group.width;
        if (total != model_.instruction_bits) {
            return fail(line, "the encoding has " + std::to_string(total) + " bits; " +
                                  std::string(target_.architecture) + " instructions have " +
                                  std::to_string(model_.instruction_bits));
        }
        // Bits from the most significant down.
        unsigned next = total;
        // The slices of each part of each operand, by operand * 3 + part.
        std::vector<std::vector<std::pair<unsigned, unsigned>>> slices(entry_.operands.size() * 3);
        std::vector<bool> placed(entry_.operands.size() * 3);
        for (auto& group : groups) {
            next -= group.width;
            if (group.fixed) {
                entry_.fixed_bits |= group.bits << next;
                continue;
            }
            auto& field = group.field;
            field.shift = next;
            field.width = group.width;
            for (const auto& item : field.terms) placed[item.operand * 3 + item.part] = true;
            if (group.slice) {
                const auto& item = field.terms.front();
                slices[item.operand * 3 + item.part].emplace_back(field.low, field.width);
            } else if (field.terms.size() == 1) {
                // A field of one operand tells its values apart.
                const auto& item = field.terms.front();
                const auto& operand = entry_.operands[item.operand];
                auto [minimum, maximum] = term_range(item);
                if (item.negative) {
                    const auto low = -maximum;
                    maximum = -minimum;
                    minimum = low;
                }
                minimum += field.constant;
                maximum += field.constant;
                if (operand.type.kind != OperandKind::Label &&
                    !fits_field(minimum, maximum, field.width)) {
                    return fail(line, "field '" + group.spelling + "' cannot hold the values " +
                                          std::to_string(minimum) + " to " +
                                          std::to_string(maximum) + " of operand '" +
                                          operand.name + "'");
                }
            }
            entry_.fields.push_back(field);
        }
        const auto mentions = [](const ModelField& field, std::size_t index, unsigned part) {
            return std::any_of(field.terms.begin(), field.terms.end(), [&](const ModelTerm& item) {
                return item.operand == index && item.part == part;
            });
        };
        for (std::size_t index = 0; index < entry_.operands.size(); ++index) {
            const auto& operand = entry_.operands[index];
            if (operand.name.empty()) continue;
            const auto& type = operand.type;
            const bool memory = type.kind == OperandKind::Memory;
            // An operand of one value needs no field.
            const bool single = memory ? type.offset_minimum == type.offset_maximum
                                       : type.kind != OperandKind::Label &&
                                             type.minimum == type.maximum;
            if ((!placed[index * 3 + (memory ? 1 : 0)] && (memory || !single)) ||
                (memory && !single && !placed[index * 3 + 2])) {
                return fail(line, "the encoding does not place operand '" + operand.name + "'");
            }
            // Slices hold each bit of a part's values once, and no other field
            // holds the part.
            for (unsigned part = 0; part < 3; ++part) {
                auto pieces = slices[index * 3 + part];
                if (pieces.empty()) continue;
                std::sort(pieces.begin(), pieces.end());
                unsigned covered = 0;
                bool tiled = true;
                for (const auto& [low, width] : pieces) {
                    tiled = tiled && low == covered;
                    covered += width;
                }
                const auto bits = part == 2 ? value_bits(type.offset_minimum, type.offset_maximum)
                                            : value_bits(type.minimum, type.maximum);
                const auto others = std::count_if(
                    entry_.fields.begin(), entry_.fields.end(),
                    [&](const ModelField& field) { return mentions(field, index, part); });
                if (!tiled || covered != bits ||
                    static_cast<std::size_t>(others) != pieces.size()) {
                    return fail(line, "the slices of operand '" + operand.name +
                                          "' do not hold each of its " + std::to_string(bits) +
                                          " bits once");
                }
            }
            // A relocated value needs fields of its own that put its bits
            // where the architecture names.
            if (type.relocated_bits != 0) {
                const unsigned part = memory ? 2U : 0U;
                const auto wanted = type.relocated_slices.empty()
                                        ? std::vector<RelocatedSlice>{{0, type.relocated_bits, 0}}
                                        : type.relocated_slices;
                // Fields of the value alone, one unless they are slices.
                bool own = true;
                std::size_t uses = 0;
                for (const auto& field : entry_.fields) {
                    if (!mentions(field, index, part)) continue;
                    ++uses;
                    if (field.terms.size() != 1 || field.constant != 0 ||
                        field.terms[0].negative) {
                        own = false;
                    }
                }
                own = own && (uses == 1 || !slices[index * 3 + part].empty());
                const auto holds = [&](unsigned bit, unsigned at) {
                    return std::any_of(
                        entry_.fields.begin(), entry_.fields.end(), [&](const ModelField& field) {
                            return mentions(field, index, part) && bit >= field.low &&
                                   bit < field.low + field.width &&
                                   field.shift + (bit - field.low) == at;
                        });
                };
                for (const auto& slice : wanted) {
                    for (unsigned bit = 0; own && bit < slice.width; ++bit) {
                        own = holds(slice.low + bit, slice.shift + bit);
                    }
                }
                if (!own) {
                    const auto range = [](unsigned low, unsigned width) {
                        return std::to_string(low + width - 1) + "-" + std::to_string(low);
                    };
                    if (wanted.size() == 1 && wanted[0].low == 0 &&
                        wanted[0].width == type.relocated_bits) {
                        return fail(line, "operand '" + operand.name +
                                              "' needs a field of its own in bits " +
                                              range(wanted[0].shift, wanted[0].width));
                    }
                    std::string places;
                    for (std::size_t slice = 0; slice < wanted.size(); ++slice) {
                        places += slice == 0 ? "" : slice + 1 == wanted.size() ? " and " : ", ";
                        places += "bits " + range(wanted[slice].low, wanted[slice].width) +
                                  (slice == 0 ? " of its value" : "") + " in bits " +
                                  range(wanted[slice].shift, wanted[slice].width);
                    }
                    return fail(line, "operand '" + operand.name + "' needs fields of its own "
                                          "that hold " + places);
                }
            }
        }
        entry_.encoded = true;
        return true;
    }

    bool features(std::string_view property, std::vector<std::string>& destination) {
        const auto line = reader_.line(property);
        for (const auto& name : reader_.list(property)) {
            const auto& table = target_.subtargets->features;
            if (std::none_of(table.begin(), table.end(),
                             [&](const SubtargetFeature& feature) { return feature.name == name; })) {
                return fail(line, "unknown " + std::string(target_.architecture) + " feature '" +
                                      name + "'");
            }
            if (std::find(destination.begin(), destination.end(), name) != destination.end()) {
                return fail(line, "model property '" + std::string(property) + "' lists '" +
                                      name + "' twice");
            }
            destination.push_back(name);
        }
        return !reader_.failed();
    }

    bool effects(std::string_view property, std::vector<ModelEffect>& destination) {
        const auto line = reader_.line(property);
        for (const auto& name : reader_.list(property)) {
            ModelEffect effect;
            effect.name = name;
            if (const auto* operand = find_operand(name)) {
                if (operand->type.kind != OperandKind::Register) {
                    return fail(line, "operand '" + name + "' is not a register; name 'memory' "
                                          "or another resource");
                }
                effect.kind = ModelEffect::Kind::Operand;
                effect.operand = static_cast<unsigned>(operand - entry_.operands.data());
            } else if (find_register(target_, name)) {
                effect.kind = ModelEffect::Kind::Register;
            } else if (resource_name(name)) {
                effect.kind = ModelEffect::Kind::Resource;
            } else {
                return fail(line, "invalid resource name '" + name + "'");
            }
            if (std::any_of(destination.begin(), destination.end(),
                            [&](const ModelEffect& other) { return other.name == name; })) {
                return fail(line, "model property '" + std::string(property) + "' lists '" +
                                      name + "' twice");
            }
            destination.push_back(std::move(effect));
        }
        return !reader_.failed();
    }

    // `keeps`: register operands that the form writes.
    bool keeps() {
        const auto line = reader_.line("keeps");
        for (const auto& name : reader_.list("keeps")) {
            const auto* operand = find_operand(name);
            const auto index =
                operand ? static_cast<unsigned>(operand - entry_.operands.data()) : 0U;
            if (!operand || operand->type.kind != OperandKind::Register ||
                std::none_of(entry_.writes.begin(), entry_.writes.end(),
                             [&](const ModelEffect& effect) {
                                 return effect.kind == ModelEffect::Kind::Operand &&
                                        effect.operand == index;
                             })) {
                return fail(line, "'keeps' names '" + name +
                                      "', which is not a register operand that the form writes");
            }
            if (std::find(entry_.keeps.begin(), entry_.keeps.end(), index) != entry_.keeps.end()) {
                return fail(line, "model property 'keeps' lists '" + name + "' twice");
            }
            entry_.keeps.push_back(index);
        }
        return !reader_.failed();
    }

    bool hazard_facts() {
        // Resource names first, so that the facts can refer to them.
        std::vector<std::tuple<std::string, unsigned, std::optional<unsigned>,
                               std::optional<unsigned>, std::vector<std::uint64_t>, bool>>
            parsed;
        for (const bool writes : {false, true}) {
            const auto property = writes ? "hazard_writes" : "hazard_uses";
            const auto line = reader_.line(property);
            for (const auto& item : reader_.list(property)) {
                const auto condition = item.find(" if ");
                const std::string_view main(item.data(),
                                            condition == std::string::npos ? item.size() : condition);
                const auto at = main.rfind('@');
                const auto stage = at == std::string_view::npos
                                       ? std::nullopt
                                       : integer_text(main.substr(at + 1));
                if (!stage || *stage < 0 || *stage > 255) {
                    return fail(line, "hazard fact '" + item +
                                          "' needs a stage, as in 'resource@4'");
                }
                auto head = main.substr(0, at);
                std::optional<unsigned> operand;
                if (head.ends_with(']')) {
                    const auto open = head.find('[');
                    if (open == std::string_view::npos) {
                        return fail(line, "invalid hazard fact '" + item + "'");
                    }
                    operand = immediate_operand(head.substr(open + 1, head.size() - open - 2), line);
                    if (!operand) return false;
                    head = head.substr(0, open);
                }
                if (!resource_name(head)) return fail(line, "invalid hazard fact '" + item + "'");
                std::optional<unsigned> when;
                std::vector<std::uint64_t> values;
                if (condition != std::string::npos) {
                    const std::string_view rest(item.data() + condition + 4);
                    const auto equals = rest.find('=');
                    if (equals == std::string_view::npos) {
                        return fail(line, "hazard fact condition must be 'if OPERAND=V|V'");
                    }
                    when = immediate_operand(rest.substr(0, equals), line);
                    if (!when) return false;
                    std::size_t start = equals + 1;
                    while (start <= rest.size()) {
                        const auto bar = rest.find('|', start);
                        const auto value = integer_text(rest.substr(
                            start, bar == std::string_view::npos ? std::string_view::npos
                                                                 : bar - start));
                        if (!value || *value < 0) {
                            return fail(line, "hazard fact condition must be 'if OPERAND=V|V'");
                        }
                        values.push_back(static_cast<std::uint64_t>(*value));
                        if (bar == std::string_view::npos) break;
                        start = bar + 1;
                    }
                }
                parsed.emplace_back(std::string(head), static_cast<unsigned>(*stage), operand, when,
                                    std::move(values), writes);
            }
        }
        for (const auto& item : parsed) {
            if (std::find(entry_.hazard_resources.begin(), entry_.hazard_resources.end(),
                          std::get<0>(item)) == entry_.hazard_resources.end()) {
                entry_.hazard_resources.push_back(std::get<0>(item));
            }
        }
        for (auto& [resource, stage, operand, when, values, writes] : parsed) {
            const auto& stored = *std::find(entry_.hazard_resources.begin(),
                                            entry_.hazard_resources.end(), resource);
            HazardFact fact{stored, stage, operand, when, std::move(values)};
            (writes ? entry_.hazard_writes : entry_.hazard_uses).push_back(std::move(fact));
        }
        return true;
    }

    std::optional<unsigned> immediate_operand(std::string_view name, unsigned line) {
        const auto* operand = find_operand(name);
        if (!operand || operand->type.kind != OperandKind::Immediate) {
            fail(line, "'" + std::string(name) + "' is not an immediate operand");
            return std::nullopt;
        }
        return static_cast<unsigned>(operand - entry_.operands.data());
    }

    bool expansions(std::string_view property, std::vector<ModelExpansion>& destination) {
        const auto line = reader_.line(property);
        for (const auto& item : reader_.list(property)) {
            ModelExpansion expansion;
            const auto space = item.find(' ');
            expansion.mnemonic = item.substr(0, space);
            if (expansion.mnemonic.empty()) return fail(line, "empty expansion line");
            if (space != std::string::npos) {
                std::size_t start = space + 1;
                while (start <= item.size()) {
                    const auto comma = item.find(',', start);
                    const auto piece = trim(std::string_view(item).substr(
                        start, comma == std::string::npos ? std::string::npos : comma - start));
                    ModelExpansion::Operand operand;
                    if (piece.starts_with('$')) {
                        const auto name = piece.substr(1);
                        const auto number = model_.register_number(name);
                        if (!number || !find_register(target_, name)) {
                            return fail(line, "unknown register '" + piece + "'");
                        }
                        operand.kind = ModelExpansion::Operand::Kind::Register;
                        operand.register_name = name;
                        operand.register_number = *number;
                    } else {
                        const bool negated = piece.starts_with('-');
                        const auto name = negated ? piece.substr(1) : piece;
                        const auto* source = find_operand(name);
                        if (!source || source->name.empty()) {
                            return fail(line, "unknown operand '" + name + "' in '" + item + "'");
                        }
                        if (negated && source->type.kind != OperandKind::Immediate &&
                            source->type.kind != OperandKind::Expression) {
                            return fail(line, "only an immediate operand can be negated in '" +
                                                  item + "'");
                        }
                        operand.kind = negated ? ModelExpansion::Operand::Kind::Negated
                                               : ModelExpansion::Operand::Kind::Operand;
                        operand.operand = static_cast<unsigned>(source - entry_.operands.data());
                    }
                    expansion.operands.push_back(std::move(operand));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            destination.push_back(std::move(expansion));
        }
        return !reader_.failed();
    }

    bool constraints() {
        const auto line = reader_.line("constraints");
        for (const auto& item : reader_.list("constraints")) {
            using Comparison = ModelConstraint::Comparison;
            constexpr std::pair<std::string_view, Comparison> comparisons[] = {
                {"<=", Comparison::LessEqual}, {">=", Comparison::GreaterEqual},
                {"==", Comparison::Equal},     {"!=", Comparison::NotEqual},
                {"<", Comparison::Less},       {">", Comparison::Greater},
            };
            ModelConstraint constraint;
            constraint.text = item;
            std::size_t at = std::string::npos;
            std::size_t length = 0;
            for (const auto& [spelling, comparison] : comparisons) {
                at = item.find(spelling);
                if (at != std::string::npos) {
                    constraint.comparison = comparison;
                    length = spelling.size();
                    break;
                }
            }
            if (at == std::string::npos) {
                return fail(line, "constraint '" + item + "' needs a comparison, as in "
                                      "'pos+size <= 32'");
            }
            const auto left = trim(std::string_view(item).substr(0, at));
            const auto right = trim(std::string_view(item).substr(at + length));
            const auto bound = integer_text(right);
            if (!bound) return fail(line, "constraint '" + item + "' must compare with an integer");
            constraint.bound = *bound;
            std::string compact;
            for (const char c : left) {
                if (c != ' ') compact.push_back(c);
            }
            if (!expression(compact, line, constraint.terms, constraint.constant)) return false;
            for (const auto& item_term : constraint.terms) {
                const auto kind = entry_.operands[item_term.operand].type.kind;
                if (kind != OperandKind::Immediate && kind != OperandKind::Register) {
                    return fail(line, "constraint '" + item +
                                          "' names an operand that is neither an immediate "
                                          "nor a register");
                }
            }
            entry_.constraints.push_back(std::move(constraint));
        }
        return !reader_.failed();
    }

private:
    bool fail(unsigned line, std::string message) { return reader_.fail(line, std::move(message)); }

    // The target's operand type `name`, built once per architecture.
    const std::optional<OperandType>& operand_type(const std::string& name) {
        static std::unordered_map<std::string, std::optional<OperandType>> types;
        const auto key = std::string(target_.architecture) + '\n' + name;
        const auto found = types.find(key);
        if (found != types.end()) return found->second;
        return types.emplace(key, model_.operand_type(name)).first->second;
    }

    const ModelOperand* find_operand(std::string_view name) const {
        for (const auto& operand : entry_.operands) {
            if (!operand.name.empty() && operand.name == name) return &operand;
        }
        return nullptr;
    }

    EntryReader& reader_;
    const TargetInfo& target_;
    const InstructionModel& model_;
    ModelInstruction& entry_;
};

std::string operand_signature(const ModelInstruction& entry) {
    std::string result;
    for (const auto& operand : entry.operands) {
        if (!result.empty()) result += ", ";
        result += operand.spelling;
    }
    return result;
}

std::string effect_signature(const ModelInstruction& entry) {
    std::string result;
    const auto add = [&](std::string_view key, const auto& items, const auto& name_of) {
        if (items.empty()) return;
        if (!result.empty()) result += ' ';
        result += key;
        result += '=';
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index != 0) result += ',';
            result += name_of(items[index]);
        }
    };
    const auto effect_name = [](const ModelEffect& effect) -> const std::string& {
        return effect.name;
    };
    const auto text = [](const std::string& item) -> const std::string& { return item; };
    add("reads", entry.reads, effect_name);
    add("writes", entry.writes, effect_name);
    add("hazards", entry.hazards, text);
    const auto flag = [&](std::string_view item) {
        if (!result.empty()) result += ' ';
        result += item;
    };
    if (entry.control != InstructionControlEffect::None) {
        flag("control=" + std::string(control_name(entry.control)));
    }
    for (const auto& [name, condition] : branch_conditions) {
        if (condition == entry.branch_condition) flag("condition=" + std::string(name));
    }
    if (entry.delay_slot) flag("delay_slot");
    if (entry.likely) flag("likely");
    return result;
}

// The entry an alias re-spells, or the entry.
const ModelInstruction& spelled_entry(const InstructionTable& table,
                                      const ModelInstruction& entry) {
    return entry.is_alias() ? table.entries[entry.alias_entry] : entry;
}

// Whether the operands of `entry` can take the operands of an expansion
// line of `macro`.
bool expansion_matches(const ModelInstruction& macro, const ModelExpansion& line,
                       const ModelInstruction& entry) {
    // A line may leave out the optional operands.
    if (entry.operands.size() < line.operands.size()) return false;
    for (auto index = line.operands.size(); index < entry.operands.size(); ++index) {
        const auto& omitted = entry.operands[index];
        if (!omitted.default_value && !omitted.default_operand) return false;
    }
    for (std::size_t index = 0; index < line.operands.size(); ++index) {
        const auto& field = entry.operands[index];
        const auto& source = line.operands[index];
        using Kind = ModelExpansion::Operand::Kind;
        if (source.kind == Kind::Register) {
            if (field.type.kind != OperandKind::Register) return false;
            if (field.name.empty() && field.literal_number != source.register_number) return false;
            continue;
        }
        const auto& type = macro.operands[source.operand].type;
        if (!field.name.empty() && field.type.kind == OperandKind::Immediate &&
            (type.kind == OperandKind::Immediate || type.kind == OperandKind::Expression)) {
            continue;
        }
        if (source.kind == Kind::Operand && type.kind == OperandKind::Register &&
            field.type.kind == OperandKind::Register && !field.name.empty() &&
            field.type.register_class == type.register_class) {
            continue;
        }
        return false;
    }
    return true;
}

bool names_operand(const std::vector<ModelEffect>& effects, unsigned index) {
    return std::any_of(effects.begin(), effects.end(), [&](const ModelEffect& effect) {
        return effect.kind == ModelEffect::Kind::Operand && effect.operand == index;
    });
}

bool names_resource(const std::vector<ModelEffect>& effects, std::string_view name) {
    return std::any_of(effects.begin(), effects.end(), [&](const ModelEffect& effect) {
        return effect.kind != ModelEffect::Kind::Operand && effect.name == name;
    });
}

// The role of operand `index` in a form of `count` operands: a register
// that it writes is an output, one whose value it also reads or keeps, or
// that an omitted operand reads through its default, is both; a memory
// operand follows the form's effects on memory.
InstructionOperandRole operand_role(const ModelInstruction& entry, unsigned index,
                                    std::size_t count) {
    bool read = false;
    bool written = false;
    if (entry.operands[index].type.kind == OperandKind::Memory) {
        read = names_resource(entry.reads, "memory");
        written = names_resource(entry.writes, "memory");
    } else {
        read = names_operand(entry.reads, index) ||
               std::find(entry.keeps.begin(), entry.keeps.end(), index) != entry.keeps.end();
        written = names_operand(entry.writes, index);
        for (auto omitted = count; omitted < entry.operands.size(); ++omitted) {
            if (entry.operands[omitted].default_operand != index) continue;
            read = read || names_operand(entry.reads, static_cast<unsigned>(omitted));
            written = written || names_operand(entry.writes, static_cast<unsigned>(omitted));
        }
    }
    if (read && written) return InstructionOperandRole::InOut;
    return written ? InstructionOperandRole::Output : InstructionOperandRole::Input;
}

// Why an encoded entry gives no built-in form, completing "has no built-in
// form: "; empty when it gives forms.
std::string builtin_unavailability(const ModelInstruction& entry) {
    const bool transfers = entry.control == InstructionControlEffect::UnconditionalBranch ||
                           entry.control == InstructionControlEffect::ConditionalBranch;
    bool label = false;
    for (const auto& operand : entry.operands) {
        if (operand.name.empty()) continue;
        if (!operand.type.exposable) {
            return "its operand '" + operand.spelling + "' cannot be a built-in operand";
        }
        if (operand.type.kind == OperandKind::Label) {
            if (!transfers) {
                return "its label operand '" + operand.spelling + "' needs a jump or a branch";
            }
            label = true;
        }
    }
    if (!transfers) {
        for (const auto& constraint : entry.constraints) {
            for (const auto& term : constraint.terms) {
                if (entry.operands[term.operand].type.kind == OperandKind::Register) {
                    return "its register operands must satisfy '" + constraint.text +
                           "', which register allocation does not arrange";
                }
            }
        }
        return {};
    }
    // A branch built-in is a comparison and a branch to a label, whatever
    // instruction does them: anything else that the form does is lost.
    if (!label) return "it transfers control to an address in a register";
    if (!entry.writes.empty()) {
        return "it writes '" + entry.writes.front().name + "' as it transfers control";
    }
    if (entry.control == InstructionControlEffect::ConditionalBranch &&
        entry.branch_condition == InstructionBranchCondition::None) {
        return "it branches on a condition that its entry does not state";
    }
    for (const auto& effect : entry.reads) {
        if (effect.kind != ModelEffect::Kind::Operand) {
            return "it reads '" + effect.name + "' as it transfers control";
        }
    }
    return {};
}

// The built-in name of a mnemonic: `$::_` and the mnemonic with `_` for
// each `.`, as `$::_add_s` for `add.s`.
std::string builtin_name(std::string_view mnemonic) {
    std::string result = "$::_";
    for (const char c : mnemonic) result.push_back(c == '.' ? '_' : c);
    return result;
}

// Whether `feature` gives `other` through the prerequisites of the target's
// features.
bool implies(const TargetInfo& target, std::string_view feature, std::string_view other) {
    if (feature == other) return true;
    for (const auto& item : target.subtargets->features) {
        if (item.name != feature) continue;
        return std::any_of(item.prerequisites.begin(), item.prerequisites.end(),
                           [&](std::string_view next) { return implies(target, next, other); });
    }
    return false;
}

// The registry forms named `name` of the encoded entry `entry`, which entry
// `index` spells: one for each feature that has the instruction, each
// register width that its operands take (one width for all of them), and
// each number of its optional operands. Literal registers are no built-in
// operands; the forms print them.
void add_builtin_forms(const TargetInfo& target, std::string_view name,
                       const ModelInstruction& entry, InstructionCostId cost, std::size_t index,
                       InstructionTable& table) {
    std::vector<std::string_view> alternatives;
    for (const auto& feature : entry.features) alternatives.push_back(feature);
    if (alternatives.empty()) alternatives.push_back(target.architecture);
    std::size_t required = 0;
    std::size_t widths = 1;
    for (const auto& operand : entry.operands) {
        if (!operand.default_value && !operand.default_operand) ++required;
        widths = std::max(widths, operand.type.widths.size());
    }
    for (const auto alternative : alternatives) {
        for (std::size_t width = 0; width < widths; ++width) {
            for (auto count = required; count <= entry.operands.size(); ++count) {
                InstructionEntry form;
                form.name = name;
                form.assembly_mnemonic = entry.mnemonic;
                std::vector<std::string_view> required_features;
                std::vector<std::string_view> forbidden(entry.excludes.begin(),
                                                        entry.excludes.end());
                // The built-in operand of each entry operand.
                std::vector<std::optional<unsigned>> positions(entry.operands.size());
                for (std::size_t slot = 0; slot < count; ++slot) {
                    const auto& operand = entry.operands[slot];
                    const auto& type = operand.type;
                    if (operand.name.empty()) {
                        form.assembly_literals.push_back(
                            {static_cast<unsigned>(form.operands.size()), operand.spelling});
                        continue;
                    }
                    InstructionOperandEntry field;
                    field.role = operand_role(entry, static_cast<unsigned>(slot), count);
                    switch (type.kind) {
                    case OperandKind::Register: {
                        field.allow_register = true;
                        field.register_class = type.register_class;
                        for (const auto& reg : target.registers) {
                            if (reg.register_class == type.register_class) {
                                field.register_bits = reg.bits;
                                break;
                            }
                        }
                        const auto& chosen = type.widths[std::min(width, type.widths.size() - 1)];
                        field.value_bits = chosen.bits;
                        if (!chosen.feature.empty()) required_features.push_back(chosen.feature);
                        break;
                    }
                    case OperandKind::Immediate:
                        field.allow_immediate = true;
                        field.immediate_bits = value_bits(type.minimum, type.maximum);
                        field.immediate_signed = type.minimum < 0;
                        field.immediate_range = true;
                        field.immediate_minimum = type.minimum;
                        field.immediate_maximum = type.maximum;
                        field.assembly_register_number = type.register_syntax;
                        break;
                    case OperandKind::Memory:
                        field.allow_memory = true;
                        field.memory_bits = type.memory_bits;
                        field.memory_offset_minimum = type.offset_minimum;
                        field.memory_offset_maximum = type.offset_maximum;
                        break;
                    case OperandKind::Label:
                        // A label of the function the built-in is in.
                        field.allow_label = true;
                        field.label_scope = InstructionLabelScope::SameFunction;
                        break;
                    case OperandKind::Expression: break;
                    }
                    required_features.insert(required_features.end(),
                                             type.required_features.begin(),
                                             type.required_features.end());
                    forbidden.insert(forbidden.end(), type.forbidden_features.begin(),
                                     type.forbidden_features.end());
                    positions[slot] = static_cast<unsigned>(form.operands.size());
                    form.operands.push_back(field);
                }
                // Omitted operands keep their defaults in the constraints;
                // a branch's constraints on its registers concern only its
                // own encoding.
                for (const auto& constraint : entry.constraints) {
                    if (std::any_of(constraint.terms.begin(), constraint.terms.end(),
                                    [&](const ModelTerm& term) {
                                        return entry.operands[term.operand].type.kind ==
                                               OperandKind::Register;
                                    })) {
                        continue;
                    }
                    InstructionConstraint relation;
                    relation.constant = constraint.constant;
                    relation.comparison = constraint.comparison;
                    relation.bound = constraint.bound;
                    for (const auto& term : constraint.terms) {
                        if (const auto position = positions[term.operand]) {
                            relation.terms.emplace_back(*position, term.negative);
                        } else {
                            const auto value =
                                entry.operands[term.operand].default_value.value_or(0);
                            relation.constant += term.negative ? -value : value;
                        }
                    }
                    form.constraints.push_back(std::move(relation));
                }
                std::vector<std::string_view> unique;
                for (const auto feature : required_features) {
                    if (!implies(target, alternative, feature) &&
                        std::find(unique.begin(), unique.end(), feature) == unique.end()) {
                        unique.push_back(feature);
                    }
                }
                form.feature = alternative;
                if (alternative == target.architecture && !unique.empty()) {
                    form.feature = unique.front();
                    unique.erase(unique.begin());
                }
                form.required_features = std::move(unique);
                std::vector<std::string_view> excluded;
                for (const auto feature : forbidden) {
                    if (std::find(excluded.begin(), excluded.end(), feature) == excluded.end()) {
                        excluded.push_back(feature);
                    }
                }
                form.forbidden_features = std::move(excluded);
                for (const auto& effect : entry.reads) {
                    if (effect.kind != ModelEffect::Kind::Operand) {
                        form.implicit_reads.push_back(effect.name);
                    }
                }
                for (const auto& effect : entry.writes) {
                    if (effect.kind != ModelEffect::Kind::Operand) {
                        form.implicit_writes.push_back(effect.name);
                    }
                }
                form.control = entry.control;
                form.branch_condition = entry.branch_condition;
                form.privileged = entry.privileged;
                form.volatile_effect = entry.volatile_effect;
                // Hazard facts name the built-in's operands; a form that
                // leaves out an operand they name has none.
                bool facts = true;
                const auto remap = [&](std::vector<HazardFact> items) {
                    for (auto& fact : items) {
                        for (auto* operand : {&fact.operand, &fact.when_operand}) {
                            if (!*operand) continue;
                            facts = facts && positions[**operand].has_value();
                            if (positions[**operand]) *operand = *positions[**operand];
                        }
                    }
                    return items;
                };
                form.hazard_uses = remap(entry.hazard_uses);
                form.hazard_writes = remap(entry.hazard_writes);
                if (!facts) continue;
                form.delay_slot = entry.delay_slot;
                table.builtins.push_back(std::move(form));
                table.builtin_costs.push_back(cost);
                table.builtin_entries.push_back(index);
            }
        }
    }
}

// Checks one architecture's table as a whole and derives what the target
// takes from it.
bool complete_table(const TargetInfo& target, InstructionTable& table,
                    Diagnostics& diagnostics) {
    const auto& model = *target.instruction_model;
    const auto fail = [&](const std::string& position, std::string message) {
        diagnostics.file_error(position, std::move(message));
        return false;
    };
    table.forms.clear();
    table.costs.clear();
    table.cost_listing.clear();
    table.builtins.clear();
    table.builtin_costs.clear();
    table.builtin_entries.clear();
    table.unexposed.clear();
    auto& entries = table.entries;
    std::unordered_map<std::string_view, std::size_t> names;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!names.emplace(entries[index].name, index).second) {
            return fail(entries[index].position(),
                        "duplicate " + table.architecture + " instruction '" +
                            entries[index].name + "'");
        }
    }
    for (auto& entry : entries) {
        if (!entry.is_alias()) continue;
        const auto found = names.find(entry.alias);
        if (found == names.end()) {
            return fail(entry.position("alias"),
                        "alias of unknown " + table.architecture + " instruction '" + entry.alias +
                            "'");
        }
        if (entries[found->second].is_alias()) {
            return fail(entry.position("alias"), "alias of alias '" + entry.alias + "'");
        }
        entry.alias_entry = found->second;
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        table.forms[entries[index].mnemonic].push_back(index);
    }
    // Expansion lines emit encoded forms that take their operands.
    for (auto& entry : entries) {
        for (const bool alternative : {false, true}) {
            auto& lines = alternative ? entry.otherwise : entry.expands;
            for (auto& line : lines) {
                const auto found = table.forms.find(line.mnemonic);
                bool matched = false;
                if (found != table.forms.end()) {
                    for (const auto candidate : found->second) {
                        const auto& form = spelled_entry(table, entries[candidate]);
                        if (!form.encoded || !expansion_matches(entry, line, form)) continue;
                        line.entry = entries[candidate].is_alias()
                                         ? entries[candidate].alias_entry
                                         : candidate;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    return fail(entry.position(alternative ? "otherwise" : "expands"),
                                "no " + table.architecture + " instruction '" + line.mnemonic +
                                    "' takes the operands of an expansion line of '" +
                                    entry.name + "'");
                }
            }
        }
    }
    // Forms that the assembler cannot tell apart.
    for (const auto& [mnemonic, indices] : table.forms) {
        for (std::size_t later = 1; later < indices.size(); ++later) {
            const auto& second = entries[indices[later]];
            const auto& second_form = spelled_entry(table, second);
            for (std::size_t earlier = 0; earlier < later; ++earlier) {
                const auto& first_form = spelled_entry(table, entries[indices[earlier]]);
                const auto types = [](const ModelInstruction& form) {
                    std::vector<std::string> result;
                    for (const auto& operand : form.operands) result.push_back(operand.type_name);
                    return result;
                };
                const auto sorted = [](std::vector<std::string> items) {
                    std::sort(items.begin(), items.end());
                    return items;
                };
                // Forms that differ in their constraints differ by the
                // values of their operands.
                const auto constraints = [&](const ModelInstruction& form) {
                    std::vector<std::string> result;
                    for (const auto& constraint : form.constraints) {
                        result.push_back(constraint.text);
                    }
                    return sorted(result);
                };
                if (types(first_form) == types(second_form) &&
                    sorted(first_form.features) == sorted(second_form.features) &&
                    sorted(first_form.excludes) == sorted(second_form.excludes) &&
                    constraints(first_form) == constraints(second_form)) {
                    return fail(second.position(),
                                "instruction '" + second.name + "' duplicates the form of '" +
                                    std::string(mnemonic) + "' that '" +
                                    entries[indices[earlier]].name + "' defines");
                }
            }
        }
    }
    // The instructions that the backend selects are the shipped ones, as it
    // expects them.
    for (const auto& selected : model.selected) {
        const auto found = table.forms.find(selected.mnemonic);
        if (found != table.forms.end()) {
            for (const auto index : found->second) {
                if (!entries[index].shipped) {
                    return fail(entries[index].position(),
                                "instruction '" + entries[index].name +
                                    "' adds a form to '" + std::string(selected.mnemonic) +
                                    "', which the compiler selects with its shipped forms");
                }
            }
        }
        // The forms the compiler expects as operands and effects; forms with
        // the same operands, such as those of different ISA levels, may
        // differ in their effects.
        std::vector<std::pair<std::string, std::string>> expectations;
        for (const auto expected : selected.forms) {
            const auto bar = expected.find('|');
            expectations.emplace_back(
                trim(expected.substr(0, bar)),
                bar == std::string_view::npos ? std::string{} : trim(expected.substr(bar + 1)));
        }
        if (found != table.forms.end()) {
            for (const auto index : found->second) {
                const auto& form = spelled_entry(table, entries[index]);
                // A macro's instructions have the effects.
                if (!form.encoded) continue;
                const auto operands = operand_signature(form);
                const auto effects = effect_signature(form);
                std::string expected_effects;
                bool selected_operands = false;
                bool matched = false;
                for (const auto& [expected_operands, expected] : expectations) {
                    if (expected_operands != operands) continue;
                    selected_operands = true;
                    matched = matched || expected == effects;
                    if (expected_effects.empty()) expected_effects = expected;
                }
                if (selected_operands && !matched) {
                    return fail(entries[index].position(),
                                "instruction '" + entries[index].name + "' has the effects '" +
                                    effects + "'; the compiler selects it with '" +
                                    expected_effects + "'");
                }
            }
        }
        for (const auto& [operands, effects] : expectations) {
            bool present = false;
            if (found != table.forms.end()) {
                for (const auto index : found->second) {
                    const auto& form = spelled_entry(table, entries[index]);
                    if (operand_signature(form) == operands &&
                        (!form.encoded || effect_signature(form) == effects)) {
                        present = true;
                    }
                }
            }
            if (!present) {
                return fail(entries.empty() ? std::string(target.architecture)
                                            : entries.front().origin,
                            "the compiler selects " + table.architecture + " instruction '" +
                                std::string(selected.mnemonic) + " " + operands +
                                "', which no instruction entry defines");
            }
        }
    }
    // The costs of a mnemonic come from its first form.
    std::unordered_map<std::string_view, std::size_t> first_form;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (entry.is_alias()) continue;
        const auto [found, inserted] = first_form.emplace(entry.mnemonic, index);
        if (!inserted && (entry.latency || entry.issue)) {
            return fail(entry.position(entry.latency ? "latency" : "issue"),
                        "instruction '" + entry.name + "' states costs, which '" + entry.mnemonic +
                            "' takes from its first form '" + entries[found->second].name + "'");
        }
    }
    std::unordered_map<std::string_view, InstructionCostId> costs;
    const auto add_cost = [&](std::string_view mnemonic) {
        const auto& entry = entries[first_form.at(mnemonic)];
        const InstructionCostId id{static_cast<std::uint16_t>(table.costs.size())};
        table.costs.push_back({entry.mnemonic, entry.latency.value_or(1), entry.issue.value_or(1)});
        costs.emplace(entry.mnemonic, id);
    };
    for (const auto& selected : model.selected) add_cost(selected.mnemonic);
    for (const auto& entry : entries) {
        if (entry.is_alias() || costs.contains(entry.mnemonic)) continue;
        add_cost(entry.mnemonic);
    }
    if (table.costs.size() > std::numeric_limits<std::uint16_t>::max()) {
        return fail(entries.back().position(), "too many instructions");
    }
    std::unordered_set<std::string_view> listed;
    for (const auto& entry : entries) {
        if (!entry.is_alias() && listed.insert(entry.mnemonic).second) {
            table.cost_listing.push_back(costs.at(entry.mnemonic));
        }
    }
    // Every encoded form gives a built-in, named after its mnemonic unless
    // `builtin` names it, where its operands and effects allow one.
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto& entry = entries[index];
        const auto& form = spelled_entry(table, entry);
        entry.unexposed.clear();
        if (entry.no_builtin) {
            entry.builtin = builtin_name(entry.mnemonic);
            entry.unexposed = "has no built-in form: its instruction entry gives none";
            continue;
        }
        if (entry.builtin.empty()) entry.builtin = builtin_name(entry.mnemonic);
        if (!form.encoded) {
            entry.unexposed = "is an assembler macro, not a machine instruction";
            continue;
        }
        if (auto reason = builtin_unavailability(form); !reason.empty()) {
            entry.unexposed = "has no built-in form: " + reason;
            continue;
        }
        add_builtin_forms(target, entry.builtin, form, costs.at(form.mnemonic), index, table);
    }
    // Forms of one built-in that take the same operands under the same
    // features would be ambiguous.
    std::unordered_map<std::string_view, std::vector<std::size_t>> by_name;
    for (std::size_t index = 0; index < table.builtins.size(); ++index) {
        by_name[table.builtins[index].name].push_back(index);
    }
    const auto same_forms = [](const InstructionEntry& a, const InstructionEntry& b) {
        if (a.feature != b.feature || a.required_features != b.required_features ||
            a.forbidden_features != b.forbidden_features ||
            a.operands.size() != b.operands.size() ||
            a.constraints.size() != b.constraints.size()) {
            return false;
        }
        for (std::size_t slot = 0; slot < a.operands.size(); ++slot) {
            const auto& x = a.operands[slot];
            const auto& y = b.operands[slot];
            if (x.allow_register != y.allow_register || x.allow_immediate != y.allow_immediate ||
                x.allow_memory != y.allow_memory || x.allow_label != y.allow_label ||
                x.register_class != y.register_class || x.value_bits != y.value_bits ||
                x.memory_bits != y.memory_bits || x.immediate_minimum != y.immediate_minimum ||
                x.immediate_maximum != y.immediate_maximum) {
                return false;
            }
        }
        for (std::size_t index = 0; index < a.constraints.size(); ++index) {
            const auto& x = a.constraints[index];
            const auto& y = b.constraints[index];
            if (x.terms != y.terms || x.constant != y.constant ||
                x.comparison != y.comparison || x.bound != y.bound) {
                return false;
            }
        }
        return true;
    };
    // Branch forms that differ only in the register constraints of their
    // encodings, such as Release 6 BEQC with its operands either way round,
    // compare and branch alike: the first stands for them.
    const auto same_branch = [](const InstructionEntry& a, const InstructionEntry& b) {
        return a.control != InstructionControlEffect::None && a.control == b.control &&
               a.branch_condition == b.branch_condition &&
               a.implicit_reads == b.implicit_reads && a.implicit_writes == b.implicit_writes;
    };
    std::vector<bool> redundant(table.builtins.size());
    for (const auto& [name, indices] : by_name) {
        for (std::size_t later = 1; later < indices.size(); ++later) {
            for (std::size_t earlier = 0; earlier < later; ++earlier) {
                const auto& first = table.builtins[indices[earlier]];
                const auto& second = table.builtins[indices[later]];
                if (redundant[indices[earlier]] || !same_forms(first, second)) continue;
                if (same_branch(first, second)) {
                    redundant[indices[later]] = true;
                    break;
                }
                const auto& entry = entries[table.builtin_entries[indices[later]]];
                return fail(entry.position("builtin"),
                            "instruction '" + entry.name + "' gives '" + std::string(name) +
                                "' a form that another entry gives it");
            }
        }
    }
    std::size_t kept = 0;
    for (std::size_t index = 0; index < table.builtins.size(); ++index) {
        if (redundant[index]) continue;
        if (kept != index) {
            table.builtins[kept] = std::move(table.builtins[index]);
            table.builtin_costs[kept] = table.builtin_costs[index];
            table.builtin_entries[kept] = table.builtin_entries[index];
        }
        ++kept;
    }
    table.builtins.resize(kept);
    table.builtin_costs.resize(kept);
    table.builtin_entries.resize(kept);
    // The names that no form gives, with the reason of their first entry.
    std::unordered_set<std::string_view> reported;
    for (const auto& entry : entries) {
        if (entry.unexposed.empty() || by_name.contains(entry.builtin) ||
            !reported.insert(entry.builtin).second) {
            continue;
        }
        table.unexposed.push_back({entry.builtin, entry.unexposed});
    }
    for (const auto& intrinsic : target.intrinsic_forms) {
        if (std::none_of(table.builtins.begin(), table.builtins.end(),
                         [&](const InstructionEntry& form) {
                             return form.name == intrinsic.instruction;
                         })) {
            return fail(entries.front().origin,
                        "'" + std::string(intrinsic.intrinsic) + "' needs the built-in '" +
                            std::string(intrinsic.instruction) +
                            "', which no instruction entry gives");
        }
    }
    // The macros' expansions.
    for (const auto& entry : entries) {
        if (entry.macro.empty()) continue;
        const auto* macro = &*std::find_if(model.macros.begin(), model.macros.end(),
                                           [&](const InstructionMacro& item) {
                                               return item.name == entry.macro;
                                           });
        for (const auto use : macro->uses) {
            const auto found = names.find(use);
            if (found == names.end() || !entries[found->second].encoded) {
                return fail(entry.position("macro"),
                            "macro '" + entry.macro + "' needs the encoded instruction '" +
                                std::string(use) + "'");
            }
        }
    }
    return true;
}

} // namespace

std::string ModelInstruction::position(std::string_view property) const {
    for (const auto& [key, at] : property_lines) {
        if (key == property) return origin + ':' + std::to_string(at);
    }
    return origin + ':' + std::to_string(line);
}

std::optional<ModelInstruction> make_instruction(std::string name, unsigned line,
                                                 const std::string& origin,
                                                 const ModelProperties& properties,
                                                 ModelProblem& problem) {
    EntryReader reader(properties, problem);
    if (reader.failed()) return std::nullopt;
    ModelInstruction entry;
    entry.name = std::move(name);
    entry.origin = origin;
    entry.line = line;
    entry.shipped = origin.starts_with("shipped:");
    for (const auto& [property, value] : properties) {
        entry.property_lines.emplace_back(property, value.line);
    }
    const auto architecture = reader.text("architecture");
    if (!architecture) {
        if (!reader.failed()) reader.fail(line, "missing required model property 'architecture'");
        return std::nullopt;
    }
    const auto* target = architecture_target(*architecture);
    if (!target) {
        reader.fail(reader.line("architecture"), "unknown architecture '" + *architecture + "'");
        return std::nullopt;
    }
    if (!target->instruction_model) {
        reader.fail(reader.line("architecture"),
                    "architecture '" + *architecture + "' takes no instruction entries; its "
                    "instruction table is built into the compiler");
        return std::nullopt;
    }
    entry.architecture = *architecture;
    entry.mnemonic = reader.text("mnemonic").value_or(entry.name);
    if (entry.mnemonic.empty() ||
        !std::all_of(entry.mnemonic.begin(), entry.mnemonic.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
        })) {
        reader.fail(reader.find("mnemonic") ? reader.line("mnemonic") : line,
                    "instruction mnemonic '" + entry.mnemonic +
                        "' must consist of lower-case letters, digits, '.', and '_'");
        return std::nullopt;
    }
    if (const auto alias = reader.text("alias")) {
        for (const auto& [property, value] : properties) {
            if (property != "architecture" && property != "mnemonic" && property != "alias") {
                reader.fail(value.line, "an alias takes only 'architecture' and 'mnemonic', not '" +
                                            property + "'");
                return std::nullopt;
            }
        }
        entry.alias = *alias;
        return entry;
    }
    InstructionMaker maker(reader, *target, entry);
    if (!maker.operands()) return std::nullopt;
    const auto& model = *target->instruction_model;
    const bool expansion = reader.find("expands") || reader.find("otherwise");
    if (const auto macro = reader.text("macro")) {
        const auto found = std::find_if(model.macros.begin(), model.macros.end(),
                                        [&](const InstructionMacro& item) { return item.name == *macro; });
        if (found == model.macros.end()) {
            reader.fail(reader.line("macro"), "unknown " + *architecture + " macro '" + *macro + "'");
            return std::nullopt;
        }
        if (entry.operands.size() != found->operands.size() ||
            !std::equal(found->operands.begin(), found->operands.end(), entry.operands.begin(),
                        [](OperandKind kind, const ModelOperand& operand) {
                            return operand.type.kind == kind && !operand.name.empty();
                        })) {
            reader.fail(reader.line("operands"), "the operands do not suit macro '" + *macro + "'");
            return std::nullopt;
        }
        entry.macro = *macro;
    } else if (reader.failed()) {
        return std::nullopt;
    }
    if (expansion) {
        if (!entry.macro.empty()) {
            reader.fail(reader.line(reader.find("expands") ? "expands" : "otherwise"),
                        "an instruction is a macro or an expansion, not both");
            return std::nullopt;
        }
        if (!maker.expansions("expands", entry.expands) ||
            !maker.expansions("otherwise", entry.otherwise)) {
            return std::nullopt;
        }
        const auto uses_scratch = std::any_of(
            entry.otherwise.begin(), entry.otherwise.end(), [&](const ModelExpansion& item) {
                return std::any_of(item.operands.begin(), item.operands.end(),
                                   [&](const ModelExpansion::Operand& operand) {
                                       return operand.kind == ModelExpansion::Operand::Kind::Register &&
                                              operand.register_name == model.macro_register;
                                   });
            });
        const auto expressions = std::count_if(
            entry.operands.begin(), entry.operands.end(),
            [](const ModelOperand& operand) { return operand.type.kind == OperandKind::Expression; });
        if (!entry.otherwise.empty() && (!uses_scratch || expressions != 1)) {
            reader.fail(reader.line("otherwise"),
                        "'otherwise' loads the one 'imm' operand into $" +
                            std::string(model.macro_register) + ", which it must use");
            return std::nullopt;
        }
    }
    const bool encoded = entry.macro.empty() && !expansion;
    if (!encoded) {
        for (const auto property :
             {"encoding", "reads", "writes", "keeps", "hazards", "control", "condition",
              "delay_slot", "likely", "privileged", "volatile", "hazard_uses", "hazard_writes",
              "builtin", "constraints"}) {
            if (reader.find(property)) {
                reader.fail(reader.line(property),
                            std::string("a macro has no '") + property +
                                "'; the instructions it expands to have their own");
                return std::nullopt;
            }
        }
    } else {
        if (!reader.find("encoding")) {
            reader.fail(line, "missing required model property 'encoding'");
            return std::nullopt;
        }
        if (!maker.encoding()) return std::nullopt;
        for (const auto& operand : entry.operands) {
            if (operand.type.kind == OperandKind::Expression) {
                reader.fail(reader.line("operands"),
                            "operand '" + operand.name + "' of type '" + operand.type_name +
                                "' is only for macros");
                return std::nullopt;
            }
        }
    }
    if (!maker.features("features", entry.features) || !maker.features("excludes", entry.excludes) ||
        !maker.effects("reads", entry.reads) || !maker.effects("writes", entry.writes) ||
        !maker.keeps()) {
        return std::nullopt;
    }
    for (const auto& hazard : reader.list("hazards")) {
        if (std::find(model.hazard_classes.begin(), model.hazard_classes.end(), hazard) ==
            model.hazard_classes.end()) {
            reader.fail(reader.line("hazards"),
                        "unknown " + *architecture + " hazard class '" + hazard + "'");
            return std::nullopt;
        }
        entry.hazards.push_back(hazard);
    }
    if (const auto control = reader.text("control")) {
        if (*control == "jump") entry.control = InstructionControlEffect::UnconditionalBranch;
        else if (*control == "branch") entry.control = InstructionControlEffect::ConditionalBranch;
        else if (*control == "exit") entry.control = InstructionControlEffect::RawReturn;
        else if (*control == "trap") entry.control = InstructionControlEffect::Trap;
        else {
            reader.fail(reader.line("control"),
                        "control must be 'jump', 'branch', 'exit', or 'trap'");
            return std::nullopt;
        }
    }
    if (const auto condition = reader.text("condition")) {
        const auto found = std::find_if(std::begin(branch_conditions), std::end(branch_conditions),
                                        [&](const auto& item) { return item.first == *condition; });
        if (found == std::end(branch_conditions) ||
            entry.control != InstructionControlEffect::ConditionalBranch) {
            reader.fail(reader.line("condition"),
                        "condition must be 'equal', 'not-equal', 'less-than-zero', "
                        "'greater-equal-zero', 'less-equal-zero', 'greater-than-zero', "
                        "'less-than', 'greater-equal', 'less-than-unsigned', or "
                        "'greater-equal-unsigned', on a 'branch'");
            return std::nullopt;
        }
        // A comparison with zero takes one register, an ordering of two
        // registers two, and an equality either.
        const auto registers = std::count_if(
            entry.operands.begin(), entry.operands.end(), [](const ModelOperand& operand) {
                return !operand.name.empty() && operand.type.kind == OperandKind::Register;
            });
        using Condition = InstructionBranchCondition;
        const auto compared = found->second == Condition::Equal ||
                                      found->second == Condition::NotEqual
                                  ? registers == 1 || registers == 2
                              : found->second == Condition::LessThan ||
                                      found->second == Condition::GreaterEqual ||
                                      found->second == Condition::LessThanUnsigned ||
                                      found->second == Condition::GreaterEqualUnsigned
                                  ? registers == 2
                                  : registers == 1;
        if (!compared) {
            reader.fail(reader.line("condition"),
                        "condition '" + *condition + "' does not compare the form's " +
                            std::to_string(registers) + " register operands");
            return std::nullopt;
        }
        entry.branch_condition = found->second;
    }
    entry.delay_slot = reader.boolean("delay_slot");
    entry.likely = reader.boolean("likely");
    entry.privileged = reader.boolean("privileged");
    entry.volatile_effect = reader.boolean("volatile");
    if (entry.delay_slot && entry.control == InstructionControlEffect::None) {
        reader.fail(reader.line("delay_slot"), "a delay slot follows only a control transfer");
    }
    if (entry.likely && entry.control != InstructionControlEffect::ConditionalBranch) {
        reader.fail(reader.line("likely"), "only a 'branch' can be likely");
    }
    if (reader.failed() || !maker.hazard_facts() || !maker.constraints()) return std::nullopt;
    entry.latency = reader.integer("latency", 0, 65535);
    entry.issue = reader.integer("issue", 1, 65535);
    if (const auto* property = reader.find("builtin");
        property && property->value.kind == ModelValue::Kind::Boolean) {
        if (property->value.boolean) {
            reader.fail(property->line, "model property 'builtin' must be text or false");
            return std::nullopt;
        }
        entry.no_builtin = true;
    } else if (const auto builtin = reader.text("builtin")) {
        if (!identifier(*builtin)) {
            reader.fail(reader.line("builtin"), "invalid built-in name '" + *builtin + "'");
            return std::nullopt;
        }
        if (const auto reason = builtin_unavailability(entry); !reason.empty()) {
            reader.fail(reader.line("builtin"),
                        "instruction '" + entry.name + "' cannot be a built-in: " + reason);
            return std::nullopt;
        }
        entry.builtin = "$::_" + *builtin;
    }
    if (reader.failed()) return std::nullopt;
    return entry;
}

const InstructionTable* ModelRegistry::instruction_table(std::string_view architecture) const {
    for (const auto& table : instruction_tables_) {
        if (table.architecture == architecture) return &table;
    }
    return nullptr;
}

std::span<const InstructionCostEntry> ModelRegistry::instruction_costs(
    const TargetInfo& target) const {
    if (const auto* table = instruction_table(target.architecture)) return table->costs;
    return target.instruction_costs;
}

bool ModelRegistry::complete_instructions(
    Diagnostics& diagnostics, std::optional<std::string_view> architecture) {
    if (instructions_pending_) {
        for (auto& table : instruction_tables_) {
            if (!complete_table(*architecture_target(table.architecture), table, diagnostics)) {
                return false;
            }
        }
        instructions_pending_ = false;
    }
    // A target whose table comes from models needs one.
    for (const auto* target : all_targets()) {
        if ((!architecture || target->architecture == *architecture) &&
            target->instruction_model && !instruction_table(target->architecture)) {
            diagnostics.command_error("no model gives architecture '" +
                                      std::string(target->architecture) + "' instructions");
            return false;
        }
    }
    // The instructions that cpu entries describe, by name.
    std::unordered_map<std::string_view, std::unordered_map<std::string_view, InstructionCostId>>
        indices;
    for (auto& cpu : cpus_) {
        auto& index = indices[cpu.architecture];
        if (index.empty()) {
            const auto costs = instruction_costs(*architecture_target(cpu.architecture));
            for (std::size_t position = 0; position < costs.size(); ++position) {
                index.emplace(costs[position].name,
                              InstructionCostId{static_cast<std::uint16_t>(position)});
            }
        }
        const auto origin = cpu.source.substr(0, cpu.source.rfind(':'));
        for (auto& block : cpu.instructions) {
            const auto found = index.find(block.name);
            if (found == index.end()) {
                diagnostics.file_error(origin + ':' + std::to_string(block.line),
                                       "unknown " + cpu.architecture + " instruction '" +
                                           block.name + "'");
                return false;
            }
            block.instruction = found->second;
        }
    }
    return true;
}

void ModelRegistry::install_instruction_tables() const {
    for (const auto& table : instruction_tables_) {
        for (const auto* target : all_targets()) {
            if (target->architecture != table.architecture) continue;
            // The targets that take models' tables are not const objects.
            auto& installed = const_cast<TargetInfo&>(*target);
            installed.instructions = table.builtins;
            installed.instruction_costs = table.costs;
            installed.form_instructions = table.builtin_costs;
            installed.cost_listing = table.cost_listing;
            installed.unexposed_instructions = table.unexposed;
        }
    }
}

} // namespace cross
