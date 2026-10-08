// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace cross;

// Observe the host pump, not a Cross pointer or target address. A synchronous
// query nested inside an evaluated operation creates a different schedule.
struct Witness {
    ContinuationSchedule*& expected;
    bool& continuous;
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    bool await_suspend(std::coroutine_handle<Promise> frame) const noexcept {
        const auto schedule = frame.promise().schedule;
        if (!expected) expected = schedule;
        else if (expected != schedule) continuous = false;
        return false;
    }
    void await_resume() const noexcept {}
};

void check(unsigned bits, EvaluationByteOrder order, unsigned natural_alignment) {
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << bits << '/' << natural_alignment << ": " << message << '\n' << output.str();
            std::abort();
        }
    };
    const auto* source = sources.add("evaluation-layout.x", R"(
        typedef u32 Lanes [[ext_vector_type(4)]];
        struct Box { Lanes value; };
        static u32 run() {
            u32 scalar = 3u32;
            u32 *pointer = &scalar;
            *pointer += 4u32;
            u32 *saved = pointer;
            if (*saved != 7u32) return 1u32;
            u32 array[2] = {};
            array[1u32] = 11u32;
            if (array + 1u32 - array != 1iptr || !(array < array + 1u32)) return 2u32;
            Lanes value = (Lanes)3u32;
            value += 2u32;
            Lanes other = (Lanes)1u32;
            other = value + other;
            if (other[2u32] != 6u32 || (-other)[1u32] != (u32)-6i32) return 3u32;
            struct Box box = {};
            box.value = other;
            struct Box copy = box;
            struct Box *owner = &copy;
            u32 *leaf = (u32 *)owner;
            *leaf = 13u32;
            if (copy.value[0u32] != 13u32) return 4u32;
            return scalar + array[1u32] + copy.value[2u32];
        }
        global u32 entry() { return run(); }
    )");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    program.evaluation_layout.byte_order = order;
    program.evaluation_layout.natural_alignment_limit = natural_alignment;
    require(!diagnostics.errors() && program.functions.size() == 2 && program.records.size() == 1,
            "fixture failed to parse");
    ContinuationSchedule* expected{};
    bool continuous = true;
    unsigned sizes{}, alignments{}, members{}, plans{};
    bool throw_query{};
    LayoutQuery size_of, align_of;
    size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++sizes;
        if (throw_query) { throw_query = false; throw std::runtime_error("layout failure"); }
        if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) {
            const auto element = co_await size_of.async(type->element);
            co_return element ? std::optional<std::uint64_t>(*element * type->lanes) : std::nullopt;
        }
        if (type->kind == Type::Kind::Record) co_return 16;
        if (type->kind == Type::Kind::Pointer) co_return bits / 8;
        co_return type_bits(type) / 8;
    };
    align_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++alignments;
        if (type->kind == Type::Kind::Array) co_return co_await align_of.async(type->element);
        if (type->kind == Type::Kind::Record || type->kind == Type::Kind::Vector) co_return natural_alignment;
        const auto size = co_await size_of.async(type);
        co_return size ? std::optional<std::uint64_t>(std::min<std::uint64_t>(*size, natural_alignment)) : std::nullopt;
    };
    program.evaluation_size_of = size_of;
    program.evaluation_align_of = align_of;
    program.evaluation_member_layout = [&](const TypePtr&, const MemberName& name)
        -> ContinuationTask<std::optional<EvaluationMemberLayout>> {
        co_await Witness{expected, continuous};
        ++members;
        if (name != MemberName{"value", {}}) co_return std::nullopt;
        co_return EvaluationMemberLayout{0, natural_alignment, {}, 0};
    };
    program.evaluation_initializer_plan = [&](const Expr& expression, const TypePtr&)
        -> ContinuationTask<EvaluationInitializerPlan> {
        co_await Witness{expected, continuous};
        ++plans;
        require(expression.kind == Expr::Kind::AggregateInitializer && expression.initializer_entries.empty(),
                "fake layout received a nonempty initializer");
        EvaluationInitializerPlan plan;
        plan.valid = true;
        co_return plan;
    };
    const auto& expression = *program.functions.back()->body->statements.front()->expression;
    const auto evaluate = [&]() -> ContinuationTask<std::optional<Expr::IntegerConstant>> {
        co_await Witness{expected, continuous};
        co_return co_await evaluate_target_integer_constant_async(program, expression, diagnostics, size_of, align_of);
    };
    const auto result = evaluate().run();
    require(result && result->value == UInt128{24} && !diagnostics.errors(), "evaluated value or layout changed");
    require(continuous, "an object access/conversion restarted the host pump");
    require(sizes && alignments && members && plans, "a metadata service was not exercised");
    require(!program.evaluation_required_integer, "completed evaluation retained a borrowed service");
    // Failure cleanup must leave a fresh evaluation independent of the failed
    // callback and its private layout/service state.
    expected = nullptr;
    throw_query = true;
    try {
        (void)evaluate().run();
        require(false, "query exception was swallowed");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "layout failure", "query exception changed");
    }
    require(!program.evaluation_required_integer, "failed evaluation retained a borrowed service");
    expected = nullptr;
    const auto recovered = evaluate().run();
    require(recovered && recovered->value == UInt128{24} && continuous && !diagnostics.errors(),
            "fresh evaluation did not recover after a query failure");
}
}

int main() {
    for (const unsigned bits : {32U, 64U})
        for (const auto order : {cross::EvaluationByteOrder::Little, cross::EvaluationByteOrder::Big})
            for (const unsigned alignment : {2U, 8U}) check(bits, order, alignment);
}
