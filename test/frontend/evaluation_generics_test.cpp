// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"

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

void check(unsigned bits, EvaluationByteOrder order, bool active, bool labels, bool caller_scope) {
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition, const char* message,
        std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << bits << ':' << active << ':' << labels << ':' << caller_scope << ':'
                      << at.line() << ": " << message << '\n' << output.str();
            std::abort();
        }
    };
    std::string text = R"(
        static uptr constant<uptr N, uptr M>() { return N + M; }
        static uptr label_value<label L>() { return sizeof(uptr); }
        global void owner() { global label point: ; }
        static uptr caller() { return 0uptr; }
        uptr trigger = sizeof(u8[1]);
        uptr input =
    )";
    text += labels ? "label_value<(sizeof(uptr) ? owner::point : owner::point)>();"
                   : "constant<(sizeof(uptr)), 0uptr>();";
    const auto* source = sources.add("evaluation-generics.x", text);
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    program.evaluation_layout.byte_order = order;
    require(!diagnostics.errors() && program.objects.size() == 2, "fixture failed to parse");
    const auto* caller = caller_scope ? program.functions.back().get() : nullptr;
    std::unique_ptr<Expr> input;
    ContinuationSchedule* expected{};
    bool continuous = true;
    bool throw_query = true;
    unsigned queries{};
    LayoutQuery size_of;
    LayoutQuery align_of = [](const TypePtr&) -> std::optional<std::uint64_t> { return 1; };
    const auto prepare = [&]() -> ContinuationTask<std::optional<std::uint64_t>> {
        if (!(co_await program.evaluation_prepare_expression.async(input, caller, {}))) co_return std::nullopt;
        const auto value = co_await evaluate_target_integer_constant_async(program, *input,
            diagnostics, size_of, align_of, {}, caller);
        if (value) co_return value->value.low;
        co_return std::nullopt;
    };
    size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++queries;
        if (type->kind == Type::Kind::Array) co_return co_await prepare();
        if (throw_query) { throw_query = false; throw std::runtime_error("generic layout failure"); }
        if (type->kind == Type::Kind::Pointer || type->builtin == BuiltinType::Uptr ||
            type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Label)
            co_return bits / 8;
        co_return type_bits(type) / 8;
    };
    program.evaluation_size_of = size_of;
    program.evaluation_align_of = align_of;
    {
        ExpansionSemantics semantics(program, diagnostics, "default", {});
        const auto evaluate = [&]() -> ContinuationTask<std::optional<std::uint64_t>> {
            co_await Witness{expected, continuous};
            input = copy_expression(*program.objects.back()->initializer);
            if (!active) co_return co_await prepare();
            const auto value = co_await evaluate_target_integer_constant_async(program,
                *program.objects.front()->initializer, diagnostics, size_of, align_of);
            if (value) co_return value->value.low;
            co_return std::nullopt;
        };
        const auto clean = [&] {
            require(!program.evaluation_required_integer && !program.evaluation_generic_value,
                    "generic preparation retained a borrowed value service");
            require(program.objects.back()->initializer->generic_arguments.size() == (labels ? 1U : 2U),
                    "generic preparation modified the retained source");
        };
        try {
            (void)evaluate().run();
            require(false, "query exception was swallowed");
        } catch (const std::runtime_error& error) {
            require(std::string_view(error.what()) == "generic layout failure", "query exception changed");
        }
        clean();
        require(continuous && queries && !diagnostics.errors(), "generic normalization restarted the host pump");
        expected = nullptr;
        const auto recovered = evaluate().run();
        require(recovered && *recovered == bits / 8 && !diagnostics.errors() && continuous,
                "generic evaluation did not recover with its selected target width");
        clean();
        const auto resources = program.evaluation_resource_errors;
        program.evaluation_limits.steps = 2;
        expected = nullptr;
        const auto limited = evaluate().run();
        require(!limited && diagnostics.errors() == 1 &&
                program.evaluation_resource_errors == resources + 1 &&
                output.str().find("instruction budget exceeded 2") != std::string::npos,
                "generic work did not propagate one bounded failure");
        clean();
        program.evaluation_limits.steps = 1000000;
        expected = nullptr;
        const auto fresh = evaluate().run();
        require(fresh && *fresh == bits / 8 && diagnostics.errors() == 1 && continuous,
                "fresh generic evaluation inherited failed preparation state");
        clean();
    }
    require(!program.evaluation_prepare_expression, "generic preparation service outlived its owner");
}

void automatic_bound(unsigned bits, EvaluationByteOrder order) {
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto* source = sources.add("automatic-bound.x", R"cross(
static uptr bound() { return sizeof(uptr); }
static uptr local(in uptr count) {
    u8 fixed[bound()];
    u8 runtime[count];
    return sizeof(fixed);
}
)cross");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
    if (diagnostics.errors()) std::cerr << output.str();
    if (diagnostics.errors() || program.functions.size() != 2) std::abort();
    program.address_bits = bits;
    program.evaluation_layout.byte_order = order;
    ContinuationSchedule* expected{};
    bool continuous = true;
    unsigned queries{};
    program.evaluation_size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++queries;
        co_return type->builtin == BuiltinType::Uptr ? bits / 8U : type_bits(type) / 8U;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    ExpansionSemantics semantics(program, diagnostics, "default", {});
    const auto prepare = [&]() -> ContinuationTask<std::unique_ptr<FunctionDecl>> {
        co_await Witness{expected, continuous};
        co_return co_await semantics.prepare_async(*program.functions.back());
    };
    const auto prepared = prepare().run();
    if (!prepared || !continuous || diagnostics.errors() || queries != 1) {
        std::cerr << "automatic bound: bits=" << bits << " continuous=" << continuous << '\n' << output.str();
        std::abort();
    }
    const auto& fixed = *prepared->body->statements[0]->declaration;
    const auto& runtime = *prepared->body->statements[1]->declaration;
    if (fixed.dynamic_array_bound || fixed.type->lanes != bits / 8U ||
        !runtime.dynamic_array_bound || runtime.type->lanes != 0 ||
        !program.functions.back()->body->statements[0]->declaration->dynamic_array_bound) {
        std::cerr << "automatic normalization lost fixed/runtime extent or modified retained source\n";
        std::abort();
    }
}

void automatic_bound_recovery(unsigned bits) {
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto* source = sources.add("automatic-bound-recovery.x",
        "static uptr bound() { return sizeof(uptr); } static void local() { u8 fixed[bound()]; }");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
    if (diagnostics.errors()) std::abort();
    program.address_bits = bits;
    bool throw_query = true;
    unsigned queries{};
    program.evaluation_size_of = [&](const TypePtr&) -> std::optional<std::uint64_t> {
        ++queries;
        if (throw_query) { throw_query = false; throw std::runtime_error("automatic extent layout"); }
        return bits / 8U;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    ExpansionSemantics semantics(program, diagnostics, "default", {});
    bool threw{};
    try { (void)semantics.prepare_async(*program.functions.back()).run(); }
    catch (const std::runtime_error& error) { threw = std::string_view(error.what()) == "automatic extent layout"; }
    const auto clean = [&] {
        return !program.evaluation_required_integer && !program.evaluation_generic_value &&
            !program.evaluation_prepare_layout && !program.evaluation_layout_scope &&
            program.functions.back()->body->statements[0]->declaration->dynamic_array_bound != nullptr;
    };
    if (!threw || diagnostics.errors() || !clean()) std::abort();
    const auto recovered = semantics.prepare_async(*program.functions.back()).run();
    if (!recovered || recovered->body->statements[0]->declaration->type->lanes != bits / 8U ||
        !clean() || queries != 2) std::abort();
    const auto epoch = program.evaluation_resource_errors;
    program.evaluation_limits.steps = 1;
    const auto limited = semantics.prepare_async(*program.functions.back()).run();
    if (limited || diagnostics.errors() != 1 || program.evaluation_resource_errors != epoch + 1 ||
        queries != 2 || !clean() || output.str().find("instruction budget exceeded 1") == std::string::npos) {
        std::cerr << "automatic extent resource failure was swallowed or repeated\n" << output.str();
        std::abort();
    }
    program.evaluation_limits.steps = 1000000;
    const auto fresh = semantics.prepare_async(*program.functions.back()).run();
    if (!fresh || fresh->body->statements[0]->declaration->type->lanes != bits / 8U ||
        queries != 3 || diagnostics.errors() != 1 || !clean()) std::abort();
}

void invalid_automatic_bounds(unsigned bits) {
    for (const auto result : {"0i64", "-1i64", "4294967296i64"}) {
        SourceManager sources;
        std::ostringstream output;
        Diagnostics diagnostics(output);
        const auto* source = sources.add("automatic-bound-invalid.x",
            std::string("static i64 bound() { return ") + result +
                "; } static void local() { u8 invalid[bound()]; }");
        auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
        if (diagnostics.errors()) std::abort();
        program.address_bits = bits;
        ExpansionSemantics semantics(program, diagnostics, "default", {});
        const auto prepared = semantics.prepare_async(*program.functions.back()).run();
        if (prepared || diagnostics.errors() != 1 || program.evaluation_resource_errors != 0 ||
            output.str().find("fixed array bound must be a positive integer representable in 32 bits") == std::string::npos) {
            std::cerr << "automatic extent did not reject its proven invalid value\n" << output.str();
            std::abort();
        }
    }
}
}

int main() {
    for (const unsigned bits : {32U, 64U}) {
        automatic_bound_recovery(bits);
        invalid_automatic_bounds(bits);
        for (const auto order : {EvaluationByteOrder::Little, EvaluationByteOrder::Big}) {
            automatic_bound(bits, order);
            for (const bool active : {false, true})
                for (const bool labels : {false, true})
                    for (const bool caller : {false, true}) check(bits, order, active, labels, caller);
        }
    }
}
