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
#include <utility>

namespace {
using namespace cross;

struct Witness {
    ContinuationSchedule*& expected;
    bool& continuous;
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    bool await_suspend(std::coroutine_handle<Promise> frame) const noexcept {
        if (!expected) expected = frame.promise().schedule;
        else if (expected != frame.promise().schedule) continuous = false;
        return false;
    }
    void await_resume() const noexcept {}
};

void require(bool condition, const char* detail,
    std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::cerr << location.line() << ": " << detail << '\n';
        std::abort();
    }
}

std::unique_ptr<Expr> integer(unsigned value) {
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Integer;
    result->text = std::to_string(value) + "u32";
    result->evaluated_integer = Expr::IntegerConstant{UInt128{value}, BuiltinType::U32};
    return result;
}

void release_expression(std::unique_ptr<Expr>& root) {
    std::vector<std::unique_ptr<Expr>> pending;
    pending.push_back(std::move(root));
    while (!pending.empty()) {
        auto node = std::move(pending.back());
        pending.pop_back();
        if (!node) continue;
        pending.push_back(std::move(node->left));
        pending.push_back(std::move(node->right));
        pending.push_back(std::move(node->third));
        for (auto& argument : node->arguments) pending.push_back(std::move(argument));
        for (auto& argument : node->generic_arguments) pending.push_back(std::move(argument.value));
        for (auto& entry : node->initializer_entries) {
            for (auto& designator : entry.designators) pending.push_back(std::move(designator.index));
            pending.push_back(std::move(entry.value));
        }
    }
}

// Synthetic owners are dismantled iteratively. These are host traversal tests,
// not source acceptance beyond the language/evaluator depth limits.
struct Cleanup {
    Program& program;
    std::vector<TypePtr> types;
    ~Cleanup() {
        for (auto& object : program.objects) {
            release_expression(object->initializer);
            object->type.reset();
        }
        for (auto& assertion : program.static_assertions) release_expression(assertion.condition);
        std::vector<std::unique_ptr<Statement>> pending;
        for (auto& function : program.functions) pending.push_back(std::move(function->body));
        while (!pending.empty()) {
            auto node = std::move(pending.back());
            pending.pop_back();
            if (!node) continue;
            release_expression(node->expression);
            release_expression(node->condition);
            release_expression(node->increment);
            if (node->declaration) {
                release_expression(node->declaration->initializer);
                release_expression(node->declaration->dynamic_array_bound);
            }
            pending.push_back(std::move(node->first));
            pending.push_back(std::move(node->second));
            for (auto& child : node->statements) pending.push_back(std::move(child));
        }
        for (auto iterator = types.rbegin(); iterator != types.rend(); ++iterator) iterator->reset();
    }
};

void deep_expression() {
    Program program;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    Cleanup cleanup{program, {}};
    auto object = std::make_unique<ObjectDecl>();
    object->name = "deep";
    object->type = builtin_type(BuiltinType::U32);
    object->initializer = integer(1);
    for (unsigned index = 0; index < 50000; ++index) {
        auto wrapper = std::make_unique<Expr>();
        wrapper->kind = Expr::Kind::Parenthesized;
        wrapper->left = std::move(object->initializer);
        object->initializer = std::move(wrapper);
    }
    program.objects.push_back(std::move(object));
    unsigned layouts{};
    const LayoutQuery layout = [&](const TypePtr&) -> std::optional<std::uint64_t> {
        ++layouts;
        return 4;
    };
    require(finalize_target_constants(program, diagnostics, layout, layout) &&
                !diagnostics.errors() && layouts == 0,
            "layout-free deep finalization recursed or executed a value");
}

void ordinary(unsigned bits, EvaluationByteOrder order, unsigned alignment, bool legacy) {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("finalization.x", R"cross(
u8 storage[64];
u8 *shifted = storage + sizeof(uptr);
uptr width = sizeof(uptr);
f64 floating = (f64)sizeof(uptr);
u8 *null = sizeof(uptr) - sizeof(uptr);
uptr grouped[3] = { sizeof(uptr), $::alignof(u16), 1u32 };
$::static_assert(sizeof(uptr) != 0uptr, "selected width");
static u8 *patch(in u16 input) { return $::patch(storage + sizeof(input)); }
)cross");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
    program.address_bits = bits;
    program.evaluation_layout.byte_order = order;
    require(!diagnostics.errors() && program.objects.size() == 6 && program.functions.size() == 1,
            "ordinary finalization fixture did not parse");
    ContinuationSchedule* expected{};
    bool continuous = true;
    unsigned sizes{}, alignments{};
    LayoutQuery size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++sizes;
        co_return type->builtin == BuiltinType::Uptr ? bits / 8U : type_bits(type) / 8U;
    };
    LayoutQuery align_of = [&](const TypePtr&) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++alignments;
        co_return alignment;
    };
    const auto run = [&]() -> ContinuationTask<bool> {
        co_await Witness{expected, continuous};
        if (legacy) co_return finalize_target_constants(program, diagnostics, size_of, align_of);
        co_return co_await finalize_target_constants_async(program, diagnostics, size_of, align_of);
    };
    const auto valid = run().run();
    if (!valid) std::cerr << messages.str();
    require(valid && !diagnostics.errors(), "ordinary target constants failed");
    require(continuous != legacy && sizes && alignments,
            "finalization restarted its caller's pump or the legacy control did not observe it");
    const auto constant = [&](const Expr& expression, unsigned value) {
        return expression.evaluated_integer && expression.evaluated_integer->value == UInt128{value};
    };
    require(constant(*program.objects[1]->initializer->right, bits / 8U) &&
                program.objects[1]->initializer->left->kind == Expr::Kind::Name,
            "relocation offset was not folded or its symbolic base was erased");
    require(constant(*program.objects[2]->initializer, bits / 8U) &&
                program.objects[3]->initializer->evaluated_floating &&
                constant(*program.objects[4]->initializer, 0),
            "scalar/floating or integer-zero pointer folding changed");
    const auto& entries = program.objects[5]->initializer->initializer_entries;
    require(constant(*entries[0].value, bits / 8U) && constant(*entries[1].value, alignment) &&
                constant(*entries[2].value, 1), "aggregate destination/order changed");
    const auto& patch = *program.functions[0]->body->statements[0]->expression;
    require(constant(*patch.arguments[0]->right, 2), "patch proof lost its parameter's lexical type");
    require(!program.evaluation_required_integer && !program.evaluation_generic_value &&
                !program.evaluation_prepare_layout && !program.evaluation_layout_scope,
            "finalization retained borrowed evaluator services");
}

void deep_initializer() {
    constexpr unsigned depth = 50000;
    Program program;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    Cleanup cleanup{program, {}};
    auto object = std::make_unique<ObjectDecl>();
    object->name = "deep_initializer";
    object->type = builtin_type(BuiltinType::Uptr);
    object->initializer = std::make_unique<Expr>();
    object->initializer->kind = Expr::Kind::Sizeof;
    object->initializer->type = builtin_type(BuiltinType::Uptr);
    for (unsigned index = 0; index < depth; ++index) {
        auto wrapper = std::make_unique<Expr>();
        wrapper->kind = Expr::Kind::AggregateInitializer;
        Expr::InitializerEntry entry;
        entry.value = std::move(object->initializer);
        wrapper->initializer_entries.push_back(std::move(entry));
        object->initializer = std::move(wrapper);
        object->type = array_type(object->type, 1);
        cleanup.types.push_back(object->type);
    }
    program.objects.push_back(std::move(object));
    const LayoutQuery layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return 8; };
    require(finalize_target_constants(program, diagnostics, layout, layout) && !diagnostics.errors(),
            "deep aggregate finalization failed");
    // The leaf is replaced, so find it through the retained aggregate owners.
    auto* leaf = program.objects[0]->initializer.get();
    for (unsigned index = 0; index < depth; ++index) leaf = leaf->initializer_entries[0].value.get();
    require(leaf->evaluated_integer && leaf->evaluated_integer->value == UInt128{8},
            "deep aggregate lost its target proof or nesting");
}

void deep_statements() {
    Program program;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    Cleanup cleanup{program, {}};
    auto function = std::make_unique<FunctionDecl>();
    function->name = "deep_statements";
    function->return_type = builtin_type(BuiltinType::Void);
    function->body = std::make_unique<Statement>();
    for (unsigned index = 0; index < 50000; ++index) {
        auto wrapper = std::make_unique<Statement>();
        wrapper->kind = Statement::Kind::Compound;
        wrapper->statements.push_back(std::move(function->body));
        function->body = std::move(wrapper);
    }
    program.functions.push_back(std::move(function));
    const LayoutQuery layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return 4; };
    require(finalize_target_constants(program, diagnostics, layout, layout) && !diagnostics.errors(),
            "deep statement patch traversal recursed");
}

void publication_and_failure() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("finalization-publication.x",
        "$::static_assert(sizeof(uptr) != 0uptr, \"owned assertion\");");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
    ContinuationSchedule* expected{};
    bool continuous = true;
    bool publish = true, throwing = true;
    unsigned queries{};
    LayoutQuery layout = [&](const TypePtr&) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++queries;
        if (publish) {
            publish = false;
            program.static_assertions.reserve(program.static_assertions.capacity() + 64);
            StaticAssertDecl added;
            added.condition = integer(1);
            added.message = "published assertion";
            program.static_assertions.push_back(std::move(added));
        }
        if (throwing) { throwing = false; throw std::runtime_error("finalization callback failure"); }
        co_return 8;
    };
    const auto run = [&]() -> ContinuationTask<bool> {
        co_await Witness{expected, continuous};
        co_return co_await finalize_target_constants_async(program, diagnostics, layout, layout);
    };
    try {
        (void)run().run();
        require(false, "finalization callback exception was swallowed");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "finalization callback failure" && queries == 1,
                "callback exception identity changed or execution continued");
    }
    require(program.static_assertions.size() == 2 && program.static_assertions[0].condition &&
                !program.evaluation_required_integer && !program.evaluation_generic_value &&
                !program.evaluation_prepare_layout && !program.evaluation_layout_scope,
            "assertion publication/exception lost its owner or retained services");
    expected = nullptr;
    require(run().run() && continuous && queries == 2 && !diagnostics.errors(),
            "fresh assertion finalization did not recover after publication/exception");

    const auto* objects = sources.add("finalization-object-publication.x", "uptr first = sizeof(uptr);");
    auto object_program = Parser(Lexer(*objects, diagnostics).lex(), diagnostics).parse();
    unsigned object_queries{};
    LayoutQuery object_layout = [&](const TypePtr&) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        if (++object_queries == 1) {
            auto added = std::make_unique<ObjectDecl>();
            added->name = "published";
            added->type = builtin_type(BuiltinType::Uptr);
            added->initializer = copy_expression(*object_program.objects[0]->initializer);
            object_program.objects.reserve(object_program.objects.capacity() + 64);
            object_program.objects.push_back(std::move(added));
        }
        co_return 8;
    };
    expected = nullptr;
    const auto finalize_objects = [&]() -> ContinuationTask<bool> {
        co_await Witness{expected, continuous};
        co_return co_await finalize_target_constants_async(object_program, diagnostics, object_layout, object_layout);
    };
    require(finalize_objects().run() && continuous && object_queries == 2 &&
                object_program.objects.size() == 2 &&
                object_program.objects[0]->initializer->evaluated_integer &&
                object_program.objects[1]->initializer->evaluated_integer,
            "object publication invalidated the active owner or skipped its required proof");
}

void resource_recovery() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("finalization-resources.x",
        "uptr values[2] = { sizeof(uptr) + 0uptr, sizeof(u16) };");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
    program.evaluation_limits.steps = 1;
    ContinuationSchedule* expected{};
    bool continuous = true;
    std::vector<BuiltinType> queried;
    LayoutQuery layout = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        queried.push_back(type->builtin);
        co_return type->builtin == BuiltinType::Uptr ? 8 : 2;
    };
    const auto run = [&]() -> ContinuationTask<bool> {
        co_await Witness{expected, continuous};
        co_return co_await finalize_target_constants_async(program, diagnostics, layout, layout);
    };
    const auto epoch = program.evaluation_resource_errors;
    const auto result = run().run();
    if (result || diagnostics.errors() != 1 || program.evaluation_resource_errors != epoch + 1 || !queried.empty())
        std::cerr << "resource result=" << result << " errors=" << diagnostics.errors()
                  << " events=" << program.evaluation_resource_errors - epoch
                  << " queries=" << queried.size() << '\n' << messages.str();
    require(!result && diagnostics.errors() == 1 &&
                program.evaluation_resource_errors == epoch + 1 && queried.empty(),
            "resource failure continued a sibling proof or duplicated its diagnosis");
    require(!program.evaluation_required_integer && !program.evaluation_generic_value &&
                !program.evaluation_prepare_layout && !program.evaluation_layout_scope,
            "bounded finalization retained evaluator services");
    program.evaluation_limits.steps = 1000000;
    expected = nullptr;
    (void)run().run(); // The public status still includes the older diagnostic.
    const auto& entries = program.objects[0]->initializer->initializer_entries;
    require(continuous && diagnostics.errors() == 1 && queried ==
                std::vector<BuiltinType>{BuiltinType::Uptr, BuiltinType::U16} &&
                entries[0].value->evaluated_integer && entries[1].value->evaluated_integer,
            "a fresh walk inherited exhausted resources or changed older diagnostics");
}

void runtime_parameter() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("finalization-runtime-parameter.x",
        "u8 storage[64]; static u8 *patch(in u16 input) { return $::patch(storage + (uptr)input); }");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
    const LayoutQuery layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return 8; };
    require(!finalize_target_constants(program, diagnostics, layout, layout) && diagnostics.errors(),
            "patch proof borrowed a runtime parameter's value from its lexical metadata");
}
} // namespace

int main() {
    deep_expression();
    deep_initializer();
    deep_statements();
    publication_and_failure();
    resource_recovery();
    runtime_parameter();
    for (const unsigned bits : {32U, 64U})
        for (const auto order : {cross::EvaluationByteOrder::Little, cross::EvaluationByteOrder::Big})
            for (const unsigned alignment : {2U, 8U})
                for (const bool legacy : {false, true}) ordinary(bits, order, alignment, legacy);
}
