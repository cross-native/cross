// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "middle/data_ir.hpp"
#include "middle/mir.hpp"
#include "target/subtarget.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <sstream>

int main() {
    using namespace cross;
    for (const auto& [triple, abi] : {
            std::pair{"x86_64-unknown-linux-gnu", "sysv_abi"},
            std::pair{"mips-unknown-elf", "o32"},
            std::pair{"mipsel-unknown-elf", "o32"},
            std::pair{"mips64-unknown-elf", "n64"},
            std::pair{"mips64el-unknown-elf", "n64"}}) {
        std::ostringstream output;
        Diagnostics diagnostics(output);
        const auto require = [&](bool condition,
            std::source_location where = std::source_location::current()) {
            if (!condition) {
                std::cerr << triple << ':' << where.line() << '\n' << output.str();
                std::abort();
            }
        };
        CompilerOptions options;
        options.target = triple;
        options.abi = abi;
        auto target = *target_for_triple(triple);
        const auto bits = find_abi(target, abi, triple)->address_bits;
        const auto null = bits == 32 ? UInt128{0x12345678} : UInt128{0x123456789abcdef0};
        target.address_spaces.front().null_low = null.low;
        auto alternate = target.address_spaces.front();
        alternate.number = 7;
        alternate.pointer_bits = bits;
        alternate.null_low ^= 0x10101010;
        target.address_spaces.push_back(alternate);
        auto subtarget = resolve_subtarget(target, options, diagnostics);
        require(subtarget.has_value());

        SourceManager sources;
        const auto* source = sources.add("null_pointer.x", R"(
            static u32 *scalar = 0;
            static u32 *computed = 17u32 - 17u32;
            static void (*function)() = 0u128;
            static u32 [[address_space(7)]] *numbered = 0u8;
            struct Cell { u8 prefix; u32 *value; u32 [[address_space(7)]] *other; };
            static struct Cell nested = {19u8, 0, 0};
            static u32 *array[2] = {0, (0u64)};
            static u32 *absolute = 0;
            static u8 helper_zero(in u32 value) { return (u8)(value - value); }
            global bool compare(in u32 *pointer) { return pointer == (7u32 - 7u32); }
            global bool compare_numbered(in u32 [[address_space(7)]] *pointer) { return 0u64 != pointer; }
            global bool compare_function(in void (*pointer)()) { return pointer == 0; }
            global u32 *cast_object() { return (u32 *)0u8; }
            global void (*cast_function())() { return (void (*)())0u32; }
            global u32 [[address_space(7)]] *cast_numbered() { return (u32 [[address_space(7)]] *)(7u16 - 7u16); }
            global u32 *cast_wide() { return (u32 *)0u128; }
            enum Zero [[underlying(u8)]] { zero = 0u8 };
            global u32 *cast_enum() { return (u32 *)zero; }
            global u32 *choose_object(in u32 select, in u32 *pointer) { return select ? pointer : 0u8; }
            global u32 [[address_space(7)]] *choose_numbered(in u32 select,
                in u32 [[address_space(7)]] *pointer) { return select ? 0u128 : pointer; }
            global void (*choose_function(in u32 select, in void (*pointer)()))() { return select ? pointer : 0u32; }
            global u32 truth_select(in u32 *pointer) { return pointer ? 11u32 : 17u32; }
            global bool truth_cast(in u32 [[address_space(7)]] *pointer) { return (bool)pointer; }
            global bool truth_not(in u32 [[address_space(7)]] *pointer) { return !pointer; }
            global bool truth_function(in void (*pointer)()) { return (bool)pointer; }
            global u32 truth_branch(in u32 *pointer) { if (pointer) return 11u32; return 17u32; }
            global bool compare_helper(in u32 *pointer) { return pointer == helper_zero(3u32); }
            global bool compare_numbered_helper(in u32 [[address_space(7)]] *pointer) { return helper_zero(5u32) != pointer; }
            global bool compare_function_helper(in void (*pointer)()) { return helper_zero(7u32) == pointer; }
            global u32 *cast_object_helper() { return (u32 *)helper_zero(11u32); }
            global void (*cast_function_helper())() { return (void (*)())helper_zero(13u32); }
            global u32 [[address_space(7)]] *cast_numbered_helper() { return (u32 [[address_space(7)]] *)helper_zero(17u32); }
        )");
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
        auto program = parser.parse();
        program.address_bits = bits;
        require(diagnostics.errors() == 0);
        require(expand_semantics(program, diagnostics, false, "default", abi));
        auto& absolute = *program.objects.back()->initializer;
        absolute.kind = Expr::Kind::Address;
        absolute.type = program.objects.back()->type;
        absolute.evaluated_integer.reset();
        absolute.evaluated_address = AddressConstant{};
        auto module = hir::build(program, options, target, diagnostics);
        require(diagnostics.errors() == 0);
        auto data = data::lower(module, *subtarget, diagnostics);
        require(diagnostics.errors() == 0 && data.objects.size() == 7);
        const auto read = [&](const data::Object& object, std::size_t offset) {
            UInt128 value;
            for (unsigned index = 0; index < bits / 8; ++index) {
                const auto shift = target.data_layout.byte_order == ByteOrder::Little
                    ? index * 8 : bits - 8 - index * 8;
                value = bit_or(value, shift_left(UInt128{object.bytes.at(offset + index)}, shift));
            }
            return value;
        };
        for (const auto& object : data.objects) {
            require(object.relocations.empty() && !object.address);
            const auto& entity = module.object(object.source);
            if (entity.source_name == "nested") {
                const auto& record = module.record(*module.type(entity.type).record);
                require(object.bytes.front() == 19);
                require(read(object, record.members[1].offset) == null);
                require(read(object, record.members[2].offset) == UInt128{alternate.null_low});
            } else if (entity.source_name == "array") {
                require(read(object, 0) == null && read(object, bits / 8) == null);
            } else {
                require(object.initializer == data::InitializerKind::Integer);
                require(object.bits == (entity.source_name == "absolute" ? UInt128{} : entity.source_name == "numbered"
                    ? UInt128{alternate.null_low} : null));
            }
        }
        const auto managed = mir::lower_managed(module, *subtarget, options, diagnostics);
        require(diagnostics.errors() == 0 && managed.functions.size() == 23);
        require(mir::verify(managed, module, diagnostics));
        for (const auto& function : managed.functions) {
            unsigned null_constants{};
            for (const auto& value : function.values) {
                const auto& type = module.type(value.type);
                if (value.kind != mir::ValueKind::ConstantInteger || type.kind != hir::Type::Kind::Pointer)
                    continue;
                ++null_constants;
                require(UInt128{value.integer, value.integer_high} ==
                    (type.address_space == 7 ? UInt128{alternate.null_low} : null));
            }
            const auto expected_count = module.function(function.source).source_name == "helper_zero" ? 0U : 1U;
            if (null_constants != expected_count)
                std::cerr << "null constants: " << null_constants << '\n';
            require(null_constants == expected_count);
        }
        // Required address formation and explicit integer casts use the same
        // selected representation, including a nonzero null in another space.
        const LayoutQuery size_of = [&](const TypePtr& type) {
            return hir::layout_size(module, module.intern_type(type), target);
        };
        const LayoutQuery align_of = [&](const TypePtr& type) {
            return hir::layout_alignment(module, module.intern_type(type), target);
        };
        for (const auto& [expression, expected] : std::vector<std::pair<std::string, UInt128>>{
                {"(uptr)(u32*)0uptr", null},
                {"(uptr)(u32*)helper_zero(7u32)", null},
                {"(uptr)(u32 [[address_space(7)]] *)helper_zero(11u32)", UInt128{alternate.null_low}},
                {"((u32*)0uptr) == helper_zero(3u32)", UInt128{1}},
                {"((u32*)0uptr) == pointer_zero(&*((u32*)0uptr))", UInt128{1}},
                {"((u32*)0uptr) == pointer_zero((const u32*)0x1000uptr)", UInt128{1}},
                {"((u32 [[address_space(7)]] *)0uptr) == helper_zero(5u32)", UInt128{1}},
                {"((u32 [[address_space(7)]] *)0uptr) == numbered_zero(&*((u32 [[address_space(7)]] *)0uptr))", UInt128{1}},
                {"(bool)(u32*)helper_zero(13u32)", UInt128{}},
                {"(uptr)&*((u32*)0uptr)", null},
                {"(uptr)(u32 [[address_space(7)]] *)0uptr", UInt128{alternate.null_low}},
                {"(uptr)&*((u32 [[address_space(7)]] *)0uptr)", UInt128{alternate.null_low}},
                {"(uptr)&*((u32*)0x1000uptr)", UInt128{0x1000}},
                {"((u32*)0uptr) == 0u32", UInt128{1}},
                {"((u32 [[address_space(7)]] *)0uptr) == 0u32", UInt128{1}},
                {"(bool)(u32*)0uptr", UInt128{}},
                {"(uptr)((u8*)1uptr - 1u32)", UInt128{}},
                {"(bool)((u8*)1uptr - 1u32)", UInt128{1}},
                {"((u8*)1uptr - 1u32) == 0u32", UInt128{}},
                {"saved_null()", UInt128{}},
                {"saved_bits()", null},
                {"!((u32 [[address_space(7)]] *)0uptr)", UInt128{1}},
                {"(u32*)0uptr ? 11u32 : 17u32", UInt128{17}}}) {
            const auto* probe_source = sources.add("null_evaluation.x",
                "static u8 helper_zero(in u32 value) { return (u8)(value - value); }"
                "static u8 pointer_zero(in const u32 *value) { return 0u8; }"
                "static u8 numbered_zero(in const u32 [[address_space(7)]] *value) { return 0u8; }"
                "static bool saved_null() { u32 *value = 0; return (bool)*(&value); }"
                "static uptr saved_bits() { u32 *value = 0; return (uptr)*(&value); }"
                "uptr result = " + expression + ";");
            Parser probe_parser(Lexer(*probe_source, diagnostics).lex(), diagnostics, {}, bits);
            auto probe = probe_parser.parse();
            probe.address_bits = bits;
            probe.evaluation_layout.byte_order = target.data_layout.byte_order == ByteOrder::Little
                ? EvaluationByteOrder::Little : EvaluationByteOrder::Big;
            probe.evaluation_pointer_resolver = [&](std::unique_ptr<Expr>& value, const TypePtr& type,
                const FunctionDecl* caller, std::span<const NameKey> locals) {
                return data::normalize_generic_pointer_async(probe, value, type, caller, locals,
                                                        options, *subtarget, diagnostics);
            };
            require(diagnostics.errors() == 0 && probe.objects.size() == 1);
            if (expression == "((u32*)0uptr) == pointer_zero((const u32*)0x1000uptr)") {
                // Already normalized addresses must stay eligible too; no host
                // pointer or default-null assumption enters this source proof.
                auto& argument = *probe.objects.front()->initializer->right->arguments.front();
                argument.kind = Expr::Kind::Address;
                argument.left.reset();
                argument.evaluated_address = AddressConstant{};
                argument.evaluated_address->absolute = UInt128{0x1000};
            }
            const auto value = evaluate_target_integer_constant(probe, *probe.objects.front()->initializer,
                                                                 diagnostics, size_of, align_of);
            require(value && value->value == expected && diagnostics.errors() == 0);
        }
        // Reject registry encodings that cannot fit, instead of truncating them.
        target.address_spaces.front().null_high = 1;
        std::ostringstream rejected;
        Diagnostics invalid(rejected);
        (void)data::lower(module, *subtarget, invalid);
        require(invalid.errors() != 0 && rejected.str().find(
            "null pointer representation does not fit the destination storage") != std::string::npos);
        Diagnostics invalid_mir(rejected);
        auto invalid_module = hir::build(program, options, target, invalid_mir);
        (void)mir::lower_managed(invalid_module, *subtarget, options, invalid_mir);
        require(rejected.str().find("null pointer representation does not fit the pointer type") != std::string::npos);
        target.address_spaces.front().null_high = 0;
        for (const bool unavailable : {false, true}) {
            auto& space = target.address_spaces.front();
            space.pointer_bits = unavailable ? bits : bits * 2;
            space.native_lowering = !unavailable;
            std::ostringstream rejected_representation;
            Diagnostics invalid_representation(rejected_representation);
            auto rejected_module = hir::build(program, options, target, invalid_representation);
            (void)mir::lower_managed(rejected_module, *subtarget, options, invalid_representation);
            require(invalid_representation.errors() != 0 && rejected_representation.str().find(unavailable
                ? "pointer conversion has no native address-space representation"
                : "null pointer representation does not fit the pointer type") != std::string::npos);
        }
    }
}
