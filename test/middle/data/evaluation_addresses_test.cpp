// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "middle/data_ir.hpp"
#include "target/subtarget.hpp"

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

void check(std::string triple, std::string abi, bool legacy) {
    CompilerOptions options;
    options.target = triple;
    options.abi = abi;
    auto target = *target_for_triple(triple);
    const auto bits = find_abi(target, abi, triple)->address_bits;
    const UInt128 null{bits == 32 ? 0x12345678ULL : 0x123456789abcdef0ULL};
    target.address_spaces.front().null_low = null.low;
    auto alternate = target.address_spaces.front();
    alternate.number = 7;
    alternate.pointer_bits = bits;
    alternate.null_low ^= 0x10101010;
    target.address_spaces.push_back(alternate);
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition, const char* message,
        std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << triple << ':' << legacy << ':' << at.line() << ": " << message << '\n' << output.str();
            std::abort();
        }
    };
    auto subtarget = resolve_subtarget(target, options, diagnostics);
    require(subtarget.has_value(), "fixture subtarget failed");
    for (const auto& [expression, result] : std::vector<std::pair<std::string, UInt128>>{
            {"(uptr)(u32*)0uptr", null},
            {"(uptr)(u32 [[address_space(7)]]*)0uptr", UInt128{alternate.null_low}},
            {"(bool)(u32 [[address_space(7)]]*)0uptr", UInt128{}},
            {"(uptr)((u8*)0x1000uptr + 3uptr)", UInt128{0x1003}},
            {"observe(address())", UInt128{4}},
            {"observe(saved_address())", UInt128{4}},
            {"observe(&node.second)", UInt128{4}},
            {"observe_function(marker)", UInt128{1}}}) {
        const auto prior_errors = diagnostics.errors();
        const auto* source = sources.add("evaluation-addresses.x", R"(
            struct Node [[aligned(sizeof(uptr))]] { u32 first; u32 second; };
            [[aligned(sizeof(uptr))]] global u32 storage[8];
            global struct Node node;
            [[aligned(sizeof(uptr))]] static uptr marker() { return 1uptr; }
            static u32 *address() { return &storage[2]; }
            static u32 *saved_address() { u32 *value = address(); return *(&value); }
            static uptr observe(in u32 *value) { return sizeof(*value); }
            static uptr observe_function(in uptr (*value)()) { return 1uptr; }
            uptr result =
        )" + expression + ";");
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
        auto program = parser.parse();
        program.address_bits = bits;
        program.evaluation_layout.byte_order = target.data_layout.byte_order == ByteOrder::Little
            ? EvaluationByteOrder::Little : EvaluationByteOrder::Big;
        require(diagnostics.errors() == prior_errors, "fixture parse failed");
        ContinuationSchedule* expected{};
        bool continuous = true;
        bool throw_query = !legacy;
        bool symbol_seen{};
        unsigned addresses{}, integers{};
        LayoutQuery size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
            co_await Witness{expected, continuous};
            if (type->kind == Type::Kind::Pointer || type->builtin == BuiltinType::Uptr ||
                type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Label)
                co_return bits / 8;
            co_return type_bits(type) / 8;
        };
        LayoutQuery align_of = [](const TypePtr&) -> std::optional<std::uint64_t> { return 1; };
        program.evaluation_size_of = size_of;
        program.evaluation_align_of = align_of;
        program.evaluation_pointer_resolver = [&](std::unique_ptr<Expr>& value, const TypePtr& type,
            const FunctionDecl* caller, std::span<const NameKey> locals) -> ContinuationTask<bool> {
            co_await Witness{expected, continuous};
            ++addresses;
            const auto integer = program.evaluation_required_integer;
            require(static_cast<bool>(integer), "address proof lost its active evaluator");
            struct Restore {
                Program& program;
                EvaluationIntegerQuery integer;
                ~Restore() { program.evaluation_required_integer = std::move(integer); }
            } restore{program, integer};
            program.evaluation_required_integer = [&, integer](const Expr& node, Diagnostics& errors,
                const LayoutQuery& size, const LayoutQuery& align, std::string_view name_space,
                const FunctionDecl* owner, std::span<const std::pair<NameKey, TypePtr>> types,
                EvaluationIntegerContext context) -> ContinuationTask<EvaluationIntegerResult> {
                co_await Witness{expected, continuous};
                ++integers;
                if (throw_query) { throw_query = false; throw std::runtime_error("address layout failure"); }
                co_return co_await integer.async(node, errors, size, align, name_space, owner, types, context);
            };
            if (legacy)
                co_return data::normalize_generic_pointer(program, value, type, caller, locals,
                    options, *subtarget, diagnostics);
            const auto valid = co_await data::normalize_generic_pointer_async(program, value, type, caller, locals,
                options, *subtarget, diagnostics);
            if (valid && value->evaluated_address) {
                const auto& address = *value->evaluated_address;
                if (address.kind == AddressConstant::Kind::Object) {
                    require(address.object &&
                        ((address.object->name == "storage" && address.addend == 8) ||
                         (address.object->name == "node" && address.addend == 4)),
                        "symbolic object identity or subobject addend changed");
                    symbol_seen = true;
                } else if (address.kind == AddressConstant::Kind::Function) {
                    require(address.function && address.function->name == "marker" && address.addend == 0,
                            "symbolic function identity changed");
                    symbol_seen = true;
                }
            }
            co_return valid;
        };
        FunctionDecl context;
        context.source_unit = program.objects.back()->source_unit;
        const auto evaluate = [&]() -> ContinuationTask<std::optional<Expr::IntegerConstant>> {
            co_await Witness{expected, continuous};
            co_return co_await evaluate_target_integer_constant_async(program,
                *program.objects.back()->initializer, diagnostics, size_of, align_of, {}, &context);
        };
        const auto clean = [&] {
            require(!program.evaluation_required_integer && !program.evaluation_generic_value &&
                    !program.evaluation_prepare_layout && !program.evaluation_layout_scope &&
                    !program.evaluation_record_definition && !program.evaluation_member_layout &&
                    !program.evaluation_initializer_plan && !program.evaluation_initializer_types &&
                    !program.evaluation_atomic_is_lock_free,
                    "address query retained borrowed evaluator/layout state");
        };
        if (!legacy) {
            try {
                (void)evaluate().run();
                require(false, "address layout exception was swallowed");
            } catch (const std::runtime_error& error) {
                require(std::string_view(error.what()) == "address layout failure", "address layout exception changed");
            }
            clean();
            require(continuous && diagnostics.errors() == prior_errors, "exception path restarted the host pump");
            expected = nullptr;
        }
        const auto value = evaluate().run();
        require(value && value->value == result && diagnostics.errors() == prior_errors, "target address result changed");
        require(addresses && integers && continuous != legacy, "address queries did not use the expected scheduler");
        if (expression.starts_with("observe")) require(symbol_seen, "symbolic address never reached the target resolver");
        clean();
        if (legacy) break; // Prove the witness detects the synchronous wrapper.
        const auto resources = program.evaluation_resource_errors;
        program.evaluation_limits.steps = 2;
        expected = nullptr;
        const auto limited = evaluate().run();
        require(!limited && diagnostics.errors() == prior_errors + 1 && program.evaluation_resource_errors == resources + 1,
                "address proof did not report one resource failure");
        clean();
        program.evaluation_limits.steps = 1000000;
        expected = nullptr;
        const auto fresh = evaluate().run();
        require(fresh && fresh->value == result && diagnostics.errors() == prior_errors + 1 && continuous,
                "fresh address proof inherited failed state");
        clean();
    }
}
}

int main() {
    for (const auto& [triple, abi] : {
            std::pair{"x86_64-unknown-linux-gnu", "sysv_abi"},
            std::pair{"mips-unknown-elf", "o32"},
            std::pair{"mipsel-unknown-elf", "o32"},
            std::pair{"mips64-unknown-elf", "n64"},
            std::pair{"mips64el-unknown-elf", "n64"}}) {
        check(triple, abi, true);
        check(triple, abi, false);
    }
}
