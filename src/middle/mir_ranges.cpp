// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross::mir {
namespace {

// The values an integer of at most 64 bits may hold, as an unsigned and a
// signed interval that both contain each of them. Lineage: the limits of
// Go's prove pass; LLVM's ConstantRange and GCC's irange.
struct Bounds {
    unsigned bits{};
    std::uint64_t umin{};
    std::uint64_t umax{};
    std::int64_t smin{};
    std::int64_t smax{};
};

std::uint64_t unsigned_max(unsigned bits) {
    return bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1U;
}

std::int64_t signed_max(unsigned bits) {
    return static_cast<std::int64_t>(unsigned_max(bits - 1U));
}

std::int64_t signed_min(unsigned bits) { return -signed_max(bits) - 1; }

std::int64_t as_signed(std::uint64_t value, unsigned bits) {
    value &= unsigned_max(bits);
    if (bits < 64 && ((value >> (bits - 1U)) & 1U) != 0) {
        value |= ~unsigned_max(bits);
    }
    return static_cast<std::int64_t>(value);
}

std::uint64_t as_unsigned(std::int64_t value, unsigned bits) {
    return static_cast<std::uint64_t>(value) & unsigned_max(bits);
}

Bounds full_bounds(unsigned bits) {
    return {bits, 0, unsigned_max(bits), signed_min(bits), signed_max(bits)};
}

Bounds exact_bounds(std::uint64_t value, unsigned bits) {
    value &= unsigned_max(bits);
    const auto signed_value = as_signed(value, bits);
    return {bits, value, value, signed_value, signed_value};
}

bool exact(const Bounds& bounds) { return bounds.umin == bounds.umax; }

// Tightens each interval by what the other implies; false when no value
// remains.
bool normalize(Bounds& bounds) {
    const auto top = static_cast<std::uint64_t>(signed_max(bounds.bits));
    for (int round = 0; round < 2; ++round) {
        if (bounds.umin > bounds.umax || bounds.smin > bounds.smax) {
            return false;
        }
        if (bounds.umax <= top) {
            bounds.smin = std::max(bounds.smin,
                                   static_cast<std::int64_t>(bounds.umin));
            bounds.smax = std::min(bounds.smax,
                                   static_cast<std::int64_t>(bounds.umax));
        } else if (bounds.umin > top) {
            bounds.smin = std::max(bounds.smin,
                                   as_signed(bounds.umin, bounds.bits));
            bounds.smax = std::min(bounds.smax,
                                   as_signed(bounds.umax, bounds.bits));
        }
        if (bounds.smin > bounds.smax) return false;
        if (bounds.smin >= 0) {
            bounds.umin = std::max(bounds.umin,
                                   static_cast<std::uint64_t>(bounds.smin));
            bounds.umax = std::min(bounds.umax,
                                   static_cast<std::uint64_t>(bounds.smax));
        } else if (bounds.smax < 0) {
            bounds.umin = std::max(bounds.umin,
                                   as_unsigned(bounds.smin, bounds.bits));
            bounds.umax = std::min(bounds.umax,
                                   as_unsigned(bounds.smax, bounds.bits));
        }
    }
    return bounds.umin <= bounds.umax && bounds.smin <= bounds.smax;
}

Bounds unsigned_bounds(unsigned bits, std::uint64_t low, std::uint64_t high) {
    auto result = full_bounds(bits);
    result.umin = low;
    result.umax = high;
    return normalize(result) ? result : full_bounds(bits);
}

Bounds signed_bounds(unsigned bits, std::int64_t low, std::int64_t high) {
    auto result = full_bounds(bits);
    result.smin = low;
    result.smax = high;
    return normalize(result) ? result : full_bounds(bits);
}

Bounds join(const Bounds& left, const Bounds& right) {
    return {left.bits, std::min(left.umin, right.umin),
            std::max(left.umax, right.umax), std::min(left.smin, right.smin),
            std::max(left.smax, right.smax)};
}

std::optional<Bounds> meet(Bounds left, const Bounds& right) {
    left.umin = std::max(left.umin, right.umin);
    left.umax = std::min(left.umax, right.umax);
    left.smin = std::max(left.smin, right.smin);
    left.smax = std::min(left.smax, right.smax);
    if (!normalize(left)) return std::nullopt;
    return left;
}

// The bounds without VALUE where it is one of their ends.
std::optional<Bounds> exclude(Bounds bounds, std::uint64_t value) {
    value &= unsigned_max(bounds.bits);
    const auto signed_value = as_signed(value, bounds.bits);
    if (bounds.umin == value) {
        if (bounds.umax == value) return std::nullopt;
        ++bounds.umin;
    } else if (bounds.umax == value) {
        --bounds.umax;
    }
    if (bounds.smin == signed_value) {
        if (bounds.smax == signed_value) return std::nullopt;
        ++bounds.smin;
    } else if (bounds.smax == signed_value) {
        --bounds.smax;
    }
    if (!normalize(bounds)) return std::nullopt;
    return bounds;
}

// All ones up to the highest one of VALUE.
std::uint64_t smear(std::uint64_t value) {
    for (unsigned shift = 1; shift < 64; shift <<= 1U) value |= value >> shift;
    return value;
}

std::optional<std::int64_t> checked_add(std::int64_t left, std::int64_t right,
                                        unsigned bits) {
    if (right > 0 ? left > signed_max(bits) - right
                  : left < signed_min(bits) - right) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> checked_subtract(std::int64_t left,
                                             std::int64_t right,
                                             unsigned bits) {
    if (right < 0 ? left > signed_max(bits) + right
                  : left < signed_min(bits) + right) {
        return std::nullopt;
    }
    return left - right;
}

Bounds add_bounds(const Bounds& left, const Bounds& right, unsigned bits) {
    auto result = full_bounds(bits);
    const auto top = unsigned_max(bits);
    if (left.umax <= top - right.umax) {
        result.umin = left.umin + right.umin;
        result.umax = left.umax + right.umax;
    }
    const auto low = checked_add(left.smin, right.smin, bits);
    const auto high = checked_add(left.smax, right.smax, bits);
    if (low && high) {
        result.smin = *low;
        result.smax = *high;
    }
    return normalize(result) ? result : full_bounds(bits);
}

Bounds subtract_bounds(const Bounds& left, const Bounds& right,
                       unsigned bits) {
    auto result = full_bounds(bits);
    if (left.umin >= right.umax) {
        result.umin = left.umin - right.umax;
        result.umax = left.umax - right.umin;
    }
    const auto low = checked_subtract(left.smin, right.smax, bits);
    const auto high = checked_subtract(left.smax, right.smin, bits);
    if (low && high) {
        result.smin = *low;
        result.smax = *high;
    }
    return normalize(result) ? result : full_bounds(bits);
}

Bounds multiply_bounds(const Bounds& left, const Bounds& right,
                       unsigned bits) {
    const auto top = unsigned_max(bits);
    if (left.umax != 0 && right.umax > top / left.umax) {
        return full_bounds(bits);
    }
    return unsigned_bounds(bits, left.umin * right.umin,
                           left.umax * right.umax);
}

bool comparison(BinaryOperation operation) {
    return operation >= BinaryOperation::Equal &&
           operation <= BinaryOperation::UnsignedGreaterEqual;
}

BinaryOperation negated(BinaryOperation operation) {
    using B = BinaryOperation;
    switch (operation) {
    case B::Equal: return B::NotEqual;
    case B::NotEqual: return B::Equal;
    case B::SignedLess: return B::SignedGreaterEqual;
    case B::SignedLessEqual: return B::SignedGreater;
    case B::SignedGreater: return B::SignedLessEqual;
    case B::SignedGreaterEqual: return B::SignedLess;
    case B::UnsignedLess: return B::UnsignedGreaterEqual;
    case B::UnsignedLessEqual: return B::UnsignedGreater;
    case B::UnsignedGreater: return B::UnsignedLessEqual;
    case B::UnsignedGreaterEqual: return B::UnsignedLess;
    default: return operation;
    }
}

// The comparison with its operands exchanged.
BinaryOperation swapped(BinaryOperation operation) {
    using B = BinaryOperation;
    switch (operation) {
    case B::SignedLess: return B::SignedGreater;
    case B::SignedLessEqual: return B::SignedGreaterEqual;
    case B::SignedGreater: return B::SignedLess;
    case B::SignedGreaterEqual: return B::SignedLessEqual;
    case B::UnsignedLess: return B::UnsignedGreater;
    case B::UnsignedLessEqual: return B::UnsignedGreaterEqual;
    case B::UnsignedGreater: return B::UnsignedLess;
    case B::UnsignedGreaterEqual: return B::UnsignedLessEqual;
    default: return operation;
    }
}

bool implies(BinaryOperation known, BinaryOperation query) {
    using B = BinaryOperation;
    if (known == query) return true;
    switch (known) {
    case B::Equal:
        return query == B::SignedLessEqual || query == B::SignedGreaterEqual ||
               query == B::UnsignedLessEqual ||
               query == B::UnsignedGreaterEqual;
    case B::SignedLess:
        return query == B::SignedLessEqual || query == B::NotEqual;
    case B::SignedGreater:
        return query == B::SignedGreaterEqual || query == B::NotEqual;
    case B::UnsignedLess:
        return query == B::UnsignedLessEqual || query == B::NotEqual;
    case B::UnsignedGreater:
        return query == B::UnsignedGreaterEqual || query == B::NotEqual;
    default: return false;
    }
}

// Whether `left OPERATION right` holds for all, or for no, values in the
// bounds.
std::optional<bool> compare(BinaryOperation operation, const Bounds& left,
                            const Bounds& right) {
    using B = BinaryOperation;
    switch (operation) {
    case B::UnsignedLess:
        if (left.umax < right.umin) return true;
        if (left.umin >= right.umax) return false;
        return std::nullopt;
    case B::UnsignedLessEqual:
        if (left.umax <= right.umin) return true;
        if (left.umin > right.umax) return false;
        return std::nullopt;
    case B::SignedLess:
        if (left.smax < right.smin) return true;
        if (left.smin >= right.smax) return false;
        return std::nullopt;
    case B::SignedLessEqual:
        if (left.smax <= right.smin) return true;
        if (left.smin > right.smax) return false;
        return std::nullopt;
    case B::UnsignedGreater:
    case B::UnsignedGreaterEqual:
    case B::SignedGreater:
    case B::SignedGreaterEqual:
        return compare(swapped(operation), right, left);
    case B::Equal:
        if (exact(left) && exact(right) && left.umin == right.umin) {
            return true;
        }
        if (left.umax < right.umin || right.umax < left.umin ||
            left.smax < right.smin || right.smax < left.smin) {
            return false;
        }
        return std::nullopt;
    case B::NotEqual:
        if (const auto equal = compare(B::Equal, left, right)) return !*equal;
        return std::nullopt;
    default: return std::nullopt;
    }
}

// The bounds of integer values, at a point that facts about branch outcomes
// and the incoming values of phis on one path describe.
class RangeContext {
public:
    RangeContext(const ManagedFunction& function, const hir::Module& module)
        : function_(function), module_(module),
          bool_type_(module.builtin(BuiltinType::Bool)) {}

    std::optional<Bounds> bounds(ValueId id) { return evaluate(id, 0); }

    std::optional<bool> decide(ValueId condition) {
        if (infeasible_) return std::nullopt;
        return decide_at(condition, 0);
    }

    // CONDITION is TRUTH from here on.
    void assume(ValueId condition, bool truth) {
        memo_.clear();
        assume_at(condition, truth, 0);
    }

    // The path enters PHI's block from the predecessor that passes INCOMING.
    void choose(ValueId phi, ValueId incoming) {
        memo_.clear();
        chosen_[phi.value] = incoming;
    }

    // Whether the facts contradict each other: the path cannot run.
    [[nodiscard]] bool infeasible() const { return infeasible_; }

private:
    static constexpr unsigned depth_limit = 16;

    struct Relation {
        ValueId left;
        BinaryOperation operation;
        ValueId right;
    };

    unsigned integer_bits(hir::TypeId id) const {
        const auto& type = module_.type(id);
        if (type.kind != hir::Type::Kind::Builtin) return 0;
        switch (type.builtin) {
        case BuiltinType::Bool:
        case BuiltinType::I8:
        case BuiltinType::U8: return 8;
        case BuiltinType::I16:
        case BuiltinType::U16: return 16;
        case BuiltinType::I32:
        case BuiltinType::U32: return 32;
        case BuiltinType::I64:
        case BuiltinType::U64: return 64;
        case BuiltinType::Iptr:
        case BuiltinType::Uptr:
            return module_.address_bits <= 64 ? module_.address_bits : 0;
        default: return 0;
        }
    }

    Bounds type_bounds(hir::TypeId type, unsigned bits) const {
        return bool_type_ && type == *bool_type_ ? unsigned_bounds(bits, 0, 1)
                                                 : full_bounds(bits);
    }

    std::optional<Bounds> evaluate(ValueId id, unsigned depth) {
        if (const auto found = chosen_.find(id.value); found != chosen_.end()) {
            if (depth >= depth_limit) return std::nullopt;
            auto result = evaluate(found->second, depth + 1);
            if (!result) return std::nullopt;
            if (const auto known = known_.find(id.value);
                known != known_.end()) {
                if (const auto narrowed = meet(*result, known->second)) {
                    return narrowed;
                }
                infeasible_ = true;
            }
            return result;
        }
        const auto& value = function_.values[id.value];
        const auto bits = integer_bits(value.type);
        if (bits == 0) return std::nullopt;
        if (const auto found = memo_.find(id.value); found != memo_.end()) {
            return found->second;
        }
        auto result = depth < depth_limit ? compute(id, bits, depth)
                                          : type_bounds(value.type, bits);
        if (const auto narrowed = meet(result, type_bounds(value.type, bits))) {
            result = *narrowed;
        }
        if (const auto known = known_.find(id.value); known != known_.end()) {
            if (const auto narrowed = meet(result, known->second)) {
                result = *narrowed;
            } else {
                infeasible_ = true;
                result = known->second;
            }
        }
        memo_[id.value] = result;
        return result;
    }

    Bounds operand_bounds(ValueId id, unsigned depth) {
        if (auto result = evaluate(id, depth + 1)) return *result;
        const auto bits = integer_bits(function_.values[id.value].type);
        return full_bounds(bits == 0 ? 64U : bits);
    }

    Bounds compute(ValueId id, unsigned bits, unsigned depth) {
        const auto& value = function_.values[id.value];
        const auto full = full_bounds(bits);
        const auto operand = [&](std::size_t index) {
            return operand_bounds(value.operands[index], depth);
        };
        switch (value.kind) {
        case ValueKind::ConstantInteger: return exact_bounds(value.integer, bits);
        case ValueKind::Cast: {
            if (value.operands.size() != 1) return full;
            const auto source_bits = integer_bits(
                function_.values[value.operands[0].value].type);
            if (source_bits == 0) return full;
            const auto source = operand(0);
            switch (value.cast) {
            case CastOperation::ZeroExtend:
                return source_bits <= bits
                    ? unsigned_bounds(bits, source.umin, source.umax) : full;
            case CastOperation::SignExtend:
                return source_bits <= bits
                    ? signed_bounds(bits, source.smin, source.smax) : full;
            case CastOperation::Truncate:
                if (source.umax <= unsigned_max(bits)) {
                    return unsigned_bounds(bits, source.umin, source.umax);
                }
                if (source.smin >= signed_min(bits) &&
                    source.smax <= signed_max(bits)) {
                    return signed_bounds(bits, source.smin, source.smax);
                }
                return full;
            case CastOperation::Reinterpret:
                if (source_bits != bits) return full;
                return source;
            default: return full;
            }
        }
        case ValueKind::Unary: {
            if (value.operands.size() != 1) return full;
            if (value.unary == UnaryOperation::IsZero) {
                const auto source = operand(0);
                if (source.umin > 0) return exact_bounds(0, bits);
                if (source.umax == 0) return exact_bounds(1, bits);
                return unsigned_bounds(bits, 0, 1);
            }
            const auto source = operand(0);
            if (source.bits != bits) return full;
            if (value.unary == UnaryOperation::BitNot) {
                auto result = full;
                result.umin = ~source.umax & unsigned_max(bits);
                result.umax = ~source.umin & unsigned_max(bits);
                result.smin = ~source.smax;
                result.smax = ~source.smin;
                return normalize(result) ? result : full;
            }
            if (source.smin == signed_min(bits)) return full;
            return signed_bounds(bits, -source.smax, -source.smin);
        }
        case ValueKind::Binary: {
            if (value.operands.size() != 2) return full;
            if (comparison(value.binary)) {
                if (const auto truth = decide_at(id, depth + 1)) {
                    return exact_bounds(*truth ? 1U : 0U, bits);
                }
                return unsigned_bounds(bits, 0, 1);
            }
            const auto left = operand(0);
            const auto right = operand(1);
            if (left.bits != bits) return full;
            switch (value.binary) {
            case BinaryOperation::Add:
                return right.bits == bits ? add_bounds(left, right, bits) : full;
            case BinaryOperation::Subtract:
                return right.bits == bits ? subtract_bounds(left, right, bits)
                                          : full;
            case BinaryOperation::Multiply:
                return right.bits == bits ? multiply_bounds(left, right, bits)
                                          : full;
            case BinaryOperation::UnsignedDivide:
                // Division by zero is undefined: no value constrains it.
                return unsigned_bounds(
                    bits, right.umax == 0 ? 0 : left.umin / right.umax,
                    left.umax / std::max<std::uint64_t>(right.umin, 1));
            case BinaryOperation::UnsignedRemainder:
                if (right.umax == 0) return full;
                if (left.umax < right.umin) return left;
                return unsigned_bounds(bits, 0,
                                       std::min(left.umax, right.umax - 1U));
            case BinaryOperation::SignedDivide:
                if (left.smin >= 0 && right.smin > 0) {
                    return unsigned_bounds(
                        bits, left.umin / right.umax,
                        left.umax / right.umin);
                }
                return full;
            case BinaryOperation::SignedRemainder:
                if (left.smin >= 0 && right.smin > 0) {
                    return unsigned_bounds(
                        bits, 0, std::min(left.umax, right.umax - 1U));
                }
                return full;
            case BinaryOperation::BitAnd:
                return unsigned_bounds(bits, 0, std::min(left.umax, right.umax));
            case BinaryOperation::BitOr:
                return unsigned_bounds(bits, std::max(left.umin, right.umin),
                                       smear(left.umax | right.umax));
            case BinaryOperation::BitXor:
                return unsigned_bounds(bits, 0, smear(left.umax | right.umax));
            case BinaryOperation::ShiftLeft:
                if (right.umax >= bits) return full;
                if (left.umax > (unsigned_max(bits) >> right.umax)) return full;
                return unsigned_bounds(bits, left.umin << right.umin,
                                       left.umax << right.umax);
            case BinaryOperation::ShiftRightLogical:
                if (right.umax >= bits) return full;
                return unsigned_bounds(bits, left.umin >> right.umax,
                                       left.umax >> right.umin);
            case BinaryOperation::ShiftRightArithmetic:
                if (right.umax >= bits) return full;
                return signed_bounds(
                    bits,
                    std::min(left.smin >> right.umin, left.smin >> right.umax),
                    std::max(left.smax >> right.umin, left.smax >> right.umax));
            default: return full;
            }
        }
        case ValueKind::Select: {
            if (value.operands.size() != 3) return full;
            if (const auto truth = decide_at(value.operands[0], depth + 1)) {
                return operand(*truth ? 1 : 2);
            }
            const auto left = operand(1);
            const auto right = operand(2);
            return left.bits == bits && right.bits == bits ? join(left, right)
                                                           : full;
        }
        case ValueKind::Phi: {
            if (!active_.insert(id.value).second) return full;
            std::optional<Bounds> result;
            for (const auto& incoming : value.incoming) {
                const auto next = operand_bounds(incoming.value, depth);
                if (next.bits != bits) {
                    result = full;
                    break;
                }
                result = result ? join(*result, next) : next;
            }
            active_.erase(id.value);
            return result.value_or(full);
        }
        case ValueKind::Intrinsic:
            if (value.operands.size() == 1 &&
                (value.intrinsic == IntrinsicOperation::Clz ||
                 value.intrinsic == IntrinsicOperation::Ctz ||
                 value.intrinsic == IntrinsicOperation::Popcount)) {
                const auto source_bits = integer_bits(
                    function_.values[value.operands[0].value].type);
                return source_bits == 0 ? full
                                        : unsigned_bounds(bits, 0, source_bits);
            }
            if (value.intrinsic == IntrinsicOperation::Expect &&
                !value.operands.empty()) {
                const auto source = operand(0);
                return source.bits == bits ? source : full;
            }
            return full;
        default: return full;
        }
    }

    bool integer_comparison(const ManagedValue& value) const {
        return value.kind == ValueKind::Binary && comparison(value.binary) &&
               value.operands.size() == 2 &&
               integer_bits(function_.values[value.operands[0].value].type) !=
                   0 &&
               integer_bits(function_.values[value.operands[1].value].type) !=
                   0;
    }

    bool boolean(const ManagedValue& value) const {
        return bool_type_ && value.type == *bool_type_;
    }

    std::optional<bool> related(BinaryOperation operation, ValueId left,
                                ValueId right) const {
        for (const auto& relation : relations_) {
            auto query = operation;
            if (relation.left == right && relation.right == left) {
                query = swapped(operation);
            } else if (relation.left != left || relation.right != right) {
                continue;
            }
            if (implies(relation.operation, query)) return true;
            if (implies(relation.operation, negated(query))) return false;
        }
        return std::nullopt;
    }

    std::optional<bool> decide_at(ValueId id, unsigned depth) {
        if (depth > depth_limit) return std::nullopt;
        if (const auto found = chosen_.find(id.value); found != chosen_.end()) {
            return decide_at(found->second, depth + 1);
        }
        const auto& value = function_.values[id.value];
        if (value.kind == ValueKind::ConstantInteger) {
            return (value.integer | value.integer_high) != 0;
        }
        if (integer_comparison(value)) {
            if (const auto truth =
                    related(value.binary, value.operands[0], value.operands[1])) {
                return truth;
            }
            const auto left = evaluate(value.operands[0], depth + 1);
            const auto right = evaluate(value.operands[1], depth + 1);
            if (!left || !right || left->bits != right->bits) {
                return std::nullopt;
            }
            return compare(value.binary, *left, *right);
        }
        if (value.kind == ValueKind::Binary && boolean(value) &&
            value.operands.size() == 2 &&
            (value.binary == BinaryOperation::BitAnd ||
             value.binary == BinaryOperation::BitOr ||
             value.binary == BinaryOperation::BitXor)) {
            const auto left = decide_at(value.operands[0], depth + 1);
            const auto right = decide_at(value.operands[1], depth + 1);
            switch (value.binary) {
            case BinaryOperation::BitAnd:
                if ((left && !*left) || (right && !*right)) return false;
                if (left && right) return true;
                return std::nullopt;
            case BinaryOperation::BitOr:
                if ((left && *left) || (right && *right)) return true;
                if (left && right) return false;
                return std::nullopt;
            default:
                if (left && right) return *left != *right;
                return std::nullopt;
            }
        }
        const auto bounds = evaluate(id, depth + 1);
        if (!bounds) return std::nullopt;
        if (bounds->umin > 0) return true;
        if (bounds->umax == 0) return false;
        return std::nullopt;
    }

    // The value ID holds a value within NARROWED from here on; so do the
    // values it reinterprets, extends, or offsets by a constant.
    void restrict(ValueId id, const Bounds& narrowed, unsigned depth) {
        if (const auto found = chosen_.find(id.value); found != chosen_.end()) {
            const auto current = evaluate(id, 0);
            if (!current || current->bits != narrowed.bits) return;
            const auto met = meet(*current, narrowed);
            if (!met) {
                infeasible_ = true;
                return;
            }
            known_[id.value] = *met;
            memo_.clear();
            if (depth < 4) restrict(found->second, *met, depth + 1);
            return;
        }
        const auto current = evaluate(id, 0);
        if (!current || current->bits != narrowed.bits) return;
        const auto met = meet(*current, narrowed);
        if (!met) {
            infeasible_ = true;
            return;
        }
        const auto& value = function_.values[id.value];
        if (value.kind == ValueKind::ConstantInteger) return;
        known_[id.value] = *met;
        memo_.clear();
        if (depth >= 4 || value.operands.empty()) return;
        const auto source = value.operands[0];
        const auto source_bits =
            integer_bits(function_.values[source.value].type);
        if (source_bits == 0) return;
        if (value.kind == ValueKind::Cast && value.operands.size() == 1) {
            if (value.cast == CastOperation::ZeroExtend &&
                source_bits <= met->bits) {
                if (met->umin > unsigned_max(source_bits)) {
                    infeasible_ = true;
                    return;
                }
                restrict(source,
                         unsigned_bounds(source_bits, met->umin,
                                         std::min(met->umax,
                                                  unsigned_max(source_bits))),
                         depth + 1);
            } else if (value.cast == CastOperation::SignExtend &&
                       source_bits <= met->bits) {
                const auto low = std::max(met->smin, signed_min(source_bits));
                const auto high = std::min(met->smax, signed_max(source_bits));
                if (low > high) {
                    infeasible_ = true;
                    return;
                }
                restrict(source, signed_bounds(source_bits, low, high),
                         depth + 1);
            } else if (value.cast == CastOperation::Reinterpret &&
                       source_bits == met->bits) {
                restrict(source, *met, depth + 1);
            }
            return;
        }
        if (value.kind != ValueKind::Binary || value.operands.size() != 2 ||
            (value.binary != BinaryOperation::Add &&
             value.binary != BinaryOperation::Subtract) ||
            source_bits != met->bits) {
            return;
        }
        // x + c or x - c, when no x in its bounds wraps.
        const auto& addend = function_.values[value.operands[1].value];
        if (addend.kind != ValueKind::ConstantInteger) return;
        const auto bits = met->bits;
        const auto constant = addend.integer & unsigned_max(bits);
        const auto base = evaluate(source, 0);
        if (!base) return;
        const bool add = value.binary == BinaryOperation::Add;
        auto result = full_bounds(bits);
        if (add ? base->umax <= unsigned_max(bits) - constant
                : base->umin >= constant) {
            const auto top = unsigned_max(bits);
            result.umin = add ? (met->umin > constant ? met->umin - constant : 0U)
                              : std::min(met->umin, top - constant) + constant;
            result.umax = add ? (met->umax > constant ? met->umax - constant : 0U)
                              : std::min(met->umax, top - constant) + constant;
        }
        const auto signed_constant = as_signed(constant, bits);
        const auto low = add ? checked_add(base->smin, signed_constant, bits)
                             : checked_subtract(base->smin, signed_constant, bits);
        const auto high = add ? checked_add(base->smax, signed_constant, bits)
                              : checked_subtract(base->smax, signed_constant, bits);
        if (low && high) {
            const auto from = add ? checked_subtract(met->smin, signed_constant, bits)
                                  : checked_add(met->smin, signed_constant, bits);
            const auto to = add ? checked_subtract(met->smax, signed_constant, bits)
                                : checked_add(met->smax, signed_constant, bits);
            if (from && to) {
                result.smin = *from;
                result.smax = *to;
            }
        }
        if (normalize(result)) restrict(source, result, depth + 1);
    }

    void refine(BinaryOperation operation, ValueId left_id, ValueId right_id) {
        using B = BinaryOperation;
        switch (operation) {
        case B::UnsignedGreater:
        case B::UnsignedGreaterEqual:
        case B::SignedGreater:
        case B::SignedGreaterEqual:
            refine(swapped(operation), right_id, left_id);
            return;
        default: break;
        }
        const auto left_bounds = evaluate(left_id, 0);
        const auto right_bounds = evaluate(right_id, 0);
        if (!left_bounds || !right_bounds ||
            left_bounds->bits != right_bounds->bits) {
            return;
        }
        auto left = *left_bounds;
        auto right = *right_bounds;
        const auto bits = left.bits;
        switch (operation) {
        case B::UnsignedLess:
            if (right.umax == 0 || left.umin == unsigned_max(bits)) {
                infeasible_ = true;
                return;
            }
            left.umax = std::min(left.umax, right.umax - 1U);
            right.umin = std::max(right.umin, left.umin + 1U);
            break;
        case B::UnsignedLessEqual:
            left.umax = std::min(left.umax, right.umax);
            right.umin = std::max(right.umin, left.umin);
            break;
        case B::SignedLess:
            if (right.smax == signed_min(bits) ||
                left.smin == signed_max(bits)) {
                infeasible_ = true;
                return;
            }
            left.smax = std::min(left.smax, right.smax - 1);
            right.smin = std::max(right.smin, left.smin + 1);
            break;
        case B::SignedLessEqual:
            left.smax = std::min(left.smax, right.smax);
            right.smin = std::max(right.smin, left.smin);
            break;
        case B::Equal: {
            const auto both = meet(left, right);
            if (!both) {
                infeasible_ = true;
                return;
            }
            left = right = *both;
            break;
        }
        case B::NotEqual: {
            if (exact(right)) {
                const auto narrowed = exclude(left, right.umin);
                if (!narrowed) {
                    infeasible_ = true;
                    return;
                }
                left = *narrowed;
            }
            if (exact(*left_bounds)) {
                const auto narrowed = exclude(right, left_bounds->umin);
                if (!narrowed) {
                    infeasible_ = true;
                    return;
                }
                right = *narrowed;
            }
            break;
        }
        default: return;
        }
        if (!normalize(left) || !normalize(right)) {
            infeasible_ = true;
            return;
        }
        restrict(left_id, left, 0);
        restrict(right_id, right, 0);
    }

    void assume_at(ValueId id, bool truth, unsigned depth) {
        if (depth > 4) return;
        if (const auto found = chosen_.find(id.value); found != chosen_.end()) {
            assume_at(found->second, truth, depth + 1);
            return;
        }
        const auto& value = function_.values[id.value];
        if (value.kind == ValueKind::ConstantInteger) {
            if (((value.integer | value.integer_high) != 0) != truth) {
                infeasible_ = true;
            }
            return;
        }
        if (integer_comparison(value)) {
            const auto operation = truth ? value.binary : negated(value.binary);
            refine(operation, value.operands[0], value.operands[1]);
            relations_.push_back(
                {value.operands[0], operation, value.operands[1]});
            return;
        }
        if (value.kind == ValueKind::Binary && boolean(value) &&
            value.operands.size() == 2) {
            if ((value.binary == BinaryOperation::BitAnd && truth) ||
                (value.binary == BinaryOperation::BitOr && !truth)) {
                assume_at(value.operands[0], truth, depth + 1);
                assume_at(value.operands[1], truth, depth + 1);
                return;
            }
        }
        if (value.kind == ValueKind::Unary &&
            value.unary == UnaryOperation::IsZero && value.operands.size() == 1) {
            restrict_zero(value.operands[0], truth);
            return;
        }
        if (value.kind == ValueKind::Cast && value.operands.size() == 1 &&
            (value.cast == CastOperation::ZeroExtend ||
             value.cast == CastOperation::SignExtend ||
             value.cast == CastOperation::Reinterpret)) {
            assume_at(value.operands[0], truth, depth + 1);
        }
        restrict_zero(id, !truth);
    }

    // Value ID is zero, or nonzero.
    void restrict_zero(ValueId id, bool zero) {
        const auto current = evaluate(id, 0);
        if (!current) return;
        if (zero) {
            restrict(id, exact_bounds(0, current->bits), 0);
            return;
        }
        if (const auto narrowed = exclude(*current, 0)) {
            restrict(id, *narrowed, 0);
        } else {
            infeasible_ = true;
        }
    }

    const ManagedFunction& function_;
    const hir::Module& module_;
    std::optional<hir::TypeId> bool_type_;
    std::unordered_map<std::uint32_t, Bounds> known_;
    std::unordered_map<std::uint32_t, ValueId> chosen_;
    std::vector<Relation> relations_;
    std::unordered_map<std::uint32_t, Bounds> memo_;
    std::unordered_set<std::uint32_t> active_;
    bool infeasible_{};
};

// The outcome of the conditional branch that ends PREDECESSOR on its edge to
// SUCCESSOR, when the edge is the branch's only one to SUCCESSOR.
std::optional<std::pair<ValueId, bool>> edge_fact(
    const ManagedFunction& function, BlockId predecessor, BlockId successor) {
    const auto& terminator = function.blocks[predecessor.value].terminator;
    if (terminator.kind != TerminatorKind::ConditionalBranch ||
        !terminator.value || terminator.successors.size() != 2 ||
        terminator.successors[0] == terminator.successors[1]) {
        return std::nullopt;
    }
    if (terminator.successors[0] == successor) {
        return std::pair{*terminator.value, true};
    }
    if (terminator.successors[1] == successor) {
        return std::pair{*terminator.value, false};
    }
    return std::nullopt;
}

// The facts of the branch edges that every path to BLOCK takes: the edge
// into each dominator, BLOCK included, that is its only predecessor's. The
// nearest eight count, so that a chain of tests costs linear time.
void assume_dominating_edges(RangeContext& context,
                             const ManagedFunction& function,
                             const DominatorTree& dominators, BlockId block) {
    std::vector<std::pair<ValueId, bool>> facts;
    for (std::optional<BlockId> at = block; at && facts.size() < 8;
         at = dominators.immediate_dominator(*at)) {
        const auto& predecessors = function.blocks[at->value].predecessors;
        if (predecessors.size() != 1) continue;
        if (const auto fact = edge_fact(function, predecessors.front(), *at)) {
            facts.push_back(*fact);
        }
    }
    for (auto fact = facts.rbegin(); fact != facts.rend(); ++fact) {
        context.assume(fact->first, fact->second);
    }
}

// A jump-threading path: the edge from SOURCE's successor SUCCESSOR runs
// BLOCKS, whose branches the path decides, and continues at TARGET.
struct ThreadPath {
    BlockId source;
    std::size_t successor{};
    std::vector<BlockId> blocks;
    BlockId target;
};

class JumpThreader {
public:
    JumpThreader(ManagedFunction& function, const hir::Module& module,
                 unsigned limit)
        : function_(function), module_(module), limit_(limit) {}

    bool run() {
        bool changed = false;
        // Each thread removes a decided branch from one path; the bound
        // keeps a function from growing without end.
        for (unsigned threads = 0; threads <= 32; ++threads) {
            const DominatorTree dominators(function_);
            if (fold_decided_branches(dominators)) changed = true;
            if (threads == 32) break;
            const LoopForest loops(function_, dominators);
            const UseLists uses(function_);
            const auto path = find(dominators, loops, uses);
            if (!path) break;
            apply(*path, uses);
            prune_unreachable_blocks(function_);
            changed = true;
        }
        return changed;
    }

private:
    // Replaces a condition that the facts of the edges dominating its
    // branch decide with a constant, which branch folding then removes.
    bool fold_decided_branches(const DominatorTree& dominators) {
        bool changed = false;
        const auto bool_type = module_.builtin(BuiltinType::Bool);
        if (!bool_type) return false;
        for (std::size_t index = 0; index < function_.blocks.size(); ++index) {
            const BlockId block{static_cast<std::uint32_t>(index)};
            const auto& terminator = function_.blocks[index].terminator;
            if (!dominators.reachable(block) ||
                terminator.kind != TerminatorKind::ConditionalBranch ||
                !terminator.value || terminator.successors.size() != 2 ||
                terminator.successors[0] == terminator.successors[1] ||
                function_.values[terminator.value->value].kind ==
                    ValueKind::ConstantInteger) {
                continue;
            }
            RangeContext context(function_, module_);
            assume_dominating_edges(context, function_, dominators, block);
            const auto truth = context.decide(*terminator.value);
            if (!truth) continue;
            ManagedValue constant;
            constant.id = {static_cast<std::uint32_t>(function_.values.size())};
            constant.location = function_.values[terminator.value->value].location;
            constant.type = *bool_type;
            constant.kind = ValueKind::ConstantInteger;
            constant.integer = *truth ? 1U : 0U;
            const auto id = constant.id;
            function_.values.push_back(std::move(constant));
            function_.blocks[index].values.push_back(id);
            function_.blocks[index].terminator.value = id;
            changed = true;
        }
        return changed;
    }

    bool labeled(BlockId block) const {
        return std::any_of(function_.labels.begin(), function_.labels.end(),
                           [&](const ManagedLabel& label) {
                               return label.block == block;
                           });
    }

    // Whether the block's values may be copied onto a path: no call,
    // atomic, volatile access, stack or lifetime operation, machine
    // instruction, or other value with an identity of its own, and no
    // effect from another block.
    bool duplicable(const ManagedBlock& block) const {
        std::unordered_set<std::uint32_t> effects{block.effect.value};
        for (const auto id : block.values) {
            const auto& value = function_.values[id.value];
            if (value.is_volatile_access || value.patch_sink ||
                !value.call_arguments.empty()) {
                return false;
            }
            switch (value.kind) {
            case ValueKind::Phi:
            case ValueKind::ConstantInteger:
            case ValueKind::ConstantFloating:
            case ValueKind::FunctionAddress:
            case ValueKind::SlotAddress:
            case ValueKind::GlobalAddress:
            case ValueKind::IndexedAddress:
            case ValueKind::Unary:
            case ValueKind::Binary:
            case ValueKind::Cast:
            case ValueKind::Select:
            case ValueKind::Splat:
            case ValueKind::ExtractElement:
            case ValueKind::InsertElement:
            case ValueKind::Load:
            case ValueKind::Store:
            case ValueKind::PointerLoad:
            case ValueKind::PointerStore:
            case ValueKind::IndexedLoad:
            case ValueKind::GlobalLoad:
            case ValueKind::GlobalStore:
                break;
            case ValueKind::Intrinsic:
                if (value.intrinsic == IntrinsicOperation::Expect ||
                    floating_intrinsic_operation(value.intrinsic) ||
                    bit_intrinsic_operation(value.intrinsic)) {
                    break;
                }
                return false;
            default: return false;
            }
            if (value.effect_input && !effects.contains(value.effect_input->value)) {
                return false;
            }
            if (value.effect_output) effects.insert(value.effect_output->value);
        }
        return effects.contains(block.terminator.effect.value);
    }

    // Whether BLOCK heads a natural loop that contains MEMBER.
    static bool loop_contains(const LoopForest& loops, BlockId header,
                              BlockId member) {
        return std::any_of(loops.loops().begin(), loops.loops().end(),
                           [&](const NaturalLoop& loop) {
                               return loop.header == header &&
                                      loop.blocks.contains(member.value);
                           });
    }

    static bool header(const LoopForest& loops, BlockId block) {
        return std::any_of(loops.loops().begin(), loops.loops().end(),
                           [&](const NaturalLoop& loop) {
                               return loop.header == block;
                           });
    }

    static bool same_loops(const LoopForest& loops, BlockId left,
                           BlockId right) {
        return std::all_of(loops.loops().begin(), loops.loops().end(),
                           [&](const NaturalLoop& loop) {
                               return loop.blocks.contains(left.value) ==
                                      loop.blocks.contains(right.value);
                           });
    }

    struct KeptValues {
        std::unordered_set<std::uint32_t> values;
        // The values read after the path, which the original blocks and the
        // copies then pass in new phis.
        unsigned merged{};
    };

    // The operations that copies of the path's blocks keep: stores and the
    // operations whose values a later block, a phi of the target, or another
    // kept operation reads. The conditions of the decided branches, and what
    // only they read, are not copied.
    KeptValues kept_values(const ThreadPath& path, const UseLists& uses) const {
        std::unordered_set<std::uint32_t> blocks;
        for (const auto block : path.blocks) blocks.insert(block.value);
        const auto on_path = [&](ValueId id) {
            const auto definition = uses.definition_block(id);
            return definition && blocks.contains(definition->value);
        };
        // The value each phi of a path block takes on the path's edge.
        std::unordered_map<std::uint32_t, ValueId> chosen;
        for (std::size_t index = 0; index < path.blocks.size(); ++index) {
            const auto previous =
                index == 0 ? path.source : path.blocks[index - 1];
            for (const auto id :
                 function_.blocks[path.blocks[index].value].values) {
                for (const auto& incoming : function_.values[id.value].incoming) {
                    if (incoming.predecessor == previous) {
                        chosen.emplace(id.value, incoming.value);
                    }
                }
            }
        }
        std::unordered_set<std::uint32_t> kept;
        std::vector<ValueId> pending;
        const auto keep = [&](ValueId id) {
            if (on_path(id) && kept.insert(id.value).second) {
                pending.push_back(id);
            }
        };
        unsigned merged{};
        for (const auto block : path.blocks) {
            for (const auto id : function_.blocks[block.value].values) {
                if (function_.values[id.value].effect_output) keep(id);
                const auto& value_uses = uses.uses(id);
                if (std::any_of(value_uses.begin(), value_uses.end(),
                                [&](const ValueUse& use) {
                                    return !blocks.contains(use.block.value);
                                })) {
                    keep(id);
                    ++merged;
                }
            }
        }
        // The target's phis take what they take from the last block from
        // the last copy as well.
        for (const auto id : function_.blocks[path.target.value].values) {
            const auto& value = function_.values[id.value];
            if (value.kind != ValueKind::Phi) continue;
            for (const auto& incoming : value.incoming) {
                if (incoming.predecessor == path.blocks.back()) {
                    keep(incoming.value);
                }
            }
        }
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            const auto& value = function_.values[id.value];
            if (value.kind == ValueKind::Phi) {
                if (const auto found = chosen.find(id.value);
                    found != chosen.end()) {
                    keep(found->second);
                }
                continue;
            }
            for (const auto operand : value.operands) keep(operand);
        }
        return {std::move(kept), merged};
    }

    // What the thread costs: the operations its copies keep, constants
    // aside, and the values read after the path.
    unsigned thread_cost(const ThreadPath& path, const UseLists& uses) const {
        const auto kept = kept_values(path, uses);
        unsigned count = kept.merged;
        for (const auto id : kept.values) {
            const auto kind = function_.values[id].kind;
            if (kind != ValueKind::Phi && kind != ValueKind::ConstantInteger &&
                kind != ValueKind::ConstantFloating) {
                ++count;
            }
        }
        return count;
    }

    // The longest path from an edge into a join that decides a branch,
    // keeps the loops it starts in, and copies at most the limit.
    std::optional<ThreadPath> explore(BlockId source, std::size_t successor,
                                      const DominatorTree& dominators,
                                      const LoopForest& loops,
                                      const UseLists& uses) const {
        RangeContext context(function_, module_);
        assume_dominating_edges(context, function_, dominators, source);
        const auto first = function_.blocks[source.value]
                               .terminator.successors[successor];
        if (const auto fact = edge_fact(function_, source, first)) {
            context.assume(fact->first, fact->second);
        }
        if (context.infeasible()) return std::nullopt;
        std::optional<ThreadPath> best;
        ThreadPath path{source, successor, {}, first};
        std::unordered_set<std::uint32_t> visited;
        auto previous = source;
        auto current = first;
        for (unsigned steps = 0; steps < 8; ++steps) {
            const auto& block = function_.blocks[current.value];
            if (current == function_.entry || current == source ||
                !visited.insert(current.value).second || labeled(current) ||
                dominators.dominates(current, previous) || !duplicable(block)) {
                break;
            }
            bool entered = true;
            for (const auto id : block.values) {
                const auto& value = function_.values[id.value];
                if (value.kind != ValueKind::Phi) continue;
                const auto incoming = std::find_if(
                    value.incoming.begin(), value.incoming.end(),
                    [&](const PhiIncoming& edge) {
                        return edge.predecessor == previous;
                    });
                if (incoming == value.incoming.end()) {
                    entered = false;
                    break;
                }
                context.choose(id, incoming->value);
            }
            if (!entered) break;
            path.blocks.push_back(current);
            const auto& terminator = block.terminator;
            std::optional<BlockId> next;
            bool decided = false;
            if (terminator.kind == TerminatorKind::Branch &&
                terminator.successors.size() == 1) {
                next = terminator.successors.front();
            } else if (terminator.kind == TerminatorKind::ConditionalBranch &&
                       terminator.value && terminator.successors.size() == 2 &&
                       terminator.successors[0] != terminator.successors[1]) {
                const auto truth = context.decide(*terminator.value);
                if (!truth || context.infeasible()) break;
                next = terminator.successors[*truth ? 0U : 1U];
                decided = true;
            } else {
                break;
            }
            // A copied loop header leaves its loop: entering the loop's body
            // from a copy would give the loop a second entry.
            if (header(loops, current) && loop_contains(loops, current, *next)) {
                break;
            }
            if (decided && !header(loops, *next) &&
                same_loops(loops, source, *next)) {
                path.target = *next;
                if (thread_cost(path, uses) <= limit_) best = path;
            }
            previous = current;
            current = *next;
        }
        return best;
    }

    std::optional<ThreadPath> find(const DominatorTree& dominators,
                                   const LoopForest& loops,
                                   const UseLists& uses) const {
        for (std::size_t index = 0; index < function_.blocks.size(); ++index) {
            const BlockId source{static_cast<std::uint32_t>(index)};
            const auto& terminator = function_.blocks[index].terminator;
            if (!dominators.reachable(source) ||
                (terminator.kind != TerminatorKind::Branch &&
                 terminator.kind != TerminatorKind::ConditionalBranch) ||
                (terminator.successors.size() == 2 &&
                 terminator.successors[0] == terminator.successors[1])) {
                continue;
            }
            for (std::size_t successor = 0;
                 successor < terminator.successors.size(); ++successor) {
                const auto first = terminator.successors[successor];
                if (function_.blocks[first.value].predecessors.size() < 2 ||
                    dominators.dominates(first, source)) {
                    continue;
                }
                if (auto path =
                        explore(source, successor, dominators, loops, uses)) {
                    return path;
                }
            }
        }
        return std::nullopt;
    }

    ValueId append_value(ManagedValue value) {
        value.id = {static_cast<std::uint32_t>(function_.values.size())};
        const auto id = value.id;
        function_.values.push_back(std::move(value));
        return id;
    }

    EffectId append_effect(ManagedEffect effect) {
        effect.id = {static_cast<std::uint32_t>(function_.effects.size())};
        const auto id = effect.id;
        function_.effects.push_back(std::move(effect));
        return id;
    }

    void apply(const ThreadPath& path, const UseLists& uses) {
        const auto kept = kept_values(path, uses).values;
        std::unordered_map<std::uint32_t, ValueId> copies;
        const auto copied = [&](ValueId id) {
            const auto found = copies.find(id.value);
            return found == copies.end() ? id : found->second;
        };
        std::vector<BlockId> clones;
        auto previous = path.source;
        auto previous_effect = function_.blocks[path.source.value].terminator.effect;
        for (std::size_t index = 0; index < path.blocks.size(); ++index) {
            const auto original = path.blocks[index];
            const auto from =
                index == 0 ? path.source : path.blocks[index - 1];
            ManagedBlock clone;
            clone.id = {static_cast<std::uint32_t>(function_.blocks.size())};
            clone.location = function_.blocks[original.value].location;
            clone.predecessors = {previous};
            ManagedEffect entry;
            entry.location = clone.location;
            entry.kind = EffectKind::Phi;
            entry.incoming = {{previous, previous_effect}};
            clone.effect = append_effect(std::move(entry));
            std::unordered_map<std::uint32_t, EffectId> effects{
                {function_.blocks[original.value].effect.value, clone.effect}};
            const auto values = function_.blocks[original.value].values;
            for (const auto id : values) {
                auto value = function_.values[id.value];
                if (value.kind == ValueKind::Phi) {
                    for (const auto& incoming : value.incoming) {
                        if (incoming.predecessor == from) {
                            copies[id.value] = copied(incoming.value);
                        }
                    }
                    continue;
                }
                if (!kept.contains(id.value)) continue;
                for (auto& operand : value.operands) operand = copied(operand);
                if (value.effect_input) {
                    value.effect_input = effects.at(value.effect_input->value);
                }
                const auto output = value.effect_output;
                const auto copy = append_value(std::move(value));
                if (output) {
                    ManagedEffect effect;
                    effect.location = function_.values[copy.value].location;
                    effect.kind = EffectKind::Operation;
                    effect.input = function_.values[copy.value].effect_input;
                    effect.operation = copy;
                    const auto produced = append_effect(std::move(effect));
                    function_.values[copy.value].effect_output = produced;
                    effects[output->value] = produced;
                }
                clone.values.push_back(copy);
                copies[id.value] = copy;
            }
            const auto& terminator = function_.blocks[original.value].terminator;
            clone.terminator.kind = TerminatorKind::Branch;
            clone.terminator.location = terminator.location;
            clone.terminator.inline_site = terminator.inline_site;
            clone.terminator.effect = effects.at(terminator.effect.value);
            previous = clone.id;
            previous_effect = clone.terminator.effect;
            clones.push_back(clone.id);
            function_.blocks.push_back(std::move(clone));
        }
        for (std::size_t index = 0; index + 1 < clones.size(); ++index) {
            function_.blocks[clones[index].value].terminator.successors = {
                clones[index + 1]};
        }
        function_.blocks[clones.back().value].terminator.successors = {
            path.target};

        // The source's edge enters the copies instead.
        const auto first = path.blocks.front();
        function_.blocks[path.source.value].terminator.successors[path.successor] =
            clones.front();
        auto& entered = function_.blocks[first.value];
        const auto at = std::find(entered.predecessors.begin(),
                                  entered.predecessors.end(), path.source);
        if (at != entered.predecessors.end()) entered.predecessors.erase(at);
        std::erase_if(function_.effects[entered.effect.value].incoming,
                      [&](const EffectIncoming& incoming) {
                          return incoming.predecessor == path.source;
                      });
        for (const auto id : entered.values) {
            auto& value = function_.values[id.value];
            if (value.kind != ValueKind::Phi) continue;
            std::erase_if(value.incoming, [&](const PhiIncoming& incoming) {
                return incoming.predecessor == path.source;
            });
        }

        // The target gains the last copy as a predecessor.
        const auto last = path.blocks.back();
        auto& target = function_.blocks[path.target.value];
        target.predecessors.push_back(clones.back());
        function_.effects[target.effect.value].incoming.push_back(
            {clones.back(), function_.blocks[clones.back().value].terminator.effect});
        for (const auto id : target.values) {
            auto& value = function_.values[id.value];
            if (value.kind != ValueKind::Phi) continue;
            const auto incoming = std::find_if(
                value.incoming.begin(), value.incoming.end(),
                [&](const PhiIncoming& edge) { return edge.predecessor == last; });
            if (incoming != value.incoming.end()) {
                value.incoming.push_back({clones.back(), copied(incoming->value)});
            }
        }

        // Values of the path read beyond it now arrive from the original
        // blocks or from their copies.
        std::unordered_set<std::uint32_t> path_blocks;
        for (const auto block : path.blocks) path_blocks.insert(block.value);
        for (const auto block : clones) path_blocks.insert(block.value);
        const auto reachable = reachable_blocks();
        for (std::size_t index = 0; index < path.blocks.size(); ++index) {
            const auto original = path.blocks[index];
            for (const auto id : function_.blocks[original.value].values) {
                const auto copy = copies.find(id.value);
                if (copy == copies.end()) continue;
                repair(id, original, copy->second, clones[index], path_blocks,
                       reachable);
            }
        }
    }

    std::vector<bool> reachable_blocks() const {
        std::vector<bool> reachable(function_.blocks.size());
        std::vector<BlockId> pending{function_.entry};
        while (!pending.empty()) {
            const auto block = pending.back();
            pending.pop_back();
            if (reachable[block.value]) continue;
            reachable[block.value] = true;
            for (const auto successor :
                 function_.blocks[block.value].terminator.successors) {
                pending.push_back(successor);
            }
        }
        return reachable;
    }

    // Rewrites the uses of ORIGINAL outside the path and its copies to the
    // value that reaches them, ORIGINAL from its block or COPY from the
    // copy of it, with phis where both arrive (an SSA updater as in LLVM's
    // SSAUpdater).
    void repair(ValueId original, BlockId block, ValueId copy, BlockId clone,
                const std::unordered_set<std::uint32_t>& path_blocks,
                const std::vector<bool>& reachable) {
        struct Use {
            BlockId block;
            std::optional<ValueId> user;
            UseKind kind;
            std::size_t index;
        };
        std::vector<Use> outside;
        for (const auto& candidate : function_.blocks) {
            if (path_blocks.contains(candidate.id.value)) continue;
            for (const auto id : candidate.values) {
                const auto& value = function_.values[id.value];
                for (std::size_t index = 0; index < value.operands.size(); ++index) {
                    if (value.operands[index] == original) {
                        outside.push_back({candidate.id, id, UseKind::Operand, index});
                    }
                }
                for (std::size_t index = 0; index < value.call_arguments.size();
                     ++index) {
                    if (value.call_arguments[index].value == original) {
                        outside.push_back(
                            {candidate.id, id, UseKind::CallArgument, index});
                    }
                }
                for (std::size_t index = 0; index < value.incoming.size(); ++index) {
                    if (value.incoming[index].value == original) {
                        outside.push_back(
                            {candidate.id, id, UseKind::PhiIncoming, index});
                    }
                }
            }
            if (candidate.terminator.value == original) {
                outside.push_back({candidate.id, std::nullopt, UseKind::Terminator, 0});
            }
        }
        if (outside.empty()) return;
        std::unordered_map<std::uint32_t, ValueId> at_start;
        const auto type = function_.values[original.value].type;
        const auto location = function_.values[original.value].location;
        std::function<ValueId(BlockId)> start;
        const auto end = [&](BlockId at) {
            if (at == block) return original;
            if (at == clone) return copy;
            return start(at);
        };
        start = [&](BlockId at) -> ValueId {
            if (const auto found = at_start.find(at.value); found != at_start.end()) {
                return found->second;
            }
            std::vector<BlockId> live;
            for (const auto predecessor : function_.blocks[at.value].predecessors) {
                if (reachable[predecessor.value]) live.push_back(predecessor);
            }
            // A cycle of reachable blocks has a block with several
            // predecessors, whose phi ends the walk; a placeholder here
            // would leak into the values a cycle computes.
            if (live.size() == 1) {
                const auto value = end(live.front());
                at_start[at.value] = value;
                return value;
            }
            if (live.empty()) return original;
            ManagedValue phi;
            phi.location = location;
            phi.type = type;
            phi.kind = ValueKind::Phi;
            const auto id = append_value(std::move(phi));
            auto& values = function_.blocks[at.value].values;
            values.insert(values.begin(), id);
            at_start[at.value] = id;
            std::vector<PhiIncoming> incoming;
            for (const auto predecessor : function_.blocks[at.value].predecessors) {
                incoming.push_back(
                    {predecessor,
                     reachable[predecessor.value] ? end(predecessor) : original});
            }
            function_.values[id.value].incoming = std::move(incoming);
            return id;
        };
        for (const auto& use : outside) {
            if (use.kind == UseKind::PhiIncoming) {
                const auto predecessor =
                    function_.values[use.user->value].incoming[use.index].predecessor;
                const auto value =
                    reachable[predecessor.value] ? end(predecessor) : original;
                function_.values[use.user->value].incoming[use.index].value = value;
                continue;
            }
            const auto value = start(use.block);
            switch (use.kind) {
            case UseKind::Operand:
                function_.values[use.user->value].operands[use.index] = value;
                break;
            case UseKind::CallArgument:
                function_.values[use.user->value].call_arguments[use.index].value =
                    value;
                break;
            case UseKind::Terminator:
                function_.blocks[use.block.value].terminator.value = value;
                break;
            case UseKind::PhiIncoming: break;
            }
        }
    }

    ManagedFunction& function_;
    const hir::Module& module_;
    unsigned limit_;
};

} // namespace

bool thread_decided_branches(ManagedFunction& function,
                             const hir::Module& hir_module, unsigned limit) {
    return JumpThreader(function, hir_module, limit).run();
}

} // namespace cross::mir
