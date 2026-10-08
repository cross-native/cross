// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
using namespace cross;

enum class Owner { Function, Global, Synthetic, Definition, ProbeDefinition };
enum class Requirement { Quote, Call, Fresh, Recursive, Division, LocalType, LocalValue, ParameterValue };

void check(unsigned bits, Owner owner, Requirement requirement, unsigned repetitions) {
    const bool optional = owner == Owner::ProbeDefinition;
    const bool global_requirement = owner != Owner::Function && !optional;
    const bool invalid = !optional && (requirement == Requirement::Division || requirement == Requirement::LocalValue ||
        requirement == Requirement::ParameterValue);
    const auto bound_source = [&]() -> std::string {
        switch (requirement) {
        case Requirement::Quote: return "$::meta::len($::quote { a b })";
        case Requirement::Call:
        case Requirement::Recursive: return "$::eval(count($::quote { a b }))";
        case Requirement::Fresh: return "$::meta::len($::meta::gensym(\"fresh\")) + 1uptr";
        case Requirement::Division: return "1u32 / 0u32";
        case Requirement::LocalType: return "sizeof(local)";
        case Requirement::LocalValue: return "local";
        case Requirement::ParameterValue: return "$::meta::len(input)";
        }
        std::abort();
    }();
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("evaluation-context.x", R"(
        static uptr count(in $::meta::tokens input) { return
    )" + std::string(requirement == Requirement::Recursive ? "sizeof(u8[1])" : "$::meta::len(input)") + R"(; }
        static $::meta::tokens expand(in $::meta::tokens input) {
            u16 local = 7u16;
            if (0u32) $::patch((uptr)sizeof(u8[1]));
            if (sizeof(u8[1]) != 2uptr) return $::quote { wrong };
            return
    )" + std::string(requirement == Requirement::Fresh ? "$::meta::gensym(\"after\")" : "input") + R"(;
            if (0u32) { uptr requirement =
    )" + bound_source + "; } }");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    const auto require = [&](bool condition) {
        if (!condition) {
            std::cerr << "nested evaluation context: bits=" << bits << " global=" << global_requirement
                      << " bound=" << bound_source << " repeats=" << repetitions << '\n' << messages.str();
            std::abort();
        }
    };
    require(!diagnostics.errors() && program.functions.size() == 2 && program.objects.empty());
    const auto* function = program.functions.back().get();
    const auto& parameter = function->parameters.front();
    const auto& local = *function->body->statements.front()->declaration;
    const std::vector<std::pair<NameKey, TypePtr>> local_types{
        {name_key(parameter), parameter.type}, {name_key(local), local.type}};
    FunctionDecl synthetic;
    synthetic.source_namespace = function->source_namespace;
    const auto* lexical_owner = owner == Owner::Synthetic ? &synthetic
        : owner == Owner::Global ? nullptr : function;
    const auto& bound = *function->body->statements.back()->first->statements.front()->declaration->initializer;
    unsigned queries{};
    LayoutQuery size_of;
    LayoutQuery align_of = [](const TypePtr&) -> std::optional<std::uint64_t> { return 1; };
    size_of = [&](const TypePtr& type) -> std::optional<std::uint64_t> {
        if (type->kind != Type::Kind::Array) return type_bits(type) / 8;
        std::optional<Expr::IntegerConstant> value;
        for (unsigned index = 0; index < repetitions; ++index) {
            ++queries;
            value = evaluate_target_integer_constant(program, bound, diagnostics, size_of, align_of,
                {}, lexical_owner, local_types, optional ? EvaluationIntegerContext::ProbeDefinition
                    : owner == Owner::Definition ? EvaluationIntegerContext::Definition
                                                : EvaluationIntegerContext::CallerInvocation);
            if (!value && optional && !diagnostics.errors())
                value = Expr::IntegerConstant{UInt128{2}, BuiltinType::Uptr};
            if (!value) return {};
        }
        return value ? std::optional{value->value.low} : std::nullopt;
    };
    program.evaluation_size_of = size_of;
    program.evaluation_align_of = align_of;
    if (!global_requirement && !invalid && repetitions == 1 && requirement != Requirement::Recursive) {
        ExpansionSemantics semantics(program, diagnostics, "default", {});
        require(semantics.validate_source(*function));
        require(queries != 0 && !program.evaluation_required_integer);
    }
    auto context = std::make_shared<SyntaxContext>();
    context->kind = SyntaxContext::Kind::DefinitionSite;
    context->definition = function->location;
    context->invocation = function->location;
    context->expansion = ExpansionId{99};
    auto call = std::make_shared<SyntaxContext>(*context);
    call->kind = SyntaxContext::Kind::CallSite;
    EvaluationLimits limits;
    if (repetitions > 1) limits.steps = 100;
    if (requirement == Requirement::Recursive) limits.depth = 8;
    TokenSequence input;
    input.emplace_back(Token{TokenKind::Identifier, "kept", function->location});
    bool prior_called{};
    if (requirement == Requirement::Fresh) {
        program.evaluation_required_integer = [&](const Expr&, Diagnostics&, const LayoutQuery&, const LayoutQuery&,
            std::string_view, const FunctionDecl*, std::span<const std::pair<NameKey, TypePtr>>,
            EvaluationIntegerContext) {
            prior_called = true;
            return EvaluationIntegerResult{EvaluationIntegerResult::Status::Value,
                Expr::IntegerConstant{UInt128{42}, BuiltinType::U32}};
        };
    }
    queries = 0;
    const auto output = evaluate_procedural_body(*function, input, bits, size_of, align_of,
        context, diagnostics, limits, {}, {}, call, &program);
    require(queries != 0 && !prior_called);
    if (requirement == Requirement::Fresh) {
        require(static_cast<bool>(program.evaluation_required_integer));
        const auto restored = program.evaluation_required_integer(bound, diagnostics, size_of, align_of,
            {}, nullptr, {}, EvaluationIntegerContext::Definition);
        require(prior_called && restored.value && restored.value->value == UInt128{42});
        program.evaluation_required_integer = {};
    } else require(!program.evaluation_required_integer);
    require(!program.evaluation_generic_value);
    if (global_requirement || invalid || repetitions > 1 || requirement == Requirement::Recursive) {
        require(!output && diagnostics.errors() == 1);
        const auto expected = global_requirement ? "active expansion context"
            : requirement == Requirement::Recursive ? "recursion depth exceeded"
            : requirement == Requirement::Division ? "division by zero"
            : invalid ? "runtime local or parameter is not a translation-time value" : "instruction budget";
        require(messages.str().find(expected) != std::string::npos);
        if (repetitions > 1) require(queries < repetitions);
        if (requirement == Requirement::Recursive) require(queries <= limits.depth);
    } else {
        const bool fresh = requirement == Requirement::Fresh;
        require(output && output->size() == 1 && output->front().text == (fresh ? "after" : "kept") &&
            !diagnostics.errors());
        if (fresh) {
            const auto& identity = output->front().origin.fresh;
            require(identity && identity->expansion == context->expansion && identity->ordinal > queries);
        }
        // The completed invocation must leave no callable borrowing its evaluator.
        if (!optional && requirement != Requirement::LocalType) {
            const auto outside = evaluate_target_integer_constant(program, bound, diagnostics, size_of, align_of);
            require(!outside && messages.str().find("active expansion context") != std::string::npos);
        }
    }
}

void check_type_isolation() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto require = [&](bool condition) {
        if (!condition) {
            std::cerr << "private required-type state\n" << messages.str();
            std::abort();
        }
    };
    const auto* source = sources.add("evaluation-type-isolation.x", R"(
        static T identity<T>(in T value) { return value; }
        static uptr constant<uptr N>() { return N; }
        static $::meta::tokens expand(in $::meta::tokens input) {
            typedef u8 A[$::meta::len($::quote { a b }) * sizeof(uptr)];
            typedef u8 A[2uptr * sizeof(uptr)];
            A local;
            typedef u8 B[sizeof(local)];
            $::static_assert(sizeof(A) == 2uptr * sizeof(uptr), "private alias");
            $::static_assert(sizeof(B) == sizeof(A), "private local");
            typedef uptr V [[vector_size($::meta::len($::quote {a b}) * sizeof(uptr))]];
            typedef u16 W [[ext_vector_type($::meta::len($::quote {a b}) * sizeof(uptr))]];
            V vector = 7uptr;
            V copied = identity(vector + 1uptr);
            $::static_assert(sizeof(V) == 2uptr * sizeof(uptr), "private vector bytes");
            $::static_assert(sizeof(W) == 4uptr * sizeof(uptr), "private vector lanes");
            if (vector[1] != 7uptr || copied[1] != 8uptr) return $::quote {wrong};
            if (constant<($::meta::len($::quote {a b}) * sizeof(uptr))>() !=
                2uptr * sizeof(uptr)) return $::quote {wrong};
            return input;
        }
    )");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, 32);
    auto program = parser.parse();
    require(!diagnostics.errors() && program.functions.size() == 3);
    const auto& function = *program.functions.back();
    require(function.required_types.size() == 5);
    const auto unchanged = [&] {
        for (const auto& requirement : function.required_types) {
            require(requirement.type->lanes == 0);
            if (requirement.type->kind == Type::Kind::Vector) {
                require(requirement.type->vector_bound != nullptr);
                require(requirement.type->vector_extent_dependency == Type::VectorExtentDependency::None);
            } else {
                require(requirement.type->array_bound != nullptr);
                require(requirement.type->array_extent_dependency == Type::ArrayExtentDependency::None);
            }
            if (requirement.compatible_with) require(requirement.compatible_with->lanes == 0);
        }
        require(!program.evaluation_required_integer);
        require(!program.evaluation_generic_value);
    };
    unsigned bits{};
    LayoutQuery layout;
    layout = [&](const TypePtr& type) -> std::optional<std::uint64_t> {
        if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) {
            const auto element = layout(type->element);
            return type->lanes && element ? std::optional<std::uint64_t>{*element * type->lanes} : std::nullopt;
        }
        if (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Uptr) return bits / 8;
        return type_bits(type) / 8;
    };
    program.evaluation_size_of = layout;
    program.evaluation_align_of = layout;
    TokenSequence input;
    input.emplace_back(Token{TokenKind::Identifier, "kept", function.location});
    // Reuse one parsed definition with changing injected target layout facts.
    // This is an isolation check, not a claim that production target profiles
    // change during a translation unit.
    for (const auto width : {32U, 64U, 32U}) {
        bits = width;
        program.address_bits = bits;
        ExpansionSemantics semantics(program, diagnostics, "default", {});
        const auto definitions = program.functions.size();
        require(semantics.validate_source(function));
        require(program.functions.size() == definitions);
        unchanged();
        auto context = std::make_shared<SyntaxContext>();
        context->kind = SyntaxContext::Kind::DefinitionSite;
        context->definition = function.location;
        context->invocation = function.location;
        context->expansion = ExpansionId{width};
        const auto output = evaluate_procedural_body(function, input, bits, layout, layout,
            context, diagnostics, {}, {}, {}, {}, &program);
        require(output && output->size() == 1 && output->front().text == "kept" && !diagnostics.errors());
        for (const auto& instance : program.functions) {
            if (!instance->invocation_specialization) continue;
            require(!has_pending_type_bound(instance->return_type));
            for (const auto& parameter : instance->parameters)
                require(!has_pending_type_bound(parameter.type));
        }
        unchanged();
    }
}
}

int main() {
    check_type_isolation();
    for (const unsigned bits : {32U, 64U}) {
        check(bits, Owner::Function, Requirement::Quote, 1);
        check(bits, Owner::Function, Requirement::Call, 1);
        check(bits, Owner::Function, Requirement::Fresh, 1);
        check(bits, Owner::Function, Requirement::Recursive, 1);
        check(bits, Owner::Global, Requirement::Quote, 1);
        check(bits, Owner::Synthetic, Requirement::Quote, 1);
        check(bits, Owner::Definition, Requirement::Quote, 1);
        check(bits, Owner::ProbeDefinition, Requirement::Quote, 1);
        check(bits, Owner::ProbeDefinition, Requirement::Division, 1);
        check(bits, Owner::ProbeDefinition, Requirement::LocalValue, 1);
        check(bits, Owner::ProbeDefinition, Requirement::Quote, 100);
        check(bits, Owner::Function, Requirement::Division, 1);
        check(bits, Owner::Function, Requirement::LocalType, 1);
        check(bits, Owner::Function, Requirement::LocalValue, 1);
        check(bits, Owner::Function, Requirement::ParameterValue, 1);
        check(bits, Owner::Function, Requirement::Quote, 100);
    }
}
