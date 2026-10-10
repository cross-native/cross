// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "common/memory_order.hpp"

#include <optional>
#include <vector>

namespace cross {

inline bool patch_intrinsic(const Expr& expression) {
    return expression.kind == Expr::Kind::Call && expression.left &&
        expression.left->kind == Expr::Kind::Name && expression.left->text == "$::patch";
}

enum class StagingIntrinsic { None, Eval, Runtime };

inline StagingIntrinsic staging_intrinsic(const Expr& expression) {
    if (expression.kind != Expr::Kind::Call || !expression.left ||
        expression.left->kind != Expr::Kind::Name) return StagingIntrinsic::None;
    if (expression.left->text == "$::eval") return StagingIntrinsic::Eval;
    if (expression.left->text == "$::runtime") return StagingIntrinsic::Runtime;
    return StagingIntrinsic::None;
}

inline const char* staging_intrinsic_arity_error(const Expr& expression) {
    if (expression.arguments.size() == 1) return nullptr;
    switch (staging_intrinsic(expression)) {
    case StagingIntrinsic::Eval: return "$::eval requires exactly one expression";
    case StagingIntrinsic::Runtime: return "$::runtime requires exactly one expression";
    case StagingIntrinsic::None: return nullptr;
    }
    return nullptr;
}

enum class AtomicBuiltin {
    None, Load, Store, Exchange, CompareExchange,
    FetchAdd, FetchSub, FetchAnd, FetchXor, FetchOr,
    ThreadFence, SignalFence, IsLockFree,
};

inline AtomicBuiltin atomic_builtin(std::string_view name) {
    static constexpr std::pair<std::string_view, AtomicBuiltin> operations[] = {
        {"$::atomic_load", AtomicBuiltin::Load},
        {"$::atomic_store", AtomicBuiltin::Store},
        {"$::atomic_exchange", AtomicBuiltin::Exchange},
        {"$::atomic_compare_exchange", AtomicBuiltin::CompareExchange},
        {"$::atomic_fetch_add", AtomicBuiltin::FetchAdd},
        {"$::atomic_fetch_sub", AtomicBuiltin::FetchSub},
        {"$::atomic_fetch_and", AtomicBuiltin::FetchAnd},
        {"$::atomic_fetch_xor", AtomicBuiltin::FetchXor},
        {"$::atomic_fetch_or", AtomicBuiltin::FetchOr},
        {"$::atomic_thread_fence", AtomicBuiltin::ThreadFence},
        {"$::atomic_signal_fence", AtomicBuiltin::SignalFence},
        {"$::atomic_is_lock_free", AtomicBuiltin::IsLockFree},
    };
    for (const auto& [spelling, operation] : operations)
        if (name == spelling) return operation;
    return AtomicBuiltin::None;
}

inline AtomicBuiltin atomic_builtin(const Expr& expression) {
    return expression.kind == Expr::Kind::Call && expression.left &&
        expression.left->kind == Expr::Kind::Name
        ? atomic_builtin(expression.left->text) : AtomicBuiltin::None;
}

inline std::optional<MemoryOrder> parse_source_memory_order(const Expr& expression) {
    const Expr* source = &expression;
    while (source->kind == Expr::Kind::Parenthesized && source->left) source = source->left.get();
    if (source->kind != Expr::Kind::Name) return {};
    if (source->text == "$::memory::relaxed") return MemoryOrder::Relaxed;
    if (source->text == "$::memory::acquire") return MemoryOrder::Acquire;
    if (source->text == "$::memory::release") return MemoryOrder::Release;
    if (source->text == "$::memory::acq_rel") return MemoryOrder::AcqRel;
    if (source->text == "$::memory::seq_cst") return MemoryOrder::SeqCst;
    return {};
}

inline TypePtr atomic_value_type(const TypePtr& type) {
    if (!type) return {};
    auto value = std::make_shared<Type>(*type);
    value->is_const = value->is_volatile = value->is_atomic = value->is_restrict = false;
    return value;
}

enum class ControlIntrinsic { None, Expect, Assume, Unreachable, Trap };

inline ControlIntrinsic control_intrinsic(const Expr& expression) {
    if (expression.kind != Expr::Kind::Call || !expression.left ||
        expression.left->kind != Expr::Kind::Name) return ControlIntrinsic::None;
    const auto& name = expression.left->text;
    if (name == "$::expect") return ControlIntrinsic::Expect;
    if (name == "$::assume") return ControlIntrinsic::Assume;
    if (name == "$::unreachable") return ControlIntrinsic::Unreachable;
    if (name == "$::trap") return ControlIntrinsic::Trap;
    return ControlIntrinsic::None;
}

inline const char* control_intrinsic_arity_error(const Expr& expression) {
    switch (control_intrinsic(expression)) {
    case ControlIntrinsic::Expect:
        return expression.arguments.size() == 2 ? nullptr : "$::expect requires two arguments";
    case ControlIntrinsic::Assume:
        return expression.arguments.size() == 1 ? nullptr : "$::assume requires one argument";
    case ControlIntrinsic::Unreachable:
        return expression.arguments.empty() ? nullptr : "$::unreachable takes no arguments";
    case ControlIntrinsic::Trap:
        return expression.arguments.empty() ? nullptr : "$::trap takes no arguments";
    case ControlIntrinsic::None: return nullptr;
    }
    return nullptr;
}

// Operations on one floating type that return that type.
enum class FloatingIntrinsic { None, Sqrt, Fabs, Copysign, Fmin, Fmax };

inline FloatingIntrinsic floating_intrinsic(std::string_view name) {
    if (name == "$::sqrt") return FloatingIntrinsic::Sqrt;
    if (name == "$::fabs") return FloatingIntrinsic::Fabs;
    if (name == "$::copysign") return FloatingIntrinsic::Copysign;
    if (name == "$::fmin") return FloatingIntrinsic::Fmin;
    if (name == "$::fmax") return FloatingIntrinsic::Fmax;
    return FloatingIntrinsic::None;
}

inline FloatingIntrinsic floating_intrinsic(const Expr& expression) {
    return expression.kind == Expr::Kind::Call && expression.left &&
        expression.left->kind == Expr::Kind::Name
        ? floating_intrinsic(expression.left->text) : FloatingIntrinsic::None;
}

inline std::size_t floating_intrinsic_arity(FloatingIntrinsic operation) {
    return operation == FloatingIntrinsic::Sqrt || operation == FloatingIntrinsic::Fabs ? 1 : 2;
}

struct AssumptionViolation {
    enum class Kind { Effects, QualifiedRead } kind;
    const Expr* expression;
    const char* message() const {
        return kind == Kind::Effects
            ? "$::assume condition must be side-effect-free and is not evaluated"
            : "$::assume requires a non-volatile, non-atomic condition";
    }
};

// Source and MIR use the same effect walk. The callback answers whether this
// node's lvalue-to-value access is volatile/atomic using the caller's typed IR.
// Address formation and layout operands do not read their designated object.
template<class QualifiedRead>
std::optional<AssumptionViolation> assumption_violation(
    const Expr& expression, const QualifiedRead& qualified_read) {
    struct Pending { const Expr* expression; bool read; };
    std::vector<Pending> pending{{&expression, true}};
    const auto push = [&](const Expr* node, bool read = true) {
        if (node) pending.push_back({node, read});
    };
    while (!pending.empty()) {
        const auto [node, read] = pending.back();
        pending.pop_back();
        if (read && qualified_read(*node))
            return AssumptionViolation{AssumptionViolation::Kind::QualifiedRead, node};
        switch (node->kind) {
        case Expr::Kind::Integer: case Expr::Kind::Floating: case Expr::Kind::Character:
        case Expr::Kind::Name: case Expr::Kind::String: case Expr::Kind::Address:
            break;
        case Expr::Kind::Parenthesized: case Expr::Kind::Cast:
            push(node->left.get(), read);
            break;
        case Expr::Kind::Sizeof:
            // A written VLA type forms its bounds here; an existing array's
            // captured extent and a layout expression's operand are not read.
            for (auto type = node->type; type && type->kind == Type::Kind::Array; type = type->element)
                if (!type->lanes) push(type->array_bound.get());
            break;
        case Expr::Kind::Alignof: case Expr::Kind::Offsetof: break;
        case Expr::Kind::Unary:
            if (node->text == "&") push(node->left.get(), false);
            else if (node->text == "*" || node->text == "+" || node->text == "-" ||
                     node->text == "!" || node->text == "~") push(node->left.get());
            else return AssumptionViolation{AssumptionViolation::Kind::Effects, node};
            break;
        case Expr::Kind::Binary:
            if (node->text == "member") push(node->left.get(), false);
            else if (node->text == "pointer_member") push(node->left.get());
            else { push(node->right.get()); push(node->left.get()); }
            break;
        case Expr::Kind::Conditional:
            push(node->third.get()); push(node->right.get()); push(node->left.get());
            break;
        case Expr::Kind::Call:
            if (atomic_builtin(*node) == AtomicBuiltin::IsLockFree) {
                // Only the operand's source type is observed.
            } else if (control_intrinsic(*node) == ControlIntrinsic::Expect) {
                if (!node->arguments.empty()) push(node->arguments.front().get());
            } else if (floating_intrinsic(*node) != FloatingIntrinsic::None) {
                for (const auto& argument : node->arguments) push(argument.get());
            } else if (node->left && node->left->kind == Expr::Kind::Name &&
                       node->left->text == "$::eval") {
                // Its required value is prepared independently; no runtime
                // call or effect belongs to this assumption's condition.
            } else if (node->left && node->left->kind == Expr::Kind::Name &&
                       node->left->text == "$::runtime" && node->arguments.size() == 1) {
                push(node->arguments.front().get());
            } else return AssumptionViolation{AssumptionViolation::Kind::Effects, node};
            break;
        case Expr::Kind::VoidValue: case Expr::Kind::Quote: case Expr::Kind::ByteSequence:
        case Expr::Kind::Assign: case Expr::Kind::AggregateInitializer:
            return AssumptionViolation{AssumptionViolation::Kind::Effects, node};
        }
    }
    return {};
}

} // namespace cross
