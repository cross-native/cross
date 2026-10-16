// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include "target/subtarget.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace cross::mir {
namespace {

using Operation = CostedBitOperation;

// The ways to compute a bit operation at one width.
enum class Plan {
    Native,         // the target's own instructions
    Identity,       // a one-byte swap
    Widen,          // on the operand zero-extended to 32 bits
    Split,          // on both halves of a wider operand
    SplitSelect,    // on the half that holds the leading or trailing one
    ViaPopcount,    // trailing zeros as the ones below the lowest one bit
    ViaClz,         // trailing zeros as the width less their leading zeros
    Inline,         // shifts, masks, and additions
    InlineMultiply, // a population count that sums its bytes by multiplying
};

// Lowers the bit intrinsics and rotations of one function. Each operation
// takes the cheapest plan by the -mtune costs, where an inline operation
// counts one cycle, a multiplication its latency, and a constant its
// materialization; the target's instructions win a tie. Costing runs the
// same builders as emission, without emitting.
class BitLowering {
public:
    BitLowering(ManagedFunction& function, const hir::Module& module,
                const Subtarget& subtarget)
        : function_(function), module_(module), subtarget_(subtarget),
          register_bits_(subtarget.integer_register_bits()) {}

    bool run() {
        ValueReplacements replacements;
        bool changed = false;
        for (std::size_t block = 0; block < function_.blocks.size(); ++block) {
            std::vector<ValueId> rewritten;
            for (const auto id : function_.blocks[block].values) {
                const auto operation = bit_operation(function_.values[id.value]);
                const auto bits = operation
                    ? integer_bits(function_.values[id.value].operands.front())
                    : 0U;
                if (!operation || bits == 0 ||
                    best(*operation, bits).second == Plan::Native) {
                    rewritten.push_back(id);
                    continue;
                }
                const auto source = function_.values[id.value];
                location_ = source.location;
                inline_site_ = source.inline_site;
                emitted_ = &rewritten;
                const auto x = as_type(source.operands.front(),
                                       unsigned_type(bits));
                const auto n = source.operands.size() == 2
                    ? as_type(source.operands[1], unsigned_type(bits))
                    : ValueId{};
                auto result = lower(*operation, bits, x, n);
                result = as_type(result, source.type);
                replacements.add(id, result);
                changed = true;
            }
            function_.blocks[block].values = std::move(rewritten);
        }
        if (!changed) return false;
        replacements.apply(function_);
        compact_managed_values(function_);
        return true;
    }

private:
    static constexpr unsigned no_cost = 1U << 20;

    std::optional<Operation> bit_operation(const ManagedValue& value) const {
        if (value.kind == ValueKind::Intrinsic && value.operands.size() == 1) {
            switch (value.intrinsic) {
            case IntrinsicOperation::Clz: return Operation::CountLeadingZeros;
            case IntrinsicOperation::Ctz: return Operation::CountTrailingZeros;
            case IntrinsicOperation::Popcount: return Operation::PopulationCount;
            case IntrinsicOperation::Bswap: return Operation::ByteSwap;
            default: return std::nullopt;
            }
        }
        if (value.kind == ValueKind::Binary && value.operands.size() == 2) {
            if (value.binary == BinaryOperation::RotateLeft)
                return Operation::RotateLeft;
            if (value.binary == BinaryOperation::RotateRight)
                return Operation::RotateRight;
        }
        return std::nullopt;
    }

    // The width of a scalar integer value, or zero for other values.
    unsigned integer_bits(ValueId id) const {
        const auto& type = module_.type(function_.values[id.value].type);
        if (type.kind != hir::Type::Kind::Builtin) return 0;
        switch (type.builtin) {
        case BuiltinType::I8: case BuiltinType::U8: return 8;
        case BuiltinType::I16: case BuiltinType::U16: return 16;
        case BuiltinType::I32: case BuiltinType::U32: return 32;
        case BuiltinType::I64: case BuiltinType::U64: return 64;
        case BuiltinType::I128: case BuiltinType::U128: return 128;
        case BuiltinType::Iptr: case BuiltinType::Uptr: return module_.address_bits;
        default: return 0;
        }
    }

    hir::TypeId unsigned_type(unsigned bits) const {
        return *module_.builtin(bits == 8 ? BuiltinType::U8
                                : bits == 16 ? BuiltinType::U16
                                : bits == 32 ? BuiltinType::U32
                                : bits == 64 ? BuiltinType::U64
                                             : BuiltinType::U128);
    }

    hir::TypeId u32() const { return *module_.builtin(BuiltinType::U32); }

    // The cheapest plan and its cost.
    std::pair<unsigned, Plan> best(Operation operation, unsigned bits) {
        const auto key = std::pair(operation, bits);
        if (const auto found = plans_.find(key); found != plans_.end())
            return found->second;
        if (operation == Operation::ByteSwap && bits == 8)
            return plans_[key] = {0, Plan::Identity};
        std::pair<unsigned, Plan> choice{no_cost, Plan::Inline};
        if (const auto native = subtarget_.bit_operation_cost(operation, bits))
            choice = {*native, Plan::Native};
        const auto consider = [&](Plan plan) {
            const auto cost = measure(operation, bits, plan);
            if (cost < choice.first) choice = {cost, plan};
        };
        // Inline operations count one each only up to the register width;
        // wider ones are pair sequences, so wider operands split instead.
        const bool native_width = bits <= register_bits_;
        const bool rotate = operation == Operation::RotateLeft ||
                            operation == Operation::RotateRight;
        if (rotate) {
            if (native_width || choice.second != Plan::Native)
                consider(Plan::Inline);
        } else {
            if (bits < 32) consider(Plan::Widen);
            if (bits > 32) consider(Plan::Split);
            if (bits > 32 && (operation == Operation::CountLeadingZeros ||
                              operation == Operation::CountTrailingZeros))
                consider(Plan::SplitSelect);
            if (native_width && bits >= 32 &&
                operation == Operation::CountTrailingZeros) {
                consider(Plan::ViaPopcount);
                consider(Plan::ViaClz);
            }
            if (native_width && operation != Operation::CountTrailingZeros) {
                consider(Plan::Inline);
                if (operation == Operation::PopulationCount && bits > 8)
                    consider(Plan::InlineMultiply);
            }
        }
        plans_[key] = choice;
        return choice;
    }

    unsigned measure(Operation operation, unsigned bits, Plan plan) {
        const auto saved_cost = cost_;
        const auto saved_counting = counting_;
        cost_ = 0;
        counting_ = true;
        (void)emit(operation, bits, plan, ValueId{}, ValueId{});
        const auto cost = cost_;
        cost_ = saved_cost;
        counting_ = saved_counting;
        return cost;
    }

    // The operation on `x`, and `n` for a rotation, of type uN.
    ValueId lower(Operation operation, unsigned bits, ValueId x, ValueId n) {
        const auto [cost, plan] = best(operation, bits);
        if (counting_) {
            cost_ += cost;
            return {};
        }
        return emit(operation, bits, plan, x, n);
    }

    ValueId emit(Operation operation, unsigned bits, Plan plan, ValueId x,
                 ValueId n) {
        const auto type = unsigned_type(bits);
        switch (plan) {
        case Plan::Native: return native(operation, type, x, n);
        case Plan::Identity: return x;
        case Plan::Widen: return widen(operation, bits, x);
        case Plan::Split:
        case Plan::SplitSelect: return split(operation, bits, plan, x);
        case Plan::ViaPopcount:
        case Plan::ViaClz: {
            // ~(x | -x) has a one in each trailing zero of x; a target may
            // count the leading ones of x | -x instead.
            const auto below = unary(
                UnaryOperation::BitNot, type,
                binary(BinaryOperation::BitOr, type, x,
                       unary(UnaryOperation::Negate, type, x)));
            if (plan == Plan::ViaPopcount)
                return lower(Operation::PopulationCount, bits, below, {});
            return binary(BinaryOperation::Subtract, u32(),
                          constant(u32(), bits),
                          lower(Operation::CountLeadingZeros, bits, below, {}));
        }
        case Plan::Inline:
        case Plan::InlineMultiply:
            switch (operation) {
            case Operation::CountLeadingZeros: return leading_zeros(bits, x);
            case Operation::PopulationCount:
                return population(bits, x, plan == Plan::InlineMultiply);
            case Operation::ByteSwap: return byte_swap(bits, x);
            default: return rotate(operation, bits, x, n);
            }
        }
        return x;
    }

    ValueId native(Operation operation, hir::TypeId type, ValueId x, ValueId n) {
        ManagedValue value;
        value.type = type;
        if (operation == Operation::RotateLeft ||
            operation == Operation::RotateRight) {
            value.kind = ValueKind::Binary;
            value.binary = operation == Operation::RotateLeft
                ? BinaryOperation::RotateLeft : BinaryOperation::RotateRight;
            value.operands = {x, n};
            return add(std::move(value));
        }
        value.kind = ValueKind::Intrinsic;
        value.intrinsic =
            operation == Operation::CountLeadingZeros ? IntrinsicOperation::Clz
            : operation == Operation::CountTrailingZeros ? IntrinsicOperation::Ctz
            : operation == Operation::PopulationCount ? IntrinsicOperation::Popcount
                                                      : IntrinsicOperation::Bswap;
        if (operation != Operation::ByteSwap) value.type = u32();
        value.operands = {x};
        return add(std::move(value));
    }

    // On the zero-extended 32-bit value: a leading-zero count less the added
    // zeros, a trailing-zero count stopped by a one bit just above the
    // operand, and a byte swap shifted back down.
    ValueId widen(Operation operation, unsigned bits, ValueId x) {
        const auto wide = cast(CastOperation::ZeroExtend, u32(), x);
        switch (operation) {
        case Operation::CountLeadingZeros:
            return binary(BinaryOperation::Subtract, u32(),
                          lower(operation, 32, wide, {}),
                          constant(u32(), 32 - bits));
        case Operation::CountTrailingZeros:
            return lower(operation, 32,
                         binary(BinaryOperation::BitOr, u32(), wide,
                                constant(u32(), shift_left(UInt128{1}, bits))),
                         {});
        case Operation::PopulationCount: return lower(operation, 32, wide, {});
        default:
            return cast(CastOperation::Truncate, unsigned_type(bits),
                        binary(BinaryOperation::ShiftRightLogical, u32(),
                               lower(operation, 32, wide, {}),
                               constant(u32(), 32 - bits)));
        }
    }

    ValueId split(Operation operation, unsigned bits, Plan plan, ValueId x) {
        const auto half = bits / 2;
        const auto type = unsigned_type(bits);
        const auto half_type = unsigned_type(half);
        const auto low = cast(CastOperation::Truncate, half_type, x);
        const auto high = cast(
            CastOperation::Truncate, half_type,
            binary(BinaryOperation::ShiftRightLogical, type, x,
                   constant(type, half)));
        if (operation == Operation::PopulationCount) {
            return binary(BinaryOperation::Add, u32(),
                          lower(operation, half, low, {}),
                          lower(operation, half, high, {}));
        }
        if (operation == Operation::ByteSwap) {
            const auto swapped_low = cast(CastOperation::ZeroExtend, type,
                                          lower(operation, half, low, {}));
            const auto swapped_high = cast(CastOperation::ZeroExtend, type,
                                           lower(operation, half, high, {}));
            return binary(BinaryOperation::BitOr, type,
                          binary(BinaryOperation::ShiftLeft, type, swapped_low,
                                 constant(type, half)),
                          swapped_high);
        }
        // The count of the half on the counted side, plus the other half's
        // count when that half is zero.
        const bool leading = operation == Operation::CountLeadingZeros;
        const auto first = leading ? high : low;
        const auto second = leading ? low : high;
        const auto empty = compare(BinaryOperation::Equal, half_type, first,
                                   constant(half_type, 0));
        if (plan == Plan::Split) {
            const auto mask = unary(UnaryOperation::Negate, u32(),
                                    cast(CastOperation::ZeroExtend, u32(), empty));
            return binary(BinaryOperation::Add, u32(),
                          lower(operation, half, first, {}),
                          binary(BinaryOperation::BitAnd, u32(),
                                 lower(operation, half, second, {}), mask));
        }
        const auto mask = unary(UnaryOperation::Negate, half_type,
                                cast(CastOperation::ZeroExtend, half_type, empty));
        const auto chosen = binary(
            BinaryOperation::BitXor, half_type, first,
            binary(BinaryOperation::BitAnd, half_type,
                   binary(BinaryOperation::BitXor, half_type, first, second),
                   mask));
        return binary(BinaryOperation::Add, u32(),
                      lower(operation, half, chosen, {}),
                      to_u32(half, binary(BinaryOperation::BitAnd, half_type,
                                          mask, constant(half_type, half))));
    }

    // A binary search: while the value is below 2^(bits - k), shift it left
    // by k, from k = bits/2 down to 1, and count the shifts; a zero operand
    // counts one more.
    ValueId leading_zeros(unsigned bits, ValueId x) {
        const auto width = std::max(bits, 32U);
        const auto type = unsigned_type(width);
        auto value = bits < width ? cast(CastOperation::ZeroExtend, type, x) : x;
        std::optional<ValueId> count;
        for (unsigned step = bits / 2; step != 0; step /= 2) {
            auto shift = cast(CastOperation::ZeroExtend, type,
                              compare(BinaryOperation::UnsignedLess, type, value,
                                      constant(type, shift_left(UInt128{1}, bits - step))));
            if (step > 1) {
                shift = binary(BinaryOperation::ShiftLeft, type, shift,
                               constant(type, static_cast<unsigned>(
                                                  std::countr_zero(step))));
                value = binary(BinaryOperation::ShiftLeft, type, value, shift);
            }
            count = count ? binary(BinaryOperation::Add, type, *count, shift) : shift;
        }
        const auto zero = cast(CastOperation::ZeroExtend, type,
                               compare(BinaryOperation::Equal, type, value,
                                       constant(type, 0)));
        return to_u32(width, binary(BinaryOperation::Add, type, *count, zero));
    }

    // Bit-pair, nibble, and byte sums, then the byte sums added by shifts
    // or by a multiplication that collects them in the top byte.
    ValueId population(unsigned bits, ValueId x, bool multiply) {
        const auto width = std::max(bits, 32U);
        const auto type = unsigned_type(width);
        const auto repeated = [&](std::uint8_t byte) {
            UInt128 result;
            for (unsigned index = 0; index < width / 8; ++index)
                result = bit_or(result, shift_left(UInt128{byte}, index * 8));
            return constant(type, result);
        };
        const auto shifted = [&](ValueId value, unsigned amount) {
            return binary(BinaryOperation::ShiftRightLogical, type, value,
                          constant(type, amount));
        };
        auto value = bits < width ? cast(CastOperation::ZeroExtend, type, x) : x;
        value = binary(BinaryOperation::Subtract, type, value,
                       binary(BinaryOperation::BitAnd, type, shifted(value, 1),
                              repeated(0x55)));
        const auto pairs = repeated(0x33);
        value = binary(BinaryOperation::Add, type,
                       binary(BinaryOperation::BitAnd, type, value, pairs),
                       binary(BinaryOperation::BitAnd, type, shifted(value, 2),
                              pairs));
        value = binary(BinaryOperation::BitAnd, type,
                       binary(BinaryOperation::Add, type, value, shifted(value, 4)),
                       repeated(0x0f));
        if (bits > 8 && multiply) {
            value = shifted(binary(BinaryOperation::Multiply, type, value,
                                   repeated(0x01)),
                            width - 8);
        } else if (bits > 8) {
            for (unsigned amount = 8; amount < bits; amount *= 2)
                value = binary(BinaryOperation::Add, type, value,
                               shifted(value, amount));
            value = binary(BinaryOperation::BitAnd, type, value,
                           constant(type, 0xff));
        }
        return to_u32(width, value);
    }

    // Swaps of ever larger neighboring pieces under masks, ending with the
    // two halves.
    ValueId byte_swap(unsigned bits, ValueId x) {
        const auto width = std::max(bits, 32U);
        const auto type = unsigned_type(width);
        auto value = bits < width ? cast(CastOperation::ZeroExtend, type, x) : x;
        for (unsigned amount = 8; amount < bits / 2; amount *= 2) {
            UInt128 pattern;
            for (unsigned index = 0; index < width; index += 2 * amount)
                pattern = bit_or(pattern,
                                 shift_left(subtract(shift_left(UInt128{1}, amount),
                                                     UInt128{1}),
                                            index));
            const auto mask = constant(type, pattern);
            value = binary(
                BinaryOperation::BitOr, type,
                binary(BinaryOperation::BitAnd, type,
                       binary(BinaryOperation::ShiftRightLogical, type, value,
                              constant(type, amount)),
                       mask),
                binary(BinaryOperation::ShiftLeft, type,
                       binary(BinaryOperation::BitAnd, type, value, mask),
                       constant(type, amount)));
        }
        value = binary(BinaryOperation::BitOr, type,
                       binary(BinaryOperation::ShiftRightLogical, type, value,
                              constant(type, bits / 2)),
                       binary(BinaryOperation::ShiftLeft, type, value,
                              constant(type, bits / 2)));
        return bits < width ? cast(CastOperation::Truncate, unsigned_type(bits), value)
                            : value;
    }

    // Two shifts by the count modulo the width; a narrow operand rotates in
    // 32 bits, where its shift by the full width is still defined.
    ValueId rotate(Operation operation, unsigned bits, ValueId x, ValueId n) {
        const bool left = operation == Operation::RotateLeft;
        const auto width = std::max(bits, 32U);
        const auto type = unsigned_type(width);
        std::optional<unsigned> amount;
        if (!counting_ && function_.values[n.value].kind == ValueKind::ConstantInteger)
            amount = static_cast<unsigned>(function_.values[n.value].integer % bits);
        ValueId first;
        ValueId second;
        if (amount) {
            if (bits < width) x = cast(CastOperation::ZeroExtend, type, x);
            first = constant(type, *amount);
            second = constant(type, bits < width ? bits - *amount
                                                 : (bits - *amount) % bits);
        } else if (bits < width) {
            x = cast(CastOperation::ZeroExtend, type, x);
            n = cast(CastOperation::ZeroExtend, type, n);
            first = binary(BinaryOperation::BitAnd, type, n, constant(type, bits - 1));
            second = binary(BinaryOperation::Subtract, type, constant(type, bits), first);
        } else {
            const auto mask = constant(type, bits - 1);
            first = binary(BinaryOperation::BitAnd, type, n, mask);
            second = binary(BinaryOperation::BitAnd, type,
                            unary(UnaryOperation::Negate, type, n), mask);
        }
        const auto result = binary(
            BinaryOperation::BitOr, type,
            binary(left ? BinaryOperation::ShiftLeft
                        : BinaryOperation::ShiftRightLogical,
                   type, x, first),
            binary(left ? BinaryOperation::ShiftRightLogical
                        : BinaryOperation::ShiftLeft,
                   type, x, second));
        return bits < width ? cast(CastOperation::Truncate, unsigned_type(bits), result)
                            : result;
    }

    ValueId to_u32(unsigned bits, ValueId value) {
        return bits == 32 ? value : cast(CastOperation::Truncate, u32(), value);
    }

    ValueId as_type(ValueId value, hir::TypeId type) {
        if (function_.values[value.value].type == type) return value;
        return cast(CastOperation::Reinterpret, type, value);
    }

    ValueId add(ManagedValue value) {
        if (counting_) {
            ++cost_;
            return {};
        }
        value.id = {static_cast<std::uint32_t>(function_.values.size())};
        value.location = location_;
        value.inline_site = inline_site_;
        const auto id = value.id;
        function_.values.push_back(std::move(value));
        emitted_->push_back(id);
        return id;
    }

    ValueId constant(hir::TypeId type, UInt128 integer) {
        if (counting_) {
            cost_ += subtarget_.integer_constant_materialization_cost(
                {integer_bits_of(type), integer.low, integer.high, false});
            return {};
        }
        ManagedValue value;
        value.kind = ValueKind::ConstantInteger;
        value.type = type;
        value.integer = integer.low;
        value.integer_high = integer.high;
        return add(std::move(value));
    }

    unsigned integer_bits_of(hir::TypeId type) const {
        switch (module_.type(type).builtin) {
        case BuiltinType::U8: return 8;
        case BuiltinType::U16: return 16;
        case BuiltinType::U32: return 32;
        case BuiltinType::U64: return 64;
        default: return 128;
        }
    }

    ValueId binary(BinaryOperation operation, hir::TypeId type, ValueId left,
                   ValueId right) {
        if (counting_ && operation == BinaryOperation::Multiply) {
            const auto latency = subtarget_.integer_operation_cost(
                CostedIntegerOperation::Multiply, {integer_bits_of(type), false});
            cost_ += latency ? *latency : no_cost;
            return {};
        }
        ManagedValue value;
        value.kind = ValueKind::Binary;
        value.binary = operation;
        value.type = type;
        value.operands = {left, right};
        return add(std::move(value));
    }

    ValueId compare(BinaryOperation operation, hir::TypeId, ValueId left,
                    ValueId right) {
        return binary(operation, *module_.builtin(BuiltinType::Bool), left, right);
    }

    ValueId unary(UnaryOperation operation, hir::TypeId type, ValueId operand) {
        ManagedValue value;
        value.kind = ValueKind::Unary;
        value.unary = operation;
        value.type = type;
        value.operands = {operand};
        return add(std::move(value));
    }

    ValueId cast(CastOperation operation, hir::TypeId type, ValueId operand) {
        ManagedValue value;
        value.kind = ValueKind::Cast;
        value.cast = operation;
        value.type = type;
        value.operands = {operand};
        return add(std::move(value));
    }

    ManagedFunction& function_;
    const hir::Module& module_;
    const Subtarget& subtarget_;
    unsigned register_bits_;
    std::map<std::pair<Operation, unsigned>, std::pair<unsigned, Plan>> plans_;
    bool counting_{};
    unsigned cost_{};
    SourceLocation location_;
    std::optional<InlineSiteId> inline_site_;
    std::vector<ValueId>* emitted_{};
};

} // namespace

bool lower_bit_operations(ManagedFunction& function,
                          const hir::Module& hir_module,
                          const Subtarget& subtarget) {
    return BitLowering(function, hir_module, subtarget).run();
}

} // namespace cross::mir
