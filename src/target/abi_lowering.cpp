// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/abi_lowering.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cross {
namespace {

struct PieceRequest {
    const AbiRegisterBank* bank{};
    std::uint32_t value_bit_offset{};
    std::uint16_t value_bits{};
    std::uint16_t carrier_bits{};
    std::uint16_t indirect_value_bits{};
    AbiExtensionKind extension{AbiExtensionKind::None};
};

struct LoweredValue {
    std::vector<PieceRequest> pieces;
    const AbiRule* rule{};
    bool force_stack{};
    bool ignored{};
    bool indirect{};
};

struct AllocationState {
    std::unordered_map<std::string, std::size_t> cursors;
    std::unordered_map<std::string, std::size_t> bank_uses;
    std::size_t stack_cursor{};
    std::size_t high_water{};
};

enum class RuleUse { FixedArgument, VariadicArgument, Result };

constexpr AbiValueKind model_kind(ScalarKind kind) {
    switch (kind) {
    case ScalarKind::Integer: return AbiValueKind::Integer;
    case ScalarKind::Floating: return AbiValueKind::Floating;
    case ScalarKind::Pointer: return AbiValueKind::Pointer;
    case ScalarKind::Pair: return AbiValueKind::Pair;
    case ScalarKind::Aggregate: return AbiValueKind::Aggregate;
    case ScalarKind::Array: return AbiValueKind::Array;
    case ScalarKind::Vector: return AbiValueKind::Vector;
    case ScalarKind::Zero: return AbiValueKind::Zero;
    }
    return AbiValueKind::Any;
}

const AbiRegisterBank* find_bank(const AbiEntry& abi,
                                 std::string_view name) {
    const auto found = std::find_if(
        abi.banks.begin(), abi.banks.end(),
        [&](const AbiRegisterBank& bank) {
            return bank.canonical_name == name;
        });
    return found == abi.banks.end() ? nullptr : &*found;
}

bool has_feature(AbiFeatureSet features, std::string_view sought) {
    return std::find(features.begin(), features.end(), sought) !=
           features.end();
}

bool feature_match(const AbiRule& rule, AbiFeatureSet features) {
    return std::all_of(
               rule.required_features.begin(),
               rule.required_features.end(),
               [&](const std::string& feature) {
                   return has_feature(features, feature);
               }) &&
           std::none_of(
               rule.forbidden_features.begin(),
               rule.forbidden_features.end(),
               [&](const std::string& feature) {
                   return has_feature(features, feature);
               });
}

bool naturally_aligned(const AbiValue& value, unsigned depth = 0) {
    if (depth >= 64) return false;
    std::uint32_t running{};
    for (std::size_t index = 0; index < value.elements.size(); ++index) {
        const auto& element = value.elements[index];
        const auto offset = index < value.element_offsets_bits.size()
                                ? value.element_offsets_bits[index]
                                : running;
        if (element.alignment_bits != 0 &&
            offset % element.alignment_bits != 0) {
            return false;
        }
        if (!naturally_aligned(element, depth + 1)) return false;
        running = offset + element.mode.bits;
    }
    if (value.mode.kind == ScalarKind::Array &&
        value.element_count > value.elements.size() &&
        value.elements.size() == 1) {
        const auto& element = value.elements.front();
        for (std::uint32_t index = 1; index < value.element_count; ++index) {
            const auto offset = index * element.mode.bits;
            if (element.alignment_bits != 0 &&
                offset % element.alignment_bits != 0) {
                return false;
            }
        }
    }
    return true;
}

bool rule_matches(const AbiEntry& abi, const AbiRule& rule,
                  const AbiValue& value, RuleUse use,
                  AbiFeatureSet features,
                  const AllocationState* allocation = nullptr,
                  std::size_t argument_index = 0) {
    const auto kind = model_kind(
        value.transport == ValueTransport::ByReference
            ? ScalarKind::Pointer
            : value.mode.kind);
    const auto bits =
        value.transport == ValueTransport::ByReference
            ? static_cast<std::uint16_t>(abi.address_bits)
            : value.mode.bits;
    if (!feature_match(rule, features)) return false;
    if ((use == RuleUse::FixedArgument && !rule.fixed_arguments) ||
        (use == RuleUse::VariadicArgument &&
         !rule.variadic_arguments) ||
        (use == RuleUse::Result && !rule.results)) {
        return false;
    }
    if (use != RuleUse::Result) {
        if (rule.argument_limit != 0 &&
            argument_index >= rule.argument_limit) {
            return false;
        }
        if (allocation && std::any_of(
                rule.requires_unused_banks.begin(),
                rule.requires_unused_banks.end(),
                [&](const std::string& bank) {
                    const auto found = allocation->bank_uses.find(bank);
                    return found != allocation->bank_uses.end() &&
                           found->second != 0;
                })) {
            return false;
        }
    }
    if (std::find(rule.matches.begin(), rule.matches.end(), kind) ==
            rule.matches.end() &&
        std::find(rule.matches.begin(), rule.matches.end(),
                  AbiValueKind::Any) == rule.matches.end()) {
        return false;
    }
    if (bits < rule.min_bits ||
        (rule.max_bits != 0 && bits > rule.max_bits)) {
        return false;
    }
    if (kind == AbiValueKind::Array &&
        value.element_count > rule.max_elements) {
        return false;
    }
    return !rule.require_natural_alignment || naturally_aligned(value);
}

bool checked_align(std::size_t value, std::size_t alignment,
                   std::size_t& result) {
    if (alignment == 0) return false;
    const auto remainder = value % alignment;
    const auto padding = remainder == 0 ? 0 : alignment - remainder;
    if (value > std::numeric_limits<std::size_t>::max() - padding) {
        return false;
    }
    result = value + padding;
    return true;
}

bool checked_add(std::size_t left, std::size_t right,
                 std::size_t& result) {
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

std::size_t value_bytes(const AbiValue& value, const AbiEntry& abi) {
    if (value.transport == ValueTransport::ByReference) {
        return (abi.address_bits + 7U) / 8U;
    }
    if (value.mode.kind == ScalarKind::Zero) return 0;
    return std::max<std::size_t>(1, (value.mode.bits + 7U) / 8U);
}

std::size_t value_alignment(const AbiValue& value,
                            const AbiEntry& abi) {
    // By-reference Cross parameters transport an address, not the referent's
    // object representation.  A highly aligned record must therefore not
    // over-align a spilled pointer channel.
    if (value.transport == ValueTransport::ByReference) {
        const auto bytes = (abi.address_bits + 7U) / 8U;
        return std::max<std::size_t>(
            1, std::min<std::size_t>(bytes, abi.stack_alignment));
    }
    if (value.alignment_bits != 0) {
        return std::max<std::size_t>(
            1, (value.alignment_bits + 7U) / 8U);
    }
    const auto bytes = value_bytes(value, abi);
    const auto natural =
        bytes >= 16 ? 16U : bytes >= 8 ? 8U : bytes >= 4 ? 4U
                                                : bytes >= 2 ? 2U : 1U;
    return std::max<std::size_t>(
        std::min<std::size_t>(natural, abi.stack_alignment),
        std::min<std::size_t>(bytes, abi.stack_slot_bytes));
}

std::uint32_t element_offset(const AbiValue& value,
                             std::size_t index,
                             std::uint32_t running) {
    return index < value.element_offsets_bits.size()
               ? value.element_offsets_bits[index]
               : running;
}

std::optional<LoweredValue> lower_value(const AbiEntry& abi,
                                        const AbiValue& value,
                                        RuleUse use,
                                        AbiFeatureSet features,
                                        const AllocationState* allocation = nullptr,
                                        std::size_t argument_index = 0,
                                        unsigned depth = 0) {
    if (depth >= 64) return std::nullopt;
    for (const auto& candidate : abi.rules) {
        if (!rule_matches(abi, candidate, value, use, features,
                          allocation, argument_index)) {
            continue;
        }
        const auto* rule = &candidate;
        auto attempt = [&]() -> std::optional<LoweredValue> {
            LoweredValue result;
            result.rule = rule;
            const auto effective_bits =
                value.transport == ValueTransport::ByReference
                    ? static_cast<std::uint16_t>(abi.address_bits)
                    : value.mode.bits;
            const auto* bank = find_bank(abi, rule->bank);
            const auto unit_bits =
                rule->unit_bits != 0
                    ? rule->unit_bits
                    : bank ? bank->register_bits : 0U;
            const auto carrier = [&](std::uint16_t bits) {
                return static_cast<std::uint16_t>(
                    std::max<unsigned>(bits, rule->carrier_bits));
            };

            switch (rule->action) {
    case AbiRuleAction::Direct:
        if (!bank || effective_bits == 0 ||
            effective_bits > bank->register_bits) {
            return std::nullopt;
        }
        result.pieces.push_back(
            {bank, 0, effective_bits, carrier(effective_bits), 0,
             rule->extension});
        break;
    case AbiRuleAction::Split:
    case AbiRuleAction::Coerce: {
        if (!bank || unit_bits == 0 || effective_bits == 0) {
            return std::nullopt;
        }
        for (std::uint32_t offset = 0; offset < effective_bits;
             offset += unit_bits) {
            const auto bits = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(
                    unit_bits, effective_bits - offset));
            result.pieces.push_back(
                {bank, offset, bits, carrier(bits), 0,
                 rule->extension});
        }
        break;
    }
    case AbiRuleAction::Flatten: {
        std::uint32_t running{};
        for (std::size_t index = 0; index < value.elements.size();
             ++index) {
            const auto offset =
                element_offset(value, index, running);
            auto child =
                lower_value(abi, value.elements[index], use, features,
                            allocation, argument_index, depth + 1);
            if (!child || child->force_stack || child->indirect) {
                return std::nullopt;
            }
            for (auto piece : child->pieces) {
                piece.value_bit_offset += offset;
                result.pieces.push_back(std::move(piece));
            }
            running = offset + value.elements[index].mode.bits;
        }
        if (value.mode.kind == ScalarKind::Array &&
            value.element_count > value.elements.size() &&
            value.elements.size() == 1) {
            const auto element = value.elements.front();
            for (std::uint32_t index = 1;
                 index < value.element_count; ++index) {
                auto child =
                    lower_value(abi, element, use, features,
                                allocation, argument_index, depth + 1);
                if (!child || child->force_stack || child->indirect) {
                    return std::nullopt;
                }
                const auto offset = index * element.mode.bits;
                for (auto piece : child->pieces) {
                    piece.value_bit_offset += offset;
                    result.pieces.push_back(std::move(piece));
                }
            }
        }
        if (!rule->merge_banks.empty() && !result.pieces.empty()) {
            // A single wide child (notably a fixed vector) already names one
            // legal carrier and may encode an ABI continuation class inside
            // that carrier. Preserve it rather than splitting it into chunks.
            const bool preserve_wide =
                result.pieces.size() == 1 && result.pieces.front().bank &&
                std::find(rule->merge_banks.begin(),
                          rule->merge_banks.end(),
                          result.pieces.front().bank->canonical_name) !=
                    rule->merge_banks.end() &&
                result.pieces.front().value_bits > rule->unit_bits &&
                result.pieces.front().value_bit_offset % rule->unit_bits == 0 &&
                result.pieces.front().value_bits <=
                    result.pieces.front().bank->register_bits;
            if (!preserve_wide) {
                std::vector<PieceRequest> merged;
                const auto precedence = [&](const AbiRegisterBank* piece_bank) {
                    const auto found = std::find(
                        rule->merge_banks.begin(), rule->merge_banks.end(),
                        piece_bank ? piece_bank->canonical_name : std::string{});
                    return found == rule->merge_banks.end()
                               ? rule->merge_banks.size()
                               : static_cast<std::size_t>(
                                     found - rule->merge_banks.begin());
                };
                for (std::uint32_t begin = 0; begin < effective_bits;
                     begin += rule->unit_bits) {
                    const auto end = std::min<std::uint32_t>(
                        effective_bits, begin + rule->unit_bits);
                    const AbiRegisterBank* selected{};
                    auto selected_precedence = rule->merge_banks.size();
                    bool overlaps{};
                    for (const auto& piece : result.pieces) {
                        const auto piece_end =
                            piece.value_bit_offset + piece.value_bits;
                        if (piece.value_bit_offset >= end ||
                            piece_end <= begin) {
                            continue;
                        }
                        overlaps = true;
                        const auto priority = precedence(piece.bank);
                        if (priority == rule->merge_banks.size()) {
                            return std::nullopt;
                        }
                        if (!selected || priority < selected_precedence) {
                            selected = piece.bank;
                            selected_precedence = priority;
                        }
                    }
                    if (!overlaps) continue;
                    // Bank merging creates a new raw aggregate carrier.  A
                    // scalar child's extension does not describe aggregate
                    // padding (for example, a struct containing an i8 is not
                    // passed as a sign-extended scalar i8), so the rebuilt
                    // transport deliberately starts with no extension.
                    merged.push_back(
                        {selected, begin,
                         static_cast<std::uint16_t>(end - begin),
                         static_cast<std::uint16_t>(rule->unit_bits)});
                }
                result.pieces = std::move(merged);
            }
        }
        if (result.pieces.empty()) result.ignored = true;
        break;
    }
    case AbiRuleAction::Indirect: {
        if (!bank || bank->register_bits < abi.address_bits) {
            return std::nullopt;
        }
        result.indirect = true;
        const auto indirect_unit =
            rule->unit_bits != 0 ? rule->unit_bits : effective_bits;
        if (indirect_unit == 0 || indirect_unit > effective_bits) {
            return std::nullopt;
        }
        for (std::uint32_t offset = 0; offset < effective_bits;
             offset += indirect_unit) {
            const auto chunk = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(
                    indirect_unit, effective_bits - offset));
            result.pieces.push_back(
                {bank, offset,
                 static_cast<std::uint16_t>(abi.address_bits),
                 static_cast<std::uint16_t>(bank->register_bits), chunk});
        }
        break;
    }
    case AbiRuleAction::Stack:
        result.force_stack = true;
        break;
    case AbiRuleAction::Ignore:
        result.ignored = true;
        break;
            }
            return result;
        }();
        if (attempt) return attempt;
        // Recursive flattening is a tentative classification. If one of its
        // leaves cannot be represented by the rule's banks, later matching
        // rules remain authoritative (typically a model-declared memory
        // fallback). No ABI name or aggregate class is hard-coded here.
    }
    return std::nullopt;
}

ValuePiece register_piece(std::string reg,
                          const PieceRequest& request) {
    return {{LocationKind::Register, std::move(reg), 0},
            static_cast<std::uint16_t>(request.value_bit_offset),
            request.value_bits, request.carrier_bits,
            request.indirect_value_bits, request.extension};
}

ValuePiece stack_piece(std::size_t offset,
                       const PieceRequest& request) {
    return {{LocationKind::Stack, {}, offset},
            static_cast<std::uint16_t>(request.value_bit_offset),
            request.value_bits, request.carrier_bits,
            request.indirect_value_bits, AbiExtensionKind::None};
}

AbiRegisterFailure failure_policy(const AbiEntry& abi,
                                  const LoweredValue& value,
                                  bool arguments) {
    if (value.rule && value.rule->failure_override) {
        return value.rule->failure;
    }
    return arguments ? abi.argument_register_failure
                     : abi.result_register_failure;
}

bool allocate_stack_value(const AbiEntry& abi, const AbiValue& value,
                          const LoweredValue& lowered,
                          AllocationState& state,
                          ArgumentAssignment& assignment) {
    const auto alignment = std::max<std::size_t>(
        lowered.rule && lowered.rule->stack_alignment != 0
            ? lowered.rule->stack_alignment
            : value_alignment(value, abi),
        abi.stack_slot_bytes);
    std::size_t base{};
    if (!checked_align(state.stack_cursor, alignment, base)) return false;
    if (value_bytes(value, abi) == 0) {
        assignment.stack_alignment = alignment;
        PieceRequest request;
        assignment.pieces.push_back(stack_piece(base, request));
        return true;
    }
    const auto size = std::max<std::size_t>(
        {abi.stack_slot_bytes, value_bytes(value, abi),
         lowered.rule ? lowered.rule->stack_size : 0U});
    std::size_t end{};
    if (!checked_add(base, size, end)) return false;
    state.stack_cursor = end;
    state.high_water = std::max(state.high_water, end);
    assignment.stack_size = size;
    assignment.stack_alignment = alignment;

    if (lowered.pieces.empty()) {
        PieceRequest request;
        request.value_bits = value.mode.bits;
        request.carrier_bits = value.mode.bits;
        assignment.pieces.push_back(stack_piece(base, request));
        return true;
    }
    for (const auto& request : lowered.pieces) {
        assignment.pieces.push_back(stack_piece(
            base + request.value_bit_offset / 8U, request));
    }
    return true;
}

bool allocate_partial_stack_piece(
    const AbiEntry& abi, const PieceRequest& request,
    std::size_t cursor, AllocationState& state,
    ArgumentAssignment& assignment) {
    // A piece wider than a slot keeps its whole carrier; in a packed area
    // it is also aligned like that carrier.
    const auto bytes = std::max<std::size_t>(
        abi.stack_slot_bytes, (request.carrier_bits + 7U) / 8U);
    std::size_t alignment = abi.stack_slot_bytes;
    std::size_t offset{};
    if (abi.stack_layout == AbiStackLayout::Slots) {
        if (cursor >
            std::numeric_limits<std::size_t>::max() /
                abi.stack_slot_bytes) {
            return false;
        }
        offset = cursor * abi.stack_slot_bytes;
        offset = std::max<std::size_t>(
            offset, abi.argument_stack_base);
        std::size_t end{};
        if (!checked_add(offset, bytes, end)) {
            return false;
        }
        state.stack_cursor = std::max(state.stack_cursor, end);
        state.high_water = std::max(state.high_water, end);
    } else {
        alignment = std::max<std::size_t>(
            alignment, std::min<std::size_t>(bytes, abi.stack_alignment));
        if (!checked_align(state.stack_cursor, alignment, offset) ||
            !checked_add(offset, bytes, state.stack_cursor)) {
            return false;
        }
        state.high_water =
            std::max(state.high_water, state.stack_cursor);
    }
    assignment.pieces.push_back(stack_piece(offset, request));
    assignment.stack_size += bytes;
    assignment.stack_alignment =
        std::max(assignment.stack_alignment, alignment);
    return true;
}

bool allocate_result_stack_value(
    const AbiEntry& abi, const AbiValue& value,
    const LoweredValue& lowered, AllocationState& state,
    ReturnAssignment& assignment) {
    const auto alignment = std::max<std::size_t>(
        lowered.rule && lowered.rule->stack_alignment != 0
            ? lowered.rule->stack_alignment
            : value_alignment(value, abi),
        abi.stack_slot_bytes);
    std::size_t base{};
    if (!checked_align(state.stack_cursor, alignment, base)) return false;
    if (value_bytes(value, abi) == 0) {
        assignment.stack_alignment = alignment;
        PieceRequest request;
        assignment.pieces.push_back(stack_piece(base, request));
        return true;
    }
    const auto size = std::max<std::size_t>(
        {abi.stack_slot_bytes, value_bytes(value, abi),
         lowered.rule ? lowered.rule->stack_size : 0U});
    std::size_t end{};
    if (!checked_add(base, size, end)) return false;
    state.stack_cursor = end;
    state.high_water = std::max(state.high_water, end);
    assignment.stack_size = size;
    assignment.stack_alignment = alignment;

    if (lowered.pieces.empty()) {
        PieceRequest request;
        request.value_bits = value.mode.bits;
        request.carrier_bits = value.mode.bits;
        assignment.pieces.push_back(stack_piece(base, request));
        return true;
    }
    for (const auto& request : lowered.pieces) {
        assignment.pieces.push_back(stack_piece(
            base + request.value_bit_offset / 8U, request));
    }
    return true;
}

struct PendingPiece {
    PieceRequest request;
    std::size_t cursor{};
};

struct ArgumentWork {
    LoweredValue lowered;
    bool whole_stack{};
    bool spill{};
    std::vector<PendingPiece> partial_stack;
};

struct ResultWork {
    LoweredValue lowered;
    bool whole_stack{};
    std::vector<PendingPiece> partial_stack;
};

using CursorMap = std::unordered_map<std::string, std::size_t>;

bool prepare_rule_cursors(const LoweredValue& lowered, CursorMap& cursors,
                          CursorMap& starts) {
    const auto alignment = lowered.rule
                               ? lowered.rule->cursor_alignment
                               : 1U;
    for (const auto& piece : lowered.pieces) {
        if (!piece.bank || starts.contains(piece.bank->cursor)) continue;
        std::size_t aligned{};
        if (!checked_align(cursors[piece.bank->cursor], alignment,
                           aligned)) {
            return false;
        }
        cursors[piece.bank->cursor] = aligned;
        starts.emplace(piece.bank->cursor, aligned);
    }
    return true;
}

bool finish_rule_cursors(const LoweredValue& lowered, CursorMap& cursors,
                         const CursorMap& starts) {
    if (!lowered.rule || lowered.rule->cursor_advance == 0) return true;
    for (const auto& [name, start] : starts) {
        const auto found = cursors.find(name);
        if (found == cursors.end() || found->second < start) return false;
        const auto used = found->second - start;
        if (used > lowered.rule->cursor_advance ||
            start > std::numeric_limits<std::size_t>::max() -
                        lowered.rule->cursor_advance) {
            return false;
        }
        found->second = start + lowered.rule->cursor_advance;
    }
    return true;
}

void record_rule_banks(const LoweredValue& lowered,
                       AllocationState& state) {
    std::unordered_set<std::string_view> seen;
    for (const auto& piece : lowered.pieces) {
        if (!piece.bank ||
            !seen.insert(piece.bank->canonical_name).second) {
            continue;
        }
        ++state.bank_uses[piece.bank->canonical_name];
    }
}

SignatureClassificationResult signature_fail(
    SignatureLayout layout, ClassificationError error,
    std::size_t value, bool result) {
    return {std::move(layout), error, value, result};
}

bool has_stack_region(const AbiEntry& abi, AbiStackRegion region) {
    return std::find(abi.stack_order.begin(), abi.stack_order.end(),
                     region) != abi.stack_order.end();
}

SignatureClassificationResult classify_signature_values(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::span<const AbiValue> results,
    std::optional<std::size_t> fixed_argument_count,
    AbiFeatureSet features) {
    SignatureLayout layout;
    layout.call.argument_stack_base_size = abi.argument_stack_base;
    layout.call.outgoing_area_alignment = abi.stack_alignment;
    layout.call.arguments.reserve(arguments.size());
    layout.results.reserve(results.size());

    if (fixed_argument_count && *fixed_argument_count > arguments.size()) {
        return signature_fail(
            std::move(layout), ClassificationError::InvalidScalarMode,
            *fixed_argument_count, false);
    }

    std::vector<ArgumentWork> argument_work(arguments.size());
    std::vector<ResultWork> result_work(results.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        auto lowered = lower_value(
            abi, results[index], RuleUse::Result, features);
        if (!lowered) {
            return signature_fail(
                std::move(layout),
                ClassificationError::InvalidScalarMode, index, true);
        }
        ReturnAssignment assignment;
        assignment.result_index = index;
        assignment.value = results[index];
        assignment.mode = results[index].mode;
        assignment.indirect = lowered->indirect;
        layout.results.push_back(std::move(assignment));
        result_work[index].lowered = std::move(*lowered);
    }

    // Hidden result-area pointers are allocated before source arguments.
    // Sharing a model cursor with an ordinary argument bank makes the hidden
    // channel consume that bank; a dedicated cursor (for example AArch64 x8)
    // leaves ordinary arguments untouched.
    AllocationState argument_registers;
    std::unordered_map<std::string, std::size_t> named_register_cursors;
    if (fixed_argument_count && *fixed_argument_count == 0) {
        named_register_cursors = argument_registers.cursors;
    }
    for (std::size_t index = 0; index < results.size(); ++index) {
        auto& work = result_work[index];
        auto& assignment = layout.results[index];
        if (!work.lowered.indirect) continue;
        CursorMap starts;
        if (!prepare_rule_cursors(
                work.lowered, argument_registers.cursors, starts)) {
            return signature_fail(
                std::move(layout), ClassificationError::SizeOverflow,
                index, true);
        }
        for (const auto& request : work.lowered.pieces) {
            if (!request.bank) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::InvalidScalarMode, index, true);
            }
            auto& cursor =
                argument_registers.cursors[request.bank->cursor];
            if (cursor >= request.bank->arguments.size()) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::InvalidScalarMode, index, true);
            }
            assignment.pieces.push_back(register_piece(
                request.bank->arguments[cursor++], request));
            if (assignment.indirect_result_reg.empty() &&
                !request.bank->results.empty()) {
                assignment.indirect_result_reg =
                    request.bank->results.front();
            }
        }
        if (!finish_rule_cursors(
                work.lowered, argument_registers.cursors, starts)) {
            return signature_fail(
                std::move(layout), ClassificationError::InvalidScalarMode,
                index, true);
        }
        record_rule_banks(work.lowered, argument_registers);
    }

    const bool reserve_argument_spills =
        has_stack_region(abi, AbiStackRegion::ArgumentSpills);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& value = arguments[index];
        const auto use =
            fixed_argument_count && index >= *fixed_argument_count
                ? RuleUse::VariadicArgument
                : RuleUse::FixedArgument;
        auto lowered = lower_value(
            abi, value, use, features, &argument_registers, index);
        if (!lowered) {
            return signature_fail(
                std::move(layout),
                ClassificationError::InvalidScalarMode, index, false);
        }
        ArgumentAssignment assignment;
        assignment.argument_index = index;
        assignment.value = value;
        assignment.indirect = lowered->indirect;
        auto& work = argument_work[index];
        work.lowered = std::move(*lowered);

        if (work.lowered.ignored) {
            layout.call.arguments.push_back(std::move(assignment));
            continue;
        }
        if (work.lowered.force_stack) {
            work.whole_stack = true;
            layout.call.arguments.push_back(std::move(assignment));
            continue;
        }

        const auto saved_cursors = argument_registers.cursors;
        CursorMap starts;
        if (!prepare_rule_cursors(
                work.lowered, argument_registers.cursors, starts)) {
            return signature_fail(
                std::move(layout), ClassificationError::SizeOverflow,
                index, false);
        }
        bool exhausted = false;
        for (const auto& request : work.lowered.pieces) {
            if (!request.bank) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::InvalidScalarMode, index, false);
            }
            auto& cursor =
                argument_registers.cursors[request.bank->cursor];
            const auto position = cursor++;
            if (position < request.bank->arguments.size()) {
                assignment.pieces.push_back(register_piece(
                    request.bank->arguments[position], request));
            } else {
                exhausted = true;
                work.partial_stack.push_back({request, position});
            }
        }
        if (!finish_rule_cursors(
                work.lowered, argument_registers.cursors, starts)) {
            return signature_fail(
                std::move(layout), ClassificationError::InvalidScalarMode,
                index, false);
        }
        if (exhausted) {
            const auto failure =
                failure_policy(abi, work.lowered, true);
            if (failure == AbiRegisterFailure::Error) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::InvalidScalarMode, index, false);
            }
            if (failure == AbiRegisterFailure::Stack) {
                argument_registers.cursors = saved_cursors;
                assignment.pieces.clear();
                work.partial_stack.clear();
                work.whole_stack = true;
            }
        } else {
            work.spill = reserve_argument_spills;
        }
        record_rule_banks(work.lowered, argument_registers);
        layout.call.arguments.push_back(std::move(assignment));
        if (fixed_argument_count && index + 1 == *fixed_argument_count) {
            named_register_cursors = argument_registers.cursors;
        }
    }

    std::unordered_map<std::string, std::size_t> result_cursors;
    for (std::size_t index = 0; index < results.size(); ++index) {
        auto& work = result_work[index];
        auto& assignment = layout.results[index];
        if (work.lowered.ignored || work.lowered.indirect) continue;
        if (work.lowered.force_stack) {
            work.whole_stack = true;
            continue;
        }
        const auto saved_cursors = result_cursors;
        CursorMap starts;
        if (!prepare_rule_cursors(
                work.lowered, result_cursors, starts)) {
            return signature_fail(
                std::move(layout), ClassificationError::SizeOverflow,
                index, true);
        }
        bool exhausted = false;
        for (const auto& request : work.lowered.pieces) {
            if (!request.bank) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::InvalidScalarMode, index, true);
            }
            auto& cursor = result_cursors[request.bank->cursor];
            const auto position = cursor++;
            if (position < request.bank->results.size()) {
                assignment.pieces.push_back(register_piece(
                    request.bank->results[position], request));
            } else {
                exhausted = true;
                work.partial_stack.push_back({request, position});
            }
        }
        if (!finish_rule_cursors(
                work.lowered, result_cursors, starts)) {
            return signature_fail(
                std::move(layout), ClassificationError::InvalidScalarMode,
                index, true);
        }
        if (!exhausted) continue;
        const auto failure =
            failure_policy(abi, work.lowered, false);
        if (failure == AbiRegisterFailure::Error) {
            return signature_fail(
                std::move(layout),
                ClassificationError::InvalidScalarMode, index, true);
        }
        if (failure == AbiRegisterFailure::Stack) {
            result_cursors = saved_cursors;
            assignment.pieces.clear();
            work.partial_stack.clear();
            work.whole_stack = true;
        }
    }

    AllocationState stack;
    stack.stack_cursor = abi.argument_stack_base;
    stack.high_water = abi.argument_stack_base;
    bool allocated_arguments{};
    bool allocated_results{};
    bool allocated_spills{};
    for (const auto region : abi.stack_order) {
        switch (region) {
        case AbiStackRegion::Arguments:
            allocated_arguments = true;
            if (fixed_argument_count && *fixed_argument_count == 0) {
                layout.call.variadic_stack_offset = stack.stack_cursor;
            }
            for (std::size_t index = 0; index < arguments.size();
                 ++index) {
                auto& work = argument_work[index];
                auto& assignment = layout.call.arguments[index];
                if (work.whole_stack) {
                    if (!allocate_stack_value(
                            abi, arguments[index], work.lowered, stack,
                            assignment)) {
                        return signature_fail(
                            std::move(layout),
                            ClassificationError::SizeOverflow, index,
                            false);
                    }
                } else {
                    for (const auto& pending : work.partial_stack) {
                        if (!allocate_partial_stack_piece(
                                abi, pending.request, pending.cursor,
                                stack, assignment)) {
                            return signature_fail(
                                std::move(layout),
                                ClassificationError::SizeOverflow,
                                index, false);
                        }
                    }
                }
                if (fixed_argument_count &&
                    index + 1 == *fixed_argument_count) {
                    layout.call.variadic_stack_offset = stack.stack_cursor;
                }
            }
            break;
        case AbiStackRegion::Results:
            allocated_results = true;
            for (std::size_t index = 0; index < results.size();
                 ++index) {
                auto& work = result_work[index];
                auto& assignment = layout.results[index];
                if (work.whole_stack) {
                    if (!allocate_result_stack_value(
                            abi, results[index], work.lowered, stack,
                            assignment)) {
                        return signature_fail(
                            std::move(layout),
                            ClassificationError::SizeOverflow, index,
                            true);
                    }
                } else {
                    for (const auto& pending : work.partial_stack) {
                        ArgumentAssignment temporary;
                        if (!allocate_partial_stack_piece(
                                abi, pending.request, pending.cursor,
                                stack, temporary)) {
                            return signature_fail(
                                std::move(layout),
                                ClassificationError::SizeOverflow,
                                index, true);
                        }
                        assignment.stack_size += temporary.stack_size;
                        assignment.stack_alignment = std::max(
                            assignment.stack_alignment,
                            temporary.stack_alignment);
                        for (auto piece : temporary.pieces) {
                            assignment.pieces.push_back(
                                std::move(piece));
                        }
                    }
                }
            }
            break;
        case AbiStackRegion::ArgumentSpills: {
            allocated_spills = true;
            std::size_t spill_begin{};
            if (!checked_align(stack.stack_cursor,
                               abi.stack_slot_bytes, spill_begin)) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::SizeOverflow,
                    arguments.size(), false);
            }
            stack.stack_cursor = spill_begin;
            for (std::size_t index = 0; index < arguments.size();
                 ++index) {
                if (!argument_work[index].spill) continue;
                std::size_t aligned{};
                if (!checked_align(
                        stack.stack_cursor,
                        value_alignment(arguments[index], abi), aligned) ||
                    !checked_add(
                        aligned, value_bytes(arguments[index], abi),
                                 stack.stack_cursor)) {
                    return signature_fail(
                        std::move(layout),
                        ClassificationError::SizeOverflow, index, false);
                }
            }
            std::size_t spill_end{};
            if (!checked_align(stack.stack_cursor,
                               abi.stack_slot_bytes, spill_end)) {
                return signature_fail(
                    std::move(layout),
                    ClassificationError::SizeOverflow,
                    arguments.size(), false);
            }
            stack.stack_cursor = spill_end;
            layout.call.register_spill_size =
                stack.stack_cursor - spill_begin;
            stack.high_water =
                std::max(stack.high_water, stack.stack_cursor);
            break;
        }
        }
    }

    const auto has_argument_stack = std::any_of(
        argument_work.begin(), argument_work.end(),
        [](const ArgumentWork& work) {
            return work.whole_stack || !work.partial_stack.empty();
        });
    const auto has_result_stack = std::any_of(
        result_work.begin(), result_work.end(),
        [](const ResultWork& work) {
            return work.whole_stack || !work.partial_stack.empty();
        });
    if ((has_argument_stack && !allocated_arguments) ||
        (has_result_stack && !allocated_results) ||
        (reserve_argument_spills && !allocated_spills)) {
        return signature_fail(
            std::move(layout), ClassificationError::InvalidScalarMode,
            has_result_stack && !allocated_results ? results.size()
                                                   : arguments.size(),
            has_result_stack && !allocated_results);
    }

    for (const auto& assignment : layout.call.arguments) {
        layout.call.outgoing_area_alignment = std::max(
            layout.call.outgoing_area_alignment,
            assignment.stack_alignment);
    }
    for (const auto& assignment : layout.results) {
        layout.call.outgoing_area_alignment = std::max(
            layout.call.outgoing_area_alignment,
            assignment.stack_alignment);
    }
    layout.call.used_stack_size = stack.high_water;
    if (!checked_align(stack.high_water,
                       layout.call.outgoing_area_alignment,
                       layout.call.outgoing_area_size)) {
        return signature_fail(
            std::move(layout), ClassificationError::SizeOverflow,
            arguments.size(), false);
    }
    if (fixed_argument_count) {
        const auto ordered_cursors = [](const auto& source) {
            std::vector<AbiCursorUsage> result;
            result.reserve(source.size());
            for (const auto& [cursor, count] : source) {
                result.push_back({cursor, count});
            }
            std::sort(result.begin(), result.end(),
                      [](const AbiCursorUsage& left,
                         const AbiCursorUsage& right) {
                          return left.cursor < right.cursor;
                      });
            return result;
        };
        layout.call.named_cursors =
            ordered_cursors(named_register_cursors);
        layout.call.cursors =
            ordered_cursors(argument_registers.cursors);

        // A model can request another register-bank view of the same argument
        // slot (for example Win64 floating arguments mirrored in GPRs).
        for (std::size_t index = 0; index < layout.call.arguments.size();
             ++index) {
            auto& assignment = layout.call.arguments[index];
            for (const auto& shadow : abi.variadic_shadows) {
                const bool named = index < *fixed_argument_count;
                if ((named && !shadow.fixed_arguments) ||
                    (!named && !shadow.unnamed_arguments)) {
                    continue;
                }
                const auto* source = find_bank(abi, shadow.source_bank);
                const auto* target = find_bank(abi, shadow.target_bank);
                if (!source || !target) continue;
                for (const auto& piece : assignment.pieces) {
                    if (piece.location.kind != LocationKind::Register) {
                        continue;
                    }
                    const auto found = std::find(
                        source->arguments.begin(), source->arguments.end(),
                        piece.location.reg);
                    if (found == source->arguments.end()) continue;
                    const auto position = static_cast<std::size_t>(
                        found - source->arguments.begin());
                    if (position >= target->arguments.size()) continue;
                    auto duplicate = piece;
                    duplicate.location.reg = target->arguments[position];
                    duplicate.extension = AbiExtensionKind::None;
                    assignment.shadows.push_back(std::move(duplicate));
                }
            }
        }

        if (!abi.variadic_count_cursor.empty() &&
            !abi.variadic_count_register.empty()) {
            const auto count = abi_cursor_count(
                layout.call.cursors, abi.variadic_count_cursor);
            layout.call.implicit_register_values.push_back(
                {abi.variadic_count_register,
                 static_cast<std::uint64_t>(count),
                 abi.variadic_count_bits});
        }
    }
    return {std::move(layout), ClassificationError::None, 0, false};
}

} // namespace

ClassificationResult classify_parameters(
    const AbiEntry& abi, std::span<const AbiValue> parameters,
    AbiFeatureSet features) {
    return classify_call_arguments(abi, parameters, features);
}

ClassificationResult classify_call_arguments(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    AbiFeatureSet features) {
    auto classified = classify_signature_values(
        abi, arguments, {}, std::nullopt, features);
    return {std::move(classified.layout.call), classified.error,
            classified.error_value};
}

ClassificationResult classify_variadic_call_arguments(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::size_t fixed_argument_count, AbiFeatureSet features) {
    auto classified = classify_signature_values(
        abi, arguments, {}, fixed_argument_count, features);
    return {std::move(classified.layout.call), classified.error,
            classified.error_value};
}

ReturnAssignment classify_return(const AbiEntry& abi,
                                 const AbiValue& value,
                                 AbiFeatureSet features) {
    const std::array values{value};
    auto classified = classify_signature_values(
        abi, {}, values, std::nullopt, features);
    if (!classified || classified.layout.results.empty()) {
        ReturnAssignment result;
        result.value = value;
        result.mode = value.mode;
        result.error = classified.error == ClassificationError::None
                           ? ClassificationError::InvalidScalarMode
                           : classified.error;
        return result;
    }
    auto result = std::move(classified.layout.results.front());
    result.error = classified.error;
    return result;
}

SignatureClassificationResult classify_signature(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::span<const AbiValue> results, AbiFeatureSet features) {
    return classify_signature_values(
        abi, arguments, results, std::nullopt, features);
}

SignatureClassificationResult classify_variadic_signature(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::span<const AbiValue> results, std::size_t fixed_argument_count,
    AbiFeatureSet features) {
    return classify_signature_values(
        abi, arguments, results, fixed_argument_count, features);
}

std::size_t abi_cursor_count(std::span<const AbiCursorUsage> cursors,
                             std::string_view cursor) {
    const auto found = std::find_if(
        cursors.begin(), cursors.end(), [&](const AbiCursorUsage& value) {
            return value.cursor == cursor;
        });
    return found == cursors.end() ? 0 : found->count;
}

std::size_t variadic_save_bank_offset(const AbiEntry& abi,
                                      std::string_view bank) {
    std::size_t offset{};
    for (const auto& name : abi.variadic_save_banks) {
        const auto* entry = find_bank(abi, name);
        if (!entry) continue;
        if (name == bank) return offset;
        offset += entry->arguments.size() *
                  std::max<unsigned>(1, entry->register_bits / 8U);
    }
    return offset;
}

std::size_t variadic_save_area_size(const AbiEntry& abi) {
    return variadic_save_bank_offset(abi, {});
}

VariadicStateValue variadic_state_value(
    const AbiVariadicState& state,
    std::span<const AbiCursorUsage> named_cursors,
    std::size_t variadic_stack_offset) {
    const auto displacement =
        static_cast<std::size_t>(state.base) +
        abi_cursor_count(named_cursors, state.cursor) * state.stride;
    switch (state.kind) {
    case AbiVariadicStateKind::CursorOffset:
        return {VariadicStateBase::None, displacement};
    case AbiVariadicStateKind::CursorAddress:
        return {VariadicStateBase::IncomingArguments, displacement};
    case AbiVariadicStateKind::RegisterSaveAddress:
        return {VariadicStateBase::SaveArea, displacement};
    case AbiVariadicStateKind::StackAddress:
        break;
    }
    const auto alignment = static_cast<std::size_t>(state.alignment);
    return {VariadicStateBase::IncomingArguments,
            ((variadic_stack_offset + alignment - 1U) & ~(alignment - 1U)) +
                state.base};
}

} // namespace cross
