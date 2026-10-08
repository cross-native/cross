// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "middle/hir.hpp"
#include "target/subtarget.hpp"

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

void check(std::string triple, std::string abi, bool custom, bool synchronous) {
    CompilerOptions options;
    options.target = triple;
    options.abi = abi;
    auto target = *target_for_triple(triple);
    const auto bits = find_abi(target, abi, triple)->address_bits;
    if (custom) target.data_layout.natural_alignment_limit = 2;
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition, const char* message,
        std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << triple << ':' << custom << ':' << synchronous << ':' << at.line()
                      << ": " << message << '\n' << output.str();
            std::abort();
        }
    };
    const auto* source = sources.add("evaluation-hir.x", R"(
        struct Box [[aligned(1uptr + 0uptr)]] { u8 head; uptr tail; };
        [[aligned(1uptr + 0uptr)]] static uptr first_index() {
            struct Box object = {};
            return sizeof(object) / sizeof(struct Box);
        }
        static uptr second_index() { return $::alignof(uptr) / $::alignof(uptr); }
        [[aligned(1uptr + 0uptr)]] global uptr cells[2][2];
        global uptr entry() { return $::patch(7uptr, cells[first_index()][second_index()]); }
    )");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    program.evaluation_layout = {
        target.data_layout.byte_order == ByteOrder::Big ? EvaluationByteOrder::Big : EvaluationByteOrder::Little,
        target.data_layout.natural_alignment_limit, target.data_layout.f80_storage_bytes,
        target.data_layout.f80_alignment, target.data_layout.code_addresses};
    require(!diagnostics.errors() && program.records.size() == 1 && program.functions.size() == 3,
            "fixture failed to parse");
    ContinuationSchedule* expected{};
    bool continuous = true, throw_index{}, exhaust_index{};
    unsigned requirements{}, layouts{}, fallback{};
    std::vector<std::string> indices;
    // These providers must be restored after every Builder-owned service scope,
    // including a query exception or typed resource failure.
    program.evaluation_member_layout = [&](const TypePtr&, const MemberName&)
        -> ContinuationTask<std::optional<EvaluationMemberLayout>> { ++fallback; co_return {}; };
    program.evaluation_initializer_plan = [&](const Expr&, const TypePtr&)
        -> ContinuationTask<EvaluationInitializerPlan> { ++fallback; co_return {}; };
    program.evaluation_required_integer = [&](const Expr& expression, Diagnostics& active,
        const LayoutQuery& size_of, const LayoutQuery& align_of, std::string_view name_space,
        const FunctionDecl* caller, std::span<const std::pair<NameKey, TypePtr>> locals,
        EvaluationIntegerContext context) -> ContinuationTask<EvaluationIntegerResult> {
        co_await Witness{expected, continuous};
        ++requirements;
        const bool index = expression.kind == Expr::Kind::Call && expression.left &&
            expression.left->kind == Expr::Kind::Name && expression.left->text.ends_with("_index");
        if (index) {
            indices.push_back(expression.left->text);
            if (throw_index) { throw_index = false; throw std::runtime_error("index query failure"); }
            if (exhaust_index) {
                exhaust_index = false;
                ++program.evaluation_resource_errors;
                active.error(expression.location, "forced index resource failure");
                co_return {};
            }
        }
        const LayoutQuery size = [&](const TypePtr& type)
            -> ContinuationTask<std::optional<std::uint64_t>> {
            co_await Witness{expected, continuous};
            ++layouts;
            co_return co_await size_of.async(type);
        };
        const LayoutQuery alignment = [&](const TypePtr& type)
            -> ContinuationTask<std::optional<std::uint64_t>> {
            co_await Witness{expected, continuous};
            ++layouts;
            co_return co_await align_of.async(type);
        };
        struct Restore {
            Program& program;
            EvaluationIntegerQuery query;
            ~Restore() { program.evaluation_required_integer = std::move(query); }
        } restore{program, std::move(program.evaluation_required_integer)};
        co_return co_await evaluate_target_integer_requirement_async(program, expression, active,
            size, alignment, name_space, caller, locals, context);
    };
    const auto build = [&]() -> ContinuationTask<hir::Module> {
        co_await Witness{expected, continuous};
        // This intentional compatibility-boundary control must detect a second
        // pump. Only the awaited entry is valid inside an existing continuation.
        if (synchronous) co_return hir::build(program, options, target, diagnostics);
        co_return co_await hir::build_async(program, options, target, diagnostics);
    };
    const auto reset = [&]() {
        expected = nullptr;
        continuous = true;
        indices.clear();
        auto& sink = *program.functions.back()->body->statements.front()->expression->arguments[1];
        sink.right->evaluated_integer.reset();
        sink.left->right->evaluated_integer.reset();
    };
    const auto clean = [&]() {
        require(program.evaluation_required_integer && program.evaluation_member_layout &&
                program.evaluation_initializer_plan && !program.evaluation_initializer_types &&
                !program.evaluation_prepare_layout && !program.evaluation_layout_scope &&
                !program.evaluation_generic_value && !program.evaluation_record_definition,
                "Builder/evaluator retained or removed a borrowed service");
        require(!fallback, "a coherent Builder view fell back to an unrelated layout");
    };
    const auto verify = [&](const hir::Module& module) {
        const auto* box = module.record(program.records.front().nominal_key());
        const auto alignment = std::min(bits / 8, target.data_layout.natural_alignment_limit);
        require(box && box->complete, "record layout was not completed");
        const auto tail = module.member(box->id, MemberName{"tail", {}});
        require(box->alignment == alignment &&
                box->size == alignment + bits / 8 && tail && tail->offset == alignment,
                "model-owned record layout changed");
        require(indices == std::vector<std::string>{"first_index", "second_index"},
                "sink index proofs changed source order or were repeated");
        const auto& sink = *program.functions.back()->body->statements.front()->expression->arguments[1];
        require(sink.right->evaluated_integer && sink.left->right->evaluated_integer &&
                sink.right->evaluated_integer->value == UInt128{1} &&
                sink.left->right->evaluated_integer->value == UInt128{1},
                "helper/layout-produced index proof was not retained");
        require(requirements && layouts, "required-value/layout witness was not exercised");
        clean();
    };
    const auto module = build().run();
    require(!diagnostics.errors(), "connected HIR proof failed");
    verify(module);
    require(continuous != synchronous, "the scheduler witness did not distinguish the outer boundary");
    if (synchronous) return;

    reset();
    throw_index = true;
    try {
        (void)build().run();
        require(false, "index query exception was swallowed");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "index query failure", "query exception changed");
    }
    require(continuous && indices == std::vector<std::string>{"first_index"},
            "exception continued an outer index or restarted the pump");
    clean();
    reset();
    verify(build().run());
    require(continuous && !diagnostics.errors(), "fresh Builder did not recover after an exception");

    reset();
    exhaust_index = true;
    const auto resources = program.evaluation_resource_errors;
    (void)build().run();
    require(continuous && indices == std::vector<std::string>{"first_index"} &&
            program.evaluation_resource_errors == resources + 1 && diagnostics.errors() == 1,
            "a resource failure repeated or evaluated an outer sink index");
    clean();
    reset();
    verify(build().run());
    require(continuous && program.evaluation_resource_errors == resources + 1 && diagnostics.errors() == 1,
            "fresh Builder retained failure state or hid an earlier diagnostic");

    // The early complete-record entry must be composable on that same pump.
    reset();
    const auto records = [&]() -> ContinuationTask<hir::Module> {
        co_await Witness{expected, continuous};
        co_return co_await hir::build_record_layout_context_async(program, options, target, diagnostics);
    };
    const auto layout = records().run();
    require(continuous && layout.record(program.records.front().nominal_key())->complete,
            "early record completion restarted the host pump");
    clean();
}
}

int main() {
    for (const auto& [triple, abi] : std::vector<std::pair<std::string, std::string>>{
        {"x86_64-unknown-linux-gnu", "sysv_abi"}, {"mips-unknown-elf", "o32"},
        {"mipsel-unknown-elf", "o32"}, {"mips64-unknown-elf", "n64"}, {"mips64el-unknown-elf", "n64"}}) {
        check(triple, abi, false, true);
        check(triple, abi, false, false);
    }
    check("x86_64-unknown-linux-gnu", "sysv_abi", true, true);
    check("x86_64-unknown-linux-gnu", "sysv_abi", true, false);
}
