// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include "common/diagnostic.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross::mir {
namespace {

struct Pair {
    ValueId low;
    ValueId high;
};

void replace_effect_uses(ManagedFunction& function, EffectId from, EffectId to) {
    for (auto& value : function.values)
        if (value.effect_input == from) value.effect_input = to;
    for (auto& effect : function.effects) {
        if (effect.input == from) effect.input = to;
        for (auto& incoming : effect.incoming)
            if (incoming.effect == from) incoming.effect = to;
    }
    for (auto& block : function.blocks)
        if (block.terminator.effect == from) block.terminator.effect = to;
}

// Splits 128-bit integers into 64-bit halves. Parameters and call results
// keep their type and are split after their definitions; call arguments and
// returned values are joined from their halves, so the target sees only
// those boundary forms at 128 bits: truncations, logical right shifts by
// 64, zero extensions, left shifts by 64, and their bitwise or.
class WideIntegerExpansion {
public:
    WideIntegerExpansion(ManagedFunction& function, hir::Module& module,
                         const Subtarget& subtarget, Diagnostics& diagnostics)
        : function_(function), module_(module), subtarget_(subtarget),
          diagnostics_(diagnostics),
          half_bits_(subtarget.target().selected_integer_bits),
          half_(*module.builtin(BuiltinType::U64)),
          bool_(*module.builtin(BuiltinType::Bool)),
          u32_(*module.builtin(BuiltinType::U32)),
          index_(*module.builtin(BuiltinType::Uptr)) {}

    bool run() {
        if (half_bits_ != 64 || !uses_wide_values()) return false;
        if (!supported()) return false;
        for (const auto block : block_order()) rebuild(block);
        for (const auto& [id, placeholder] : placeholders_) {
            const auto found = halves_.find(id);
            if (found == halves_.end()) continue;
            replacements_.add(placeholder.low, found->second.low);
            replacements_.add(placeholder.high, found->second.high);
        }
        replacements_.apply(function_);
        compact_managed_values(function_);
        return true;
    }

private:
    // ---- Types ----

    unsigned integer_bits(hir::TypeId id) const {
        const auto& type = module_.type(id);
        if (type.kind == hir::Type::Kind::Pointer) return module_.address_bits;
        if (type.kind != hir::Type::Kind::Builtin) return 0;
        switch (type.builtin) {
        case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return 8;
        case BuiltinType::I16: case BuiltinType::U16: return 16;
        case BuiltinType::I32: case BuiltinType::U32: return 32;
        case BuiltinType::I64: case BuiltinType::U64: return 64;
        case BuiltinType::I128: case BuiltinType::U128: return 128;
        case BuiltinType::Iptr: case BuiltinType::Uptr: return module_.address_bits;
        default: return 0;
        }
    }

    bool wide(hir::TypeId id) const {
        const auto& type = module_.type(id);
        return type.kind == hir::Type::Kind::Builtin &&
               (type.builtin == BuiltinType::I128 ||
                type.builtin == BuiltinType::U128);
    }

    unsigned floating_bits(hir::TypeId id) const {
        const auto& type = module_.type(id);
        if (type.builtin == BuiltinType::Fptr) return module_.address_bits;
        return type.builtin == BuiltinType::F32 ? 32U
             : type.builtin == BuiltinType::F64 ? 64U : 0U;
    }

    hir::TypeId floating_type(unsigned bits) const {
        return *module_.builtin(bits == 32 ? BuiltinType::F32 : BuiltinType::F64);
    }

    // The operand that a value stores, if it stores one.
    std::optional<ValueId> stored(const ManagedValue& value) const {
        if (value.kind == ValueKind::Store && value.operands.size() == 1)
            return value.operands[0];
        if (value.kind == ValueKind::PointerStore && value.operands.size() == 2)
            return value.operands[1];
        if (value.kind == ValueKind::GlobalStore && value.operands.size() == 1)
            return value.operands[0];
        return std::nullopt;
    }

    bool wide_operand(const ManagedValue& value) const {
        for (const auto operand : value.operands)
            if (wide(function_.values[operand.value].type)) return true;
        for (const auto& incoming : value.incoming)
            if (wide(function_.values[incoming.value.value].type)) return true;
        for (const auto& argument : value.call_arguments)
            if (argument.value && wide(function_.values[argument.value->value].type))
                return true;
        return false;
    }

    bool uses_wide_values() const {
        for (const auto& block : function_.blocks) {
            for (const auto id : block.values) {
                const auto& value = function_.values[id.value];
                if (wide(value.type) || wide_operand(value)) return true;
            }
            if (block.terminator.value &&
                wide(function_.values[block.terminator.value->value].type))
                return true;
        }
        return false;
    }

    // Diagnoses the operations on wide integers this pass does not split,
    // before it changes anything.
    bool supported() {
        bool valid = true;
        const auto reject = [&](const ManagedValue& value, std::string what) {
            diagnostics_.error(value.location,
                               what + " on a 128-bit integer has no runtime-free "
                                      "lowering for the target");
            valid = false;
        };
        for (const auto& block : function_.blocks) {
            for (const auto id : block.values) {
                const auto& value = function_.values[id.value];
                if (!wide(value.type) && !wide_operand(value)) continue;
                switch (value.kind) {
                case ValueKind::Parameter:
                case ValueKind::ConstantInteger:
                case ValueKind::Unary:
                case ValueKind::Select:
                case ValueKind::Phi:
                case ValueKind::Load:
                case ValueKind::Store:
                case ValueKind::PointerLoad:
                case ValueKind::PointerStore:
                case ValueKind::IndexedLoad:
                case ValueKind::GlobalLoad:
                case ValueKind::GlobalStore:
                case ValueKind::Call:
                    break;
                case ValueKind::Binary:
                    if (value.binary == BinaryOperation::UnsignedMultiplyHigh ||
                        value.binary == BinaryOperation::SignedMultiplyHigh) {
                        reject(value, "a multiply-high");
                    }
                    break;
                case ValueKind::Cast: {
                    const auto source = function_.values[value.operands[0].value].type;
                    const bool to_wide = wide(value.type);
                    const auto other = to_wide ? source : value.type;
                    switch (value.cast) {
                    case CastOperation::SignExtend:
                    case CastOperation::ZeroExtend:
                    case CastOperation::Truncate:
                        break;
                    case CastOperation::Reinterpret:
                        if (!wide(source) || !wide(value.type))
                            reject(value, "a bit reinterpretation");
                        break;
                    case CastOperation::SignedIntegerToFloat:
                    case CastOperation::UnsignedIntegerToFloat:
                    case CastOperation::FloatToSignedInteger:
                    case CastOperation::FloatToUnsignedInteger:
                        if (floating_bits(other) != 32 && floating_bits(other) != 64)
                            reject(value, "a conversion to or from this floating type");
                        break;
                    default:
                        reject(value, "this conversion");
                        break;
                    }
                    break;
                }
                case ValueKind::Intrinsic:
                    if (value.intrinsic != IntrinsicOperation::Expect &&
                        !bit_intrinsic_operation(value.intrinsic)) {
                        reject(value, "this intrinsic");
                    }
                    break;
                case ValueKind::Atomic:
                    reject(value, "an atomic operation");
                    break;
                case ValueKind::PatchValue:
                    reject(value, "a patch value");
                    break;
                case ValueKind::MachineInstruction:
                    reject(value, "a machine instruction");
                    break;
                default:
                    reject(value, "this operation");
                    break;
                }
            }
        }
        return valid;
    }

    // Reverse postorder from the entry, then the unreachable blocks, so that
    // a definition usually precedes its uses.
    std::vector<BlockId> block_order() const {
        std::vector<BlockId> order;
        std::vector<bool> seen(function_.blocks.size());
        std::vector<std::pair<BlockId, std::size_t>> stack{{function_.entry, 0}};
        seen[function_.entry.value] = true;
        std::vector<BlockId> postorder;
        while (!stack.empty()) {
            auto& [block, next] = stack.back();
            const auto& successors = function_.blocks[block.value].terminator.successors;
            if (next < successors.size()) {
                const auto successor = successors[next++];
                if (!seen[successor.value]) {
                    seen[successor.value] = true;
                    stack.push_back({successor, 0});
                }
                continue;
            }
            postorder.push_back(block);
            stack.pop_back();
        }
        order.assign(postorder.rbegin(), postorder.rend());
        for (std::size_t index = 0; index < function_.blocks.size(); ++index)
            if (!seen[index]) order.push_back({static_cast<std::uint32_t>(index)});
        return order;
    }

    // ---- Emission ----

    ValueId add(ManagedValue value) {
        value.id = {static_cast<std::uint32_t>(function_.values.size())};
        value.location = location_;
        value.inline_site = inline_site_;
        const auto id = value.id;
        function_.values.push_back(std::move(value));
        current_.push_back(id);
        return id;
    }

    std::optional<UInt128> constant_of(ValueId id) const {
        const auto& value = function_.values[id.value];
        if (value.kind != ValueKind::ConstantInteger) return std::nullopt;
        const auto bits = integer_bits(value.type);
        if (bits == 0) return std::nullopt;
        return mask_to(UInt128{value.integer, value.integer_high}, bits);
    }

    bool is_constant(ValueId id, UInt128 expected) const {
        const auto found = constant_of(id);
        return found && *found == expected;
    }

    ValueId constant(hir::TypeId type, UInt128 integer) {
        ManagedValue value;
        value.kind = ValueKind::ConstantInteger;
        value.type = type;
        const auto masked = mask_to(integer, std::max(1U, integer_bits(type)));
        value.integer = masked.low;
        value.integer_high = masked.high;
        return add(std::move(value));
    }

    ValueId zero() { return constant(half_, UInt128{}); }

    ValueId floating_constant(unsigned bits, std::uint64_t pattern) {
        ManagedValue value;
        value.kind = ValueKind::ConstantFloating;
        value.type = floating_type(bits);
        value.integer = pattern;
        return add(std::move(value));
    }

    static bool comparison(BinaryOperation operation) {
        return operation >= BinaryOperation::Equal;
    }

    ValueId binary(BinaryOperation operation, hir::TypeId type, ValueId left,
                   ValueId right) {
        const auto bits = integer_bits(type);
        const auto l = constant_of(left);
        const auto r = constant_of(right);
        if (l && r && bits != 0 && !comparison(operation)) {
            if (const auto folded = fold_integer_binary(operation, *l, *r, bits))
                return constant(type, *folded);
        }
        if (l && r && comparison(operation)) {
            const auto operand_bits = integer_bits(function_.values[left.value].type);
            if (const auto folded = fold_integer_binary(operation, *l, *r, operand_bits))
                return constant(bool_, *folded);
        }
        const UInt128 all = mask_to(bit_not(UInt128{}), std::max(1U, bits));
        switch (operation) {
        case BinaryOperation::Add:
        case BinaryOperation::BitOr:
        case BinaryOperation::BitXor:
            if (r && *r == UInt128{}) return left;
            if (l && *l == UInt128{}) return right;
            break;
        case BinaryOperation::Subtract:
        case BinaryOperation::ShiftLeft:
        case BinaryOperation::ShiftRightLogical:
        case BinaryOperation::ShiftRightArithmetic:
            if (r && *r == UInt128{}) return left;
            if (l && *l == UInt128{} && operation != BinaryOperation::Subtract &&
                operation != BinaryOperation::ShiftRightArithmetic) {
                return left;
            }
            break;
        case BinaryOperation::BitAnd:
            if ((r && *r == UInt128{}) || (l && *l == UInt128{}))
                return constant(type, UInt128{});
            if (r && *r == all) return left;
            if (l && *l == all) return right;
            break;
        case BinaryOperation::Multiply:
            if ((r && *r == UInt128{}) || (l && *l == UInt128{}))
                return constant(type, UInt128{});
            if (r && *r == UInt128{1}) return left;
            if (l && *l == UInt128{1}) return right;
            break;
        default:
            break;
        }
        ManagedValue value;
        value.kind = ValueKind::Binary;
        value.binary = operation;
        value.type = comparison(operation) ? bool_ : type;
        value.operands = {left, right};
        return add(std::move(value));
    }

    ValueId compare(BinaryOperation operation, ValueId left, ValueId right) {
        return binary(operation, function_.values[left.value].type, left, right);
    }

    ValueId unary(UnaryOperation operation, hir::TypeId type, ValueId operand) {
        if (const auto known = constant_of(operand)) {
            const auto bits = integer_bits(type);
            if (operation == UnaryOperation::Negate)
                return constant(type, mask_to(negate(*known), bits));
            if (operation == UnaryOperation::BitNot)
                return constant(type, mask_to(bit_not(*known), bits));
        }
        ManagedValue value;
        value.kind = ValueKind::Unary;
        value.unary = operation;
        value.type = type;
        value.operands = {operand};
        return add(std::move(value));
    }

    ValueId cast(CastOperation operation, hir::TypeId type, ValueId operand) {
        const auto source = function_.values[operand.value].type;
        if (source == type) return operand;
        if (const auto known = constant_of(operand)) {
            if (const auto folded = fold_integer_cast(
                    operation, *known, integer_bits(source), integer_bits(type)))
                return constant(type, *folded);
        }
        ManagedValue value;
        value.kind = ValueKind::Cast;
        value.cast = operation;
        value.type = type;
        value.operands = {operand};
        return add(std::move(value));
    }

    ValueId select(ValueId condition, ValueId truth, ValueId falsity) {
        if (truth == falsity) return truth;
        if (const auto known = constant_of(condition))
            return *known != UInt128{} ? truth : falsity;
        ManagedValue value;
        value.kind = ValueKind::Select;
        value.type = function_.values[truth.value].type;
        value.operands = {condition, truth, falsity};
        return add(std::move(value));
    }

    ValueId flag(ValueId condition) {
        return cast(CastOperation::ZeroExtend, half_, condition);
    }

    ValueId shifted(BinaryOperation operation, ValueId value, unsigned amount) {
        return binary(operation, half_, value, constant(half_, UInt128{amount}));
    }

    // ---- Halves ----

    Pair halves(ValueId id) {
        if (const auto found = halves_.find(id.value); found != halves_.end())
            return found->second;
        if (const auto found = placeholders_.find(id.value);
            found != placeholders_.end()) {
            return found->second;
        }
        // A use before the definition is expanded (a phi input on a back
        // edge or unreachable code) takes placeholders, which the definition
        // replaces at the end.
        const auto placeholder = [&] {
            ManagedValue value;
            value.kind = ValueKind::Phi;
            value.type = half_;
            value.id = {static_cast<std::uint32_t>(function_.values.size())};
            function_.values.push_back(value);
            return value.id;
        };
        const Pair pair{placeholder(), placeholder()};
        placeholders_[id.value] = pair;
        return pair;
    }

    Pair constant_pair(UInt128 value) {
        return {constant(half_, UInt128{value.low}), constant(half_, UInt128{value.high})};
    }

    // The full-width value of a wide operand: a kept boundary value itself,
    // otherwise the halves joined.
    ValueId join(ValueId id) {
        if (kept_.contains(id.value)) return id;
        const auto type = function_.values[id.value].type;
        const auto pair = halves(id);
        const auto low = constant_of(pair.low);
        const auto high = constant_of(pair.high);
        if (low && high) return constant(type, UInt128{low->low, high->low});
        return binary(
            BinaryOperation::BitOr, type,
            cast(CastOperation::ZeroExtend, type, pair.low),
            binary(BinaryOperation::ShiftLeft, type,
                   cast(CastOperation::ZeroExtend, type, pair.high),
                   constant(type, UInt128{half_bits_})));
    }

    // Splits a kept boundary value after its definition.
    void split(ValueId id) {
        kept_.insert(id.value);
        const auto type = function_.values[id.value].type;
        const auto low = cast(CastOperation::Truncate, half_, id);
        const auto high = cast(
            CastOperation::Truncate, half_,
            binary(BinaryOperation::ShiftRightLogical, type, id,
                   constant(type, UInt128{half_bits_})));
        halves_[id.value] = {low, high};
    }

    ValueId operand(ValueId id) { return replacements_.resolve(id); }

    // ---- Arithmetic on halves ----

    Pair negated(Pair x) {
        const auto low = binary(BinaryOperation::Subtract, half_, zero(), x.low);
        const auto borrow = flag(compare(BinaryOperation::NotEqual, x.low, zero()));
        const auto high = binary(
            BinaryOperation::Subtract, half_,
            binary(BinaryOperation::Subtract, half_, zero(), x.high), borrow);
        return {low, high};
    }

    Pair sum(Pair a, Pair b) {
        const auto low = binary(BinaryOperation::Add, half_, a.low, b.low);
        const auto carry = flag(compare(BinaryOperation::UnsignedLess, low, a.low));
        return {low, binary(BinaryOperation::Add, half_,
                            binary(BinaryOperation::Add, half_, a.high, b.high), carry)};
    }

    Pair difference(Pair a, Pair b) {
        const auto low = binary(BinaryOperation::Subtract, half_, a.low, b.low);
        const auto borrow = flag(compare(BinaryOperation::UnsignedLess, a.low, b.low));
        return {low, binary(BinaryOperation::Subtract, half_,
                            binary(BinaryOperation::Subtract, half_, a.high, b.high),
                            borrow)};
    }

    Pair bitwise(BinaryOperation operation, Pair a, Pair b) {
        return {binary(operation, half_, a.low, b.low),
                binary(operation, half_, a.high, b.high)};
    }

    Pair choose(ValueId condition, Pair truth, Pair falsity) {
        return {select(condition, truth.low, falsity.low),
                select(condition, truth.high, falsity.high)};
    }

    // The high half of the product of two halves, from the target's
    // multiply-high where it has one, else from the four products of their
    // quarters.
    ValueId multiply_high(ValueId x, ValueId y) {
        const auto l = constant_of(x);
        const auto r = constant_of(y);
        if (l && r) return constant(half_, UInt128{multiply(*l, *r).high});
        if ((l && *l == UInt128{}) || (r && *r == UInt128{})) return zero();
        if (subtarget_.integer_operation_cost(CostedIntegerOperation::MultiplyHigh,
                                              {half_bits_, false})) {
            ManagedValue value;
            value.kind = ValueKind::Binary;
            value.binary = BinaryOperation::UnsignedMultiplyHigh;
            value.type = half_;
            value.operands = {x, y};
            return add(std::move(value));
        }
        const auto quarter = half_bits_ / 2;
        const auto mask = constant(half_, mask_to(bit_not(UInt128{}), quarter));
        const auto low = [&](ValueId v) {
            return binary(BinaryOperation::BitAnd, half_, v, mask);
        };
        const auto high = [&](ValueId v) {
            return shifted(BinaryOperation::ShiftRightLogical, v, quarter);
        };
        const auto product = [&](ValueId a, ValueId b) {
            return binary(BinaryOperation::Multiply, half_, a, b);
        };
        const auto ll = product(low(x), low(y));
        const auto lh = product(low(x), high(y));
        const auto hl = product(high(x), low(y));
        const auto hh = product(high(x), high(y));
        const auto middle = binary(
            BinaryOperation::Add, half_,
            binary(BinaryOperation::Add, half_, high(ll), low(lh)), low(hl));
        return binary(BinaryOperation::Add, half_,
                      binary(BinaryOperation::Add, half_, hh, high(lh)),
                      binary(BinaryOperation::Add, half_, high(hl), high(middle)));
    }

    Pair product(Pair a, Pair b) {
        const auto low = binary(BinaryOperation::Multiply, half_, a.low, b.low);
        const auto high = binary(
            BinaryOperation::Add, half_,
            binary(BinaryOperation::Add, half_, multiply_high(a.low, b.low),
                   binary(BinaryOperation::Multiply, half_, a.low, b.high)),
            binary(BinaryOperation::Multiply, half_, a.high, b.low));
        return {low, high};
    }

    // A shift by a constant number of bits below the full width.
    Pair shift_constant(BinaryOperation operation, Pair x, unsigned amount) {
        const auto bits = half_bits_;
        if (amount == 0) return x;
        const bool left = operation == BinaryOperation::ShiftLeft;
        const bool arithmetic = operation == BinaryOperation::ShiftRightArithmetic;
        if (amount >= bits) {
            const auto rest = amount - bits;
            if (left)
                return {zero(), shifted(BinaryOperation::ShiftLeft, x.low, rest)};
            const auto sign = arithmetic
                ? shifted(BinaryOperation::ShiftRightArithmetic, x.high, bits - 1)
                : zero();
            return {shifted(operation, x.high, rest), sign};
        }
        if (left) {
            return {shifted(BinaryOperation::ShiftLeft, x.low, amount),
                    binary(BinaryOperation::BitOr, half_,
                           shifted(BinaryOperation::ShiftLeft, x.high, amount),
                           shifted(BinaryOperation::ShiftRightLogical, x.low,
                                   bits - amount))};
        }
        return {binary(BinaryOperation::BitOr, half_,
                       shifted(BinaryOperation::ShiftRightLogical, x.low, amount),
                       shifted(BinaryOperation::ShiftLeft, x.high, bits - amount)),
                shifted(operation, x.high, amount)};
    }

    // A shift by the count modulo the full width. A count of the half width
    // or more moves one half into the other; within a half, the bits that
    // cross between the halves shift by one and then by the complement of
    // the count, which never reaches the half width.
    Pair shift_variable(BinaryOperation operation, Pair x, ValueId count) {
        const auto bits = half_bits_;
        const auto masked = binary(BinaryOperation::BitAnd, half_, count,
                                   constant(half_, UInt128{2 * bits - 1}));
        const auto within = binary(BinaryOperation::BitAnd, half_, masked,
                                   constant(half_, UInt128{bits - 1}));
        const auto complement = binary(BinaryOperation::BitXor, half_, within,
                                       constant(half_, UInt128{bits - 1}));
        const auto across = compare(BinaryOperation::NotEqual,
                                    binary(BinaryOperation::BitAnd, half_, masked,
                                           constant(half_, UInt128{bits})),
                                    zero());
        if (operation == BinaryOperation::ShiftLeft) {
            const auto low = binary(BinaryOperation::ShiftLeft, half_, x.low, within);
            const auto high = binary(
                BinaryOperation::BitOr, half_,
                binary(BinaryOperation::ShiftLeft, half_, x.high, within),
                binary(BinaryOperation::ShiftRightLogical, half_,
                       shifted(BinaryOperation::ShiftRightLogical, x.low, 1),
                       complement));
            return {select(across, zero(), low), select(across, low, high)};
        }
        const auto high = binary(operation, half_, x.high, within);
        const auto low = binary(
            BinaryOperation::BitOr, half_,
            binary(BinaryOperation::ShiftRightLogical, half_, x.low, within),
            binary(BinaryOperation::ShiftLeft, half_,
                   shifted(BinaryOperation::ShiftLeft, x.high, 1), complement));
        const auto fill = operation == BinaryOperation::ShiftRightArithmetic
            ? shifted(BinaryOperation::ShiftRightArithmetic, x.high, bits - 1)
            : zero();
        return {select(across, high, low), select(across, fill, high)};
    }

    Pair shift(BinaryOperation operation, Pair x, Pair count) {
        if (const auto known = constant_of(count.low))
            return shift_constant(operation, x,
                                  static_cast<unsigned>(known->low % (2 * half_bits_)));
        return shift_variable(operation, x, count.low);
    }

    // A rotation by the count modulo the full width: a count of the half
    // width or more exchanges the halves first.
    Pair rotate(bool left, Pair x, ValueId count) {
        const auto bits = half_bits_;
        if (const auto known = constant_of(count)) {
            auto amount = static_cast<unsigned>(known->low % (2 * bits));
            if (!left) amount = (2 * bits - amount) % (2 * bits);
            if (amount >= bits) {
                x = {x.high, x.low};
                amount -= bits;
            }
            if (amount == 0) return x;
            return {binary(BinaryOperation::BitOr, half_,
                           shifted(BinaryOperation::ShiftLeft, x.low, amount),
                           shifted(BinaryOperation::ShiftRightLogical, x.high,
                                   bits - amount)),
                    binary(BinaryOperation::BitOr, half_,
                           shifted(BinaryOperation::ShiftLeft, x.high, amount),
                           shifted(BinaryOperation::ShiftRightLogical, x.low,
                                   bits - amount))};
        }
        auto masked = binary(BinaryOperation::BitAnd, half_, count,
                             constant(half_, UInt128{2 * bits - 1}));
        if (!left) {
            masked = binary(BinaryOperation::BitAnd, half_,
                            unary(UnaryOperation::Negate, half_, masked),
                            constant(half_, UInt128{2 * bits - 1}));
        }
        const auto across = compare(BinaryOperation::NotEqual,
                                    binary(BinaryOperation::BitAnd, half_, masked,
                                           constant(half_, UInt128{bits})),
                                    zero());
        const Pair swapped = choose(across, Pair{x.high, x.low}, x);
        const auto within = binary(BinaryOperation::BitAnd, half_, masked,
                                   constant(half_, UInt128{bits - 1}));
        const auto complement = binary(BinaryOperation::BitXor, half_, within,
                                       constant(half_, UInt128{bits - 1}));
        const auto funnel = [&](ValueId upper, ValueId lower) {
            return binary(BinaryOperation::BitOr, half_,
                          binary(BinaryOperation::ShiftLeft, half_, upper, within),
                          binary(BinaryOperation::ShiftRightLogical, half_,
                                 shifted(BinaryOperation::ShiftRightLogical, lower, 1),
                                 complement));
        };
        return {funnel(swapped.low, swapped.high), funnel(swapped.high, swapped.low)};
    }

    // A comparison of two wide values: the high halves decide unless they
    // are equal, when the low halves decide as unsigned values.
    ValueId compared(BinaryOperation operation, Pair a, Pair b) {
        if (operation == BinaryOperation::Equal ||
            operation == BinaryOperation::NotEqual) {
            const auto different = binary(
                BinaryOperation::BitOr, half_,
                binary(BinaryOperation::BitXor, half_, a.low, b.low),
                binary(BinaryOperation::BitXor, half_, a.high, b.high));
            return compare(operation, different, zero());
        }
        BinaryOperation strict{};
        BinaryOperation low{};
        switch (operation) {
        case BinaryOperation::SignedLess:
            strict = BinaryOperation::SignedLess;
            low = BinaryOperation::UnsignedLess;
            break;
        case BinaryOperation::SignedLessEqual:
            strict = BinaryOperation::SignedLess;
            low = BinaryOperation::UnsignedLessEqual;
            break;
        case BinaryOperation::SignedGreater:
            strict = BinaryOperation::SignedGreater;
            low = BinaryOperation::UnsignedGreater;
            break;
        case BinaryOperation::SignedGreaterEqual:
            strict = BinaryOperation::SignedGreater;
            low = BinaryOperation::UnsignedGreaterEqual;
            break;
        case BinaryOperation::UnsignedLess:
        case BinaryOperation::UnsignedLessEqual:
            strict = BinaryOperation::UnsignedLess;
            low = operation;
            break;
        default:
            strict = BinaryOperation::UnsignedGreater;
            low = operation;
            break;
        }
        return binary(
            BinaryOperation::BitOr, bool_, compare(strict, a.high, b.high),
            binary(BinaryOperation::BitAnd, bool_,
                   compare(BinaryOperation::Equal, a.high, b.high),
                   compare(low, a.low, b.low)));
    }

    ValueId negative(Pair x) {
        return compare(BinaryOperation::SignedLess, x.high, zero());
    }

    // ---- Division ----

    std::optional<unsigned> power_of_two(Pair x) const {
        const auto low = constant_of(x.low);
        const auto high = constant_of(x.high);
        if (!low || !high) return std::nullopt;
        const UInt128 value{low->low, high->low};
        if (value == UInt128{} || bit_and(value, subtract(value, UInt128{1})) != UInt128{})
            return std::nullopt;
        for (unsigned bit = 0; bit < 128; ++bit)
            if (value == shift_left(UInt128{1}, bit)) return bit;
        return std::nullopt;
    }

    Pair divide(const ManagedValue& value, Pair a, Pair b) {
        const bool is_signed = value.binary == BinaryOperation::SignedDivide ||
                               value.binary == BinaryOperation::SignedRemainder;
        const bool remainder = value.binary == BinaryOperation::SignedRemainder ||
                               value.binary == BinaryOperation::UnsignedRemainder;
        // A power of two shifts and masks; a signed dividend adds the
        // divisor less one when it is negative.
        if (const auto bit = power_of_two(b); bit && (!is_signed || *bit < 127)) {
            auto dividend = a;
            if (is_signed && *bit != 0) {
                const auto sign = shift_constant(BinaryOperation::ShiftRightArithmetic,
                                                 a, 2 * half_bits_ - 1);
                dividend = sum(a, shift_constant(BinaryOperation::ShiftRightLogical, sign,
                                                 2 * half_bits_ - *bit));
            }
            const auto quotient = shift_constant(
                is_signed ? BinaryOperation::ShiftRightArithmetic
                          : BinaryOperation::ShiftRightLogical,
                dividend, *bit);
            if (!remainder) return quotient;
            return difference(a, shift_constant(BinaryOperation::ShiftLeft, quotient, *bit));
        }
        // Two values whose high halves are zero divide as halves.
        if (!is_signed && is_constant(a.high, UInt128{}) && is_constant(b.high, UInt128{})) {
            return {binary(value.binary, half_, a.low, b.low), zero()};
        }
        std::optional<ValueId> dividend_negative;
        std::optional<ValueId> divisor_negative;
        if (is_signed) {
            dividend_negative = negative(a);
            divisor_negative = negative(b);
            a = choose(*dividend_negative, negated(a), a);
            b = choose(*divisor_negative, negated(b), b);
        }
        auto [quotient, rest] = divide_loop(a, b);
        if (is_signed) {
            const auto quotient_negative =
                binary(BinaryOperation::BitXor, bool_, *dividend_negative,
                       *divisor_negative);
            quotient = choose(quotient_negative, negated(quotient), quotient);
            rest = choose(*dividend_negative, negated(rest), rest);
        }
        return remainder ? rest : quotient;
    }

    // Restoring division of unsigned values, one quotient bit per pass of a
    // loop of its own block: the dividend shifts left into the remainder,
    // which keeps the divisor subtracted when it fits, and the freed low
    // bits of the dividend collect the quotient.
    std::pair<Pair, Pair> divide_loop(Pair dividend, Pair divisor) {
        const auto head = block_;
        const auto entry_effect = effect_;
        const BlockId loop{static_cast<std::uint32_t>(function_.blocks.size())};
        const BlockId after{loop.value + 1};
        const auto location = location_;
        const auto count_start = constant(u32_, UInt128{2 * half_bits_});
        const auto zero_value = zero();

        // The continuation receives the rest of the block and its terminator.
        function_.blocks[head.value].values = std::move(current_);
        current_.clear();
        auto terminator = function_.blocks[head.value].terminator;
        for (const auto successor : terminator.successors) {
            auto& target = function_.blocks[successor.value];
            std::replace(target.predecessors.begin(), target.predecessors.end(),
                         head, after);
            for (auto& incoming : function_.effects[target.effect.value].incoming)
                if (incoming.predecessor == head) incoming.predecessor = after;
            for (const auto id : target.values)
                for (auto& incoming : function_.values[id.value].incoming)
                    if (incoming.predecessor == head) incoming.predecessor = after;
        }
        const auto new_effect = [&](EffectKind kind) {
            ManagedEffect effect;
            effect.id = {static_cast<std::uint32_t>(function_.effects.size())};
            effect.location = location;
            effect.kind = kind;
            function_.effects.push_back(effect);
            return effect.id;
        };
        const auto loop_effect = new_effect(EffectKind::Phi);
        const auto after_effect = new_effect(EffectKind::Phi);
        // Everything after the division now follows the loop.
        replace_effect_uses(function_, entry_effect, after_effect);
        terminator.effect =
            terminator.effect == entry_effect ? after_effect : terminator.effect;
        function_.effects[loop_effect.value].incoming = {{head, entry_effect},
                                                         {loop, loop_effect}};
        function_.effects[after_effect.value].incoming = {{loop, loop_effect}};

        auto& start = function_.blocks[head.value];
        start.terminator = {TerminatorKind::Branch, location, std::nullopt, {loop},
                            entry_effect, inline_site_};

        ManagedBlock body;
        body.id = loop;
        body.location = location;
        body.predecessors = {head, loop};
        body.effect = loop_effect;
        ManagedBlock rest;
        rest.id = after;
        rest.location = location;
        rest.predecessors = {loop};
        rest.effect = after_effect;
        rest.terminator = terminator;
        function_.blocks.push_back(std::move(body));
        function_.blocks.push_back(std::move(rest));

        block_ = loop;
        const auto phi = [&](hir::TypeId type, ValueId initial) {
            ManagedValue value;
            value.kind = ValueKind::Phi;
            value.type = type;
            value.incoming = {{head, initial}};
            return add(std::move(value));
        };
        const auto count = phi(u32_, count_start);
        const Pair q{phi(half_, dividend.low), phi(half_, dividend.high)};
        const Pair r{phi(half_, zero_value), phi(half_, zero_value)};
        const auto top = half_bits_ - 1;
        const Pair shifted_rest{
            binary(BinaryOperation::BitOr, half_,
                   shifted(BinaryOperation::ShiftLeft, r.low, 1),
                   shifted(BinaryOperation::ShiftRightLogical, q.high, top)),
            binary(BinaryOperation::BitOr, half_,
                   shifted(BinaryOperation::ShiftLeft, r.high, 1),
                   shifted(BinaryOperation::ShiftRightLogical, r.low, top))};
        const Pair shifted_quotient{
            shifted(BinaryOperation::ShiftLeft, q.low, 1),
            binary(BinaryOperation::BitOr, half_,
                   shifted(BinaryOperation::ShiftLeft, q.high, 1),
                   shifted(BinaryOperation::ShiftRightLogical, q.low, top))};
        const auto fits = binary(
            BinaryOperation::BitOr, bool_,
            compare(BinaryOperation::UnsignedGreater, shifted_rest.high, divisor.high),
            binary(BinaryOperation::BitAnd, bool_,
                   compare(BinaryOperation::Equal, shifted_rest.high, divisor.high),
                   compare(BinaryOperation::UnsignedGreaterEqual, shifted_rest.low,
                           divisor.low)));
        // The divisor, or zero when it does not fit, is subtracted without a
        // branch or select.
        const auto bit = flag(fits);
        const auto mask = unary(UnaryOperation::Negate, half_, bit);
        const auto next_rest = difference(
            shifted_rest, bitwise(BinaryOperation::BitAnd, divisor, Pair{mask, mask}));
        const Pair next_quotient{
            binary(BinaryOperation::BitOr, half_, shifted_quotient.low, bit),
            shifted_quotient.high};
        const auto next_count = binary(BinaryOperation::Subtract, u32_, count,
                                       constant(u32_, UInt128{1}));
        const auto more = compare(BinaryOperation::NotEqual, next_count,
                                  constant(u32_, UInt128{}));
        for (const auto& [node, next] :
             {std::pair{count, next_count}, std::pair{q.low, next_quotient.low},
              std::pair{q.high, next_quotient.high}, std::pair{r.low, next_rest.low},
              std::pair{r.high, next_rest.high}}) {
            function_.values[node.value].incoming.push_back({loop, next});
        }
        auto& loop_block = function_.blocks[loop.value];
        loop_block.values = std::move(current_);
        current_.clear();
        loop_block.terminator = {TerminatorKind::ConditionalBranch, location, more,
                                 {loop, after}, loop_effect, inline_site_};

        block_ = after;
        effect_ = after_effect;
        return {next_quotient, next_rest};
    }

    // ---- Floating conversions ----

    // The leading zeros of a wide value as a u32: of the high half, plus
    // those of the low half when the high half is zero.
    ValueId leading_zeros(Pair x) {
        const auto count = [&](ValueId half) {
            ManagedValue value;
            value.kind = ValueKind::Intrinsic;
            value.intrinsic = IntrinsicOperation::Clz;
            value.type = u32_;
            value.operands = {half};
            return add(std::move(value));
        };
        const auto empty = compare(BinaryOperation::Equal, x.high, zero());
        const auto mask = unary(UnaryOperation::Negate, u32_,
                                cast(CastOperation::ZeroExtend, u32_, empty));
        return binary(BinaryOperation::Add, u32_, count(x.high),
                      binary(BinaryOperation::BitAnd, u32_, count(x.low), mask));
    }

    // An integer to f32 or f64. The magnitude is normalized so that its top
    // bit is set; its leading 31 bits (and for f64 the next 31) convert
    // exactly from signed words that carry the value's sign, every bit below
    // folded into the last word's lowest bit, below the rounding position.
    // The one conversion or addition that rounds sees the signed value, so
    // it rounds in the current mode, and the scaling by a power of two that
    // follows is exact apart from overflow to infinity.
    ValueId to_floating(Pair x, bool is_signed, unsigned bits) {
        const auto i32 = *module_.builtin(BuiltinType::I32);
        const auto sign = is_signed ? negative(x) : constant(bool_, UInt128{});
        const auto magnitude = is_signed ? choose(sign, negated(x), x) : x;
        const auto zeros = leading_zeros(magnitude);
        const auto normal = shift_variable(BinaryOperation::ShiftLeft, magnitude,
                                           cast(CastOperation::ZeroExtend, half_, zeros));
        const auto word_mask = constant(half_, UInt128{0x7fffffffU});
        const auto signed_word = [&](ValueId word) {
            const auto narrow = cast(CastOperation::Truncate, i32, word);
            return select(sign, unary(UnaryOperation::Negate, i32, narrow), narrow);
        };
        const auto convert = [&](ValueId word) {
            ManagedValue value;
            value.kind = ValueKind::Cast;
            value.cast = CastOperation::SignedIntegerToFloat;
            value.type = floating_type(bits);
            value.operands = {signed_word(word)};
            return add(std::move(value));
        };
        const auto floating_binary = [&](BinaryOperation operation, ValueId left,
                                         ValueId right) {
            ManagedValue value;
            value.kind = ValueKind::Binary;
            value.binary = operation;
            value.type = floating_type(bits);
            value.operands = {left, right};
            return add(std::move(value));
        };
        // The leading word is bits 127-97 of the normalized value.
        const auto leading = shifted(BinaryOperation::ShiftRightLogical, normal.high, 33);
        ValueId rounded;
        ValueId exponent;
        if (bits == 32) {
            const auto below = binary(
                BinaryOperation::BitOr, half_,
                binary(BinaryOperation::BitAnd, half_, normal.high,
                       constant(half_, UInt128{0x1ffffffffULL})),
                normal.low);
            const auto word = binary(BinaryOperation::BitOr, half_, leading,
                                     flag(compare(BinaryOperation::NotEqual, below, zero())));
            rounded = convert(word);
            // The leading word counts in units of 2^(97 - zeros).
            exponent = binary(BinaryOperation::Subtract, u32_,
                              constant(u32_, UInt128{127 + 97}), zeros);
            const auto scale_bits = binary(BinaryOperation::ShiftLeft, u32_, exponent,
                                           constant(u32_, UInt128{23}));
            ManagedValue scale;
            scale.kind = ValueKind::Cast;
            scale.cast = CastOperation::Reinterpret;
            scale.type = floating_type(32);
            scale.operands = {scale_bits};
            return floating_binary(BinaryOperation::Multiply, rounded, add(std::move(scale)));
        }
        // The second word is bits 96-66.
        const auto second = binary(
            BinaryOperation::BitAnd, half_,
            shifted(BinaryOperation::ShiftRightLogical, normal.high, 2), word_mask);
        const auto below = binary(BinaryOperation::BitOr, half_,
                                  binary(BinaryOperation::BitAnd, half_, normal.high,
                                         constant(half_, UInt128{3})),
                                  normal.low);
        const auto last = binary(BinaryOperation::BitOr, half_, second,
                                 flag(compare(BinaryOperation::NotEqual, below, zero())));
        const auto upper = floating_binary(BinaryOperation::Multiply, convert(leading),
                                           floating_constant(64, 0x41e0000000000000ULL));
        rounded = floating_binary(BinaryOperation::Add, upper, convert(last));
        // The words count in units of 2^(66 - zeros).
        exponent = binary(BinaryOperation::Subtract, u32_,
                          constant(u32_, UInt128{1023 + 66}), zeros);
        const auto scale_bits = shifted(BinaryOperation::ShiftLeft,
                                        cast(CastOperation::ZeroExtend, half_, exponent),
                                        52);
        ManagedValue scale;
        scale.kind = ValueKind::Cast;
        scale.cast = CastOperation::Reinterpret;
        scale.type = floating_type(64);
        scale.operands = {scale_bits};
        return floating_binary(BinaryOperation::Multiply, rounded, add(std::move(scale)));
    }

    // An f32 or f64 to an integer, truncated toward zero from its fields:
    // the significand with its leading one shifts left or right by its
    // exponent. Values of magnitude below one give zero; a value out of
    // range has no defined result.
    Pair from_floating(ValueId value, unsigned bits) {
        const auto word = bits == 32 ? *module_.builtin(BuiltinType::U32) : half_;
        ManagedValue reinterpret;
        reinterpret.kind = ValueKind::Cast;
        reinterpret.cast = CastOperation::Reinterpret;
        reinterpret.type = word;
        reinterpret.operands = {value};
        const auto raw = cast(CastOperation::ZeroExtend, half_, add(std::move(reinterpret)));
        const unsigned fraction = bits == 32 ? 23 : 52;
        const unsigned exponent_mask = bits == 32 ? 0xff : 0x7ff;
        const unsigned bias = bits == 32 ? 127 : 1023;
        const auto exponent = binary(BinaryOperation::BitAnd, half_,
                                     shifted(BinaryOperation::ShiftRightLogical, raw, fraction),
                                     constant(half_, UInt128{exponent_mask}));
        const auto significand = binary(
            BinaryOperation::BitOr, half_,
            binary(BinaryOperation::BitAnd, half_, raw,
                   constant(half_, subtract(shift_left(UInt128{1}, fraction), UInt128{1}))),
            constant(half_, shift_left(UInt128{1}, fraction)));
        const auto point = constant(half_, UInt128{bias + fraction});
        const auto left = compare(BinaryOperation::UnsignedGreaterEqual, exponent, point);
        const auto small = compare(BinaryOperation::UnsignedLess, exponent,
                                   constant(half_, UInt128{bias}));
        const auto raised = shift_variable(
            BinaryOperation::ShiftLeft, Pair{significand, zero()},
            binary(BinaryOperation::Subtract, half_, exponent, point));
        const auto lowered = binary(BinaryOperation::ShiftRightLogical, half_, significand,
                                    binary(BinaryOperation::Subtract, half_, point, exponent));
        const Pair magnitude = choose(
            left, raised, Pair{select(small, zero(), lowered), zero()});
        const auto sign = compare(BinaryOperation::NotEqual,
                                  shifted(BinaryOperation::ShiftRightLogical, raw, bits - 1),
                                  zero());
        return choose(sign, negated(magnitude), magnitude);
    }

    // ---- Memory ----

    struct HalfAddresses {
        ValueId low;
        ValueId high;
        hir::TypeId type;
    };

    // The addresses of the halves of a wide object at `pointer`, as
    // pointers to halves with the object's volatility; a const object may
    // still be initialized.
    HalfAddresses half_addresses(ValueId pointer) {
        const auto pointer_type = function_.values[pointer.value].type;
        const auto pointee = *module_.type(pointer_type).pointee;
        const bool is_volatile = module_.type(pointee).is_volatile;
        const bool may_alias = module_.type(pointee).may_alias;
        const auto element =
            module_.add_qualifiers(half_, false, is_volatile, may_alias);
        const auto half_pointer = module_.pointer_to(element);
        const auto base = cast(CastOperation::Reinterpret, half_pointer, pointer);
        const auto at = [&](unsigned index) {
            if (index == 0) return base;
            ManagedValue value;
            value.kind = ValueKind::IndexedAddress;
            value.type = half_pointer;
            value.operands = {base, constant(index_, UInt128{index})};
            return add(std::move(value));
        };
        const bool little =
            subtarget_.target().data_layout.byte_order == ByteOrder::Little;
        return {at(little ? 0 : 1), at(little ? 1 : 0), element};
    }

    unsigned half_alignment(const ManagedValue& access, hir::TypeId wide_type) const {
        auto alignment = access.memory_alignment;
        if (alignment == 0) {
            alignment = static_cast<unsigned>(
                hir::natural_alignment(module_, wide_type, subtarget_.target())
                    .value_or(1));
        }
        return alignment >= half_bits_ / 8 ? 0 : alignment;
    }

    // An operation of the effect chain, placed after the current effect.
    ValueId chained(ManagedValue value, std::optional<EffectId> output = std::nullopt) {
        value.effect_input = effect_;
        if (output) {
            value.effect_output = *output;
        } else {
            ManagedEffect effect;
            effect.id = {static_cast<std::uint32_t>(function_.effects.size())};
            effect.location = location_;
            effect.kind = EffectKind::Operation;
            function_.effects.push_back(effect);
            value.effect_output = effect.id;
        }
        const auto id = add(std::move(value));
        auto& effect = function_.effects[function_.values[id.value].effect_output->value];
        effect.input = effect_;
        effect.operation = id;
        effect_ = *function_.values[id.value].effect_output;
        return id;
    }

    // The address of the wide object that a load or store accesses.
    ValueId object_address(const ManagedValue& value) {
        if (value.slot) {
            auto& slot = function_.slots[value.slot->value];
            slot.address_taken = true;
            ManagedValue address;
            address.kind = ValueKind::SlotAddress;
            address.type = module_.pointer_to(slot.type);
            address.slot = value.slot;
            return add(std::move(address));
        }
        if (value.object) {
            ManagedValue address;
            address.kind = ValueKind::GlobalAddress;
            address.type = module_.pointer_to(module_.object(*value.object).type);
            address.object = value.object;
            return add(std::move(address));
        }
        if (value.kind == ValueKind::IndexedLoad) {
            ManagedValue address;
            address.kind = ValueKind::IndexedAddress;
            address.type = function_.values[value.operands[0].value].type;
            address.operands = {operand(value.operands[0]), operand(value.operands[1])};
            return add(std::move(address));
        }
        return operand(value.operands[0]);
    }

    Pair load(const ManagedValue& value) {
        const auto addresses = half_addresses(object_address(value));
        const auto alignment = half_alignment(value, value.type);
        const auto access = [&](ValueId address, std::optional<EffectId> output) {
            ManagedValue load;
            load.kind = ValueKind::PointerLoad;
            load.type = addresses.type;
            load.operands = {address};
            load.memory_alignment = alignment;
            load.is_volatile_access = module_.type(addresses.type).is_volatile;
            const auto id = chained(std::move(load), output);
            return addresses.type == half_
                ? id : cast(CastOperation::Reinterpret, half_, id);
        };
        const auto low = access(addresses.low, std::nullopt);
        const auto high = access(addresses.high, value.effect_output);
        return {low, high};
    }

    void store(const ManagedValue& value, ValueId source) {
        const auto addresses = half_addresses(object_address(value));
        const auto alignment = half_alignment(value, function_.values[source.value].type);
        const auto pair = halves(source);
        const auto access = [&](ValueId address, ValueId half,
                                std::optional<EffectId> output) {
            ManagedValue store;
            store.kind = ValueKind::PointerStore;
            store.type = *module_.builtin(BuiltinType::Void);
            store.operands = {address, half};
            store.memory_alignment = alignment;
            store.is_volatile_access = module_.type(addresses.type).is_volatile;
            chained(std::move(store), output);
        };
        access(addresses.low, pair.low, std::nullopt);
        access(addresses.high, pair.high, value.effect_output);
    }

    // ---- Values ----

    void keep(ValueId id) {
        current_.push_back(id);
        const auto& value = function_.values[id.value];
        if (value.effect_output) effect_ = *value.effect_output;
    }

    void rebuild(BlockId block) {
        block_ = block;
        effect_ = function_.blocks[block.value].effect;
        const auto original = function_.blocks[block.value].values;
        current_.clear();
        for (const auto id : original) process(id);
        auto& terminator = function_.blocks[block_.value].terminator;
        if (terminator.value && wide(function_.values[terminator.value->value].type)) {
            location_ = terminator.location;
            inline_site_ = terminator.inline_site;
            const auto joined = join(*terminator.value);
            function_.blocks[block_.value].terminator.value = joined;
        }
        function_.blocks[block_.value].values = std::move(current_);
        current_.clear();
    }

    void process(ValueId id) {
        const auto value = function_.values[id.value];
        location_ = value.location;
        inline_site_ = value.inline_site;
        if (value.kind == ValueKind::Parameter) {
            keep(id);
            if (wide(value.type)) split(id);
            return;
        }
        if (value.kind == ValueKind::Call) {
            // The ordinary operands repeat the value arguments after an
            // indirect call's target.
            auto arguments = value.call_arguments;
            std::vector<ValueId> operands;
            if (value.call_signature) operands.push_back(value.operands.front());
            for (auto& argument : arguments) {
                if (argument.value && wide(function_.values[argument.value->value].type))
                    argument.value = join(*argument.value);
                if (argument.value) operands.push_back(*argument.value);
            }
            function_.values[id.value].call_arguments = std::move(arguments);
            function_.values[id.value].operands = std::move(operands);
            keep(id);
            // A must-tail call's result is only returned, as it is.
            if (wide(value.type) && value.must_tail) kept_.insert(id.value);
            else if (wide(value.type)) split(id);
            return;
        }
        if (value.kind == ValueKind::Phi) {
            if (!wide(value.type)) {
                keep(id);
                return;
            }
            const auto phi = [&](bool high) {
                ManagedValue half;
                half.kind = ValueKind::Phi;
                half.type = half_;
                for (const auto& incoming : value.incoming) {
                    const auto pair = halves(incoming.value);
                    half.incoming.push_back(
                        {incoming.predecessor, high ? pair.high : pair.low});
                }
                return add(std::move(half));
            };
            const auto low = phi(false);
            const auto high = phi(true);
            halves_[id.value] = {low, high};
            return;
        }
        if (!wide(value.type) && !wide_operand(value)) {
            keep(id);
            return;
        }
        expand(id, value);
    }

    void result(ValueId id, Pair pair) { halves_[id.value] = pair; }

    void result(ValueId id, ValueId replacement) { replacements_.add(id, replacement); }

    void expand(ValueId id, const ManagedValue& value) {
        switch (value.kind) {
        case ValueKind::ConstantInteger:
            result(id, constant_pair(UInt128{value.integer, value.integer_high}));
            return;
        case ValueKind::Unary: {
            const auto x = halves(value.operands[0]);
            if (value.unary == UnaryOperation::Negate) {
                result(id, negated(x));
            } else if (value.unary == UnaryOperation::BitNot) {
                result(id, Pair{unary(UnaryOperation::BitNot, half_, x.low),
                                unary(UnaryOperation::BitNot, half_, x.high)});
            } else {
                result(id, compare(BinaryOperation::Equal,
                                   binary(BinaryOperation::BitOr, half_, x.low, x.high),
                                   zero()));
            }
            return;
        }
        case ValueKind::Binary:
            expand_binary(id, value);
            return;
        case ValueKind::Cast:
            expand_cast(id, value);
            return;
        case ValueKind::Select:
            result(id, choose(operand(value.operands[0]), halves(value.operands[1]),
                              halves(value.operands[2])));
            return;
        case ValueKind::Intrinsic:
            expand_intrinsic(id, value);
            return;
        case ValueKind::Load:
        case ValueKind::PointerLoad:
        case ValueKind::IndexedLoad:
        case ValueKind::GlobalLoad:
            result(id, load(value));
            return;
        case ValueKind::Store:
        case ValueKind::PointerStore:
        case ValueKind::GlobalStore:
            store(value, *stored(value));
            return;
        default:
            keep(id);
            return;
        }
    }

    void expand_binary(ValueId id, const ManagedValue& value) {
        const auto a = halves(value.operands[0]);
        const auto b = halves(value.operands[1]);
        switch (value.binary) {
        case BinaryOperation::Add: result(id, sum(a, b)); return;
        case BinaryOperation::Subtract: result(id, difference(a, b)); return;
        case BinaryOperation::Multiply: result(id, product(a, b)); return;
        case BinaryOperation::BitAnd:
        case BinaryOperation::BitOr:
        case BinaryOperation::BitXor:
            result(id, bitwise(value.binary, a, b));
            return;
        case BinaryOperation::ShiftLeft:
        case BinaryOperation::ShiftRightLogical:
        case BinaryOperation::ShiftRightArithmetic:
            result(id, shift(value.binary, a, b));
            return;
        case BinaryOperation::RotateLeft:
        case BinaryOperation::RotateRight:
            result(id, rotate(value.binary == BinaryOperation::RotateLeft, a, b.low));
            return;
        case BinaryOperation::SignedDivide:
        case BinaryOperation::UnsignedDivide:
        case BinaryOperation::SignedRemainder:
        case BinaryOperation::UnsignedRemainder:
            result(id, divide(value, a, b));
            return;
        default:
            result(id, compared(value.binary, a, b));
            return;
        }
    }

    void expand_cast(ValueId id, const ManagedValue& value) {
        const auto source_id = value.operands[0];
        const auto source = function_.values[source_id.value].type;
        switch (value.cast) {
        case CastOperation::ZeroExtend:
        case CastOperation::SignExtend: {
            if (wide(source)) {
                result(id, halves(source_id));
                return;
            }
            const auto low = cast(integer_bits(source) == half_bits_
                                      ? CastOperation::Reinterpret : value.cast,
                                  half_, operand(source_id));
            const auto high = value.cast == CastOperation::ZeroExtend
                ? zero()
                : shifted(BinaryOperation::ShiftRightArithmetic, low, half_bits_ - 1);
            result(id, Pair{low, high});
            return;
        }
        case CastOperation::Truncate: {
            const auto x = halves(source_id);
            if (wide(value.type)) {
                result(id, x);
                return;
            }
            result(id, cast(integer_bits(value.type) == half_bits_
                                ? CastOperation::Reinterpret : CastOperation::Truncate,
                            value.type, x.low));
            return;
        }
        case CastOperation::Reinterpret:
            result(id, halves(source_id));
            return;
        case CastOperation::SignedIntegerToFloat:
        case CastOperation::UnsignedIntegerToFloat:
            result(id, cast(CastOperation::Reinterpret, value.type,
                            to_floating(halves(source_id),
                                        value.cast ==
                                            CastOperation::SignedIntegerToFloat,
                                        floating_bits(value.type))));
            return;
        default:
            result(id, from_floating(operand(source_id), floating_bits(source)));
            return;
        }
    }

    void expand_intrinsic(ValueId id, const ManagedValue& value) {
        const auto x = halves(value.operands[0]);
        const auto intrinsic = [&](IntrinsicOperation operation, hir::TypeId type,
                                   ValueId half) {
            ManagedValue node;
            node.kind = ValueKind::Intrinsic;
            node.intrinsic = operation;
            node.type = type;
            node.operands = {half};
            return add(std::move(node));
        };
        switch (value.intrinsic) {
        case IntrinsicOperation::Expect:
            result(id, x);
            return;
        case IntrinsicOperation::Bswap:
            result(id, Pair{intrinsic(IntrinsicOperation::Bswap, half_, x.high),
                            intrinsic(IntrinsicOperation::Bswap, half_, x.low)});
            return;
        case IntrinsicOperation::Popcount:
            result(id, cast(CastOperation::Truncate, value.type,
                            binary(BinaryOperation::Add, u32_,
                                   intrinsic(IntrinsicOperation::Popcount, u32_, x.low),
                                   intrinsic(IntrinsicOperation::Popcount, u32_, x.high))));
            return;
        default: {
            // The count of the half on the counted side, plus the other
            // half's count when that half is zero.
            const bool leading = value.intrinsic == IntrinsicOperation::Clz;
            const auto first = leading ? x.high : x.low;
            const auto second = leading ? x.low : x.high;
            const auto empty = compare(BinaryOperation::Equal, first, zero());
            const auto mask = unary(UnaryOperation::Negate, u32_,
                                    cast(CastOperation::ZeroExtend, u32_, empty));
            result(id, cast(CastOperation::Truncate, value.type,
                            binary(BinaryOperation::Add, u32_,
                                   intrinsic(value.intrinsic, u32_, first),
                                   binary(BinaryOperation::BitAnd, u32_,
                                          intrinsic(value.intrinsic, u32_, second),
                                          mask))));
            return;
        }
        }
    }

    ManagedFunction& function_;
    hir::Module& module_;
    const Subtarget& subtarget_;
    Diagnostics& diagnostics_;
    unsigned half_bits_;
    hir::TypeId half_;
    hir::TypeId bool_;
    hir::TypeId u32_;
    hir::TypeId index_;
    std::unordered_map<std::uint32_t, Pair> halves_;
    std::unordered_map<std::uint32_t, Pair> placeholders_;
    std::unordered_set<std::uint32_t> kept_;
    ValueReplacements replacements_;
    std::vector<ValueId> current_;
    BlockId block_;
    EffectId effect_;
    SourceLocation location_;
    std::optional<InlineSiteId> inline_site_;
};

} // namespace

bool expand_wide_integers(ManagedFunction& function, hir::Module& hir_module,
                          const Subtarget& subtarget, Diagnostics& diagnostics) {
    if (subtarget.target().selected_integer_bits >= 128) return false;
    return WideIntegerExpansion(function, hir_module, subtarget, diagnostics).run();
}

} // namespace cross::mir
