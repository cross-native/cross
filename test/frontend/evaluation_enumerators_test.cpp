// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <sstream>
#include <stdexcept>

namespace {
using namespace cross;

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

void check(unsigned bits, EvaluationByteOrder order) {
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition, const char* message,
        std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << bits << ':' << at.line() << ": " << message << '\n' << output.str();
            std::abort();
        }
    };
    std::string text = "namespace Values { static uptr next(in uptr value) { return value + 1uptr; }\n";
    text += "enum Number0 [[underlying(uptr)]] { value0 = next(sizeof(uptr)) };\n";
    for (unsigned index = 1; index <= 64; ++index)
        text += "enum Number" + std::to_string(index) + " [[underlying(uptr)]] { value" +
            std::to_string(index) + " = next((uptr)value" + std::to_string(index - 1) + ") };\n";
    text += "}\nuptr result = (uptr)Values::value64;\n";
    const auto* source = sources.add("evaluation-enumerators.x", text);
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    program.evaluation_layout.byte_order = order;
    require(!diagnostics.errors() && program.enumerations.size() == 65 && program.objects.size() == 1,
            "fixture failed to parse");
    ContinuationSchedule* expected{};
    bool continuous = true;
    bool throw_query = true;
    bool grew{};
    unsigned queries{}, preparations{};
    LayoutQuery size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++queries;
        if (!grew) {
            program.enumerations.reserve(program.enumerations.capacity() + 64);
            grew = true;
        }
        if (throw_query) { throw_query = false; throw std::runtime_error("enum layout failure"); }
        if (type->kind == Type::Kind::Pointer || type->builtin == BuiltinType::Uptr ||
            type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Label)
            co_return bits / 8;
        co_return type_bits(type) / 8;
    };
    LayoutQuery align_of = [](const TypePtr&) -> std::optional<std::uint64_t> { return 1; };
    program.evaluation_size_of = size_of;
    program.evaluation_align_of = align_of;
    bool previous_called{};
    program.evaluation_prepare_enumerator = [&](Program::EnumerationPosition) {
        previous_called = true;
        return false;
    };
    {
        ExpansionSemantics semantics(program, diagnostics, "default", {});
        const auto prepare = program.evaluation_prepare_enumerator;
        program.evaluation_prepare_enumerator = [&](Program::EnumerationPosition position) -> ContinuationTask<bool> {
            co_await Witness{expected, continuous};
            ++preparations;
            co_return co_await prepare.async(position);
        };
        const auto evaluate = [&]() -> ContinuationTask<std::optional<Expr::IntegerConstant>> {
            co_await Witness{expected, continuous};
            co_return co_await evaluate_target_integer_constant_async(program,
                *program.objects.front()->initializer, diagnostics, size_of, align_of);
        };
        const auto clean = [&]() {
            require(!program.evaluation_enumerator_position, "enumerator position escaped a query");
            require(!program.evaluation_required_integer && !program.evaluation_generic_value,
                    "evaluation retained a borrowed value service");
            for (const auto& enumeration : program.enumerations)
                require(enumeration.enumerators.front().initializer != nullptr,
                        "unwinding lost an enum initializer");
        };
        try {
            (void)evaluate().run();
            require(false, "query exception was swallowed");
        } catch (const std::runtime_error& error) {
            require(std::string_view(error.what()) == "enum layout failure", "query exception changed");
        }
        clean();
        require(continuous && grew && queries && preparations == 65 && !diagnostics.errors(),
                "enum preparation restarted the pump or skipped the dependency chain");
        expected = nullptr;
        const auto result = evaluate().run();
        require(result.has_value() && !diagnostics.errors(), "fresh evaluation failed after callback unwinding");
        require(result->value == UInt128{bits / 8 + 65}, "enum evaluation lost target width or a dependency");
        require(continuous, "enum retry restarted the host pump");
        clean();
        for (auto& enumeration : program.enumerations) enumeration.enumerators.front().value.reset();
        const auto resources = program.evaluation_resource_errors;
        program.evaluation_limits.steps = 128;
        expected = nullptr;
        const auto limited = evaluate().run();
        require(!limited && diagnostics.errors() == 1 &&
                program.evaluation_resource_errors == resources + 1 &&
                output.str().find("instruction budget exceeded 128") != std::string::npos,
                "enum work did not share one bounded failure");
        clean();
        program.evaluation_limits.steps = 1000000;
        expected = nullptr;
        const auto recovered = evaluate().run();
        require(recovered && recovered->value == UInt128{bits / 8 + 65} &&
                diagnostics.errors() == 1 && continuous,
                "fresh evaluation inherited failed enum state");
        clean();
        require(!previous_called, "enum query bypassed the scoped semantic service");
    }
    require(!program.evaluation_prepare_enumerator({0, 0}) && previous_called,
            "semantic scope did not restore the previous enum service");
}
}

int main() {
    for (const unsigned bits : {32U, 64U})
        for (const auto order : {EvaluationByteOrder::Little, EvaluationByteOrder::Big}) check(bits, order);
}
