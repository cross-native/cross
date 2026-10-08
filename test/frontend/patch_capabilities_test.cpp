// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "common/relocation_addend.hpp"
#include "model/model.hpp"
#include "target/instruction_constraints.hpp"
#include "target/subtarget.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
using namespace cross;

void whole_instruction_forms() {
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    CompilerOptions options;
    options.target = "x86_64-unknown-linux-gnu";
    if (!configure_models(options, diagnostics)) std::abort();
    const auto* base = target_for_triple(options.target);
    const auto selected = resolve_subtarget(*base, options, diagnostics);
    if (!selected) std::abort();
    TargetInfo target;
    target.architecture = "test";
    RegisterEntry first{"unusual", "bank7", 64, "custom", "", false, {}, false};
    first.instruction_scalar_modes = {{64, false}};
    target.registers.push_back(first);
    auto second = first;
    second.name = "another";
    second.storage = "bank9";
    target.registers.push_back(second);
    InstructionOperandEntry output;
    output.role = InstructionOperandRole::Output;
    output.allow_register = true;
    output.register_bits = 64;
    output.register_class = "custom";
    output.register_storage = "bank7";
    InstructionOperandEntry field;
    field.allow_immediate = true;
    field.patchable = true;
    field.immediate_bits = 64;
    target.instructions.emplace_back("$::_choose", "", "unused",
        std::vector{output, field, field}, std::vector<std::string_view>{},
        std::vector<std::string_view>{}, 0, InstructionControlEffect::None);
    output.register_storage = "bank9";
    target.instructions.emplace_back("$::_choose", "", "unused",
        std::vector{output, field, field}, std::vector<std::string_view>{},
        std::vector<std::string_view>{}, 0, InstructionControlEffect::None);
    std::vector<EvaluationInstructionOperand> operands(3);
    operands[0].kind = EvaluationInstructionOperand::Kind::Register;
    operands[0].fixed_register = "unusual";
    operands[0].writable = true;
    for (const std::size_t i : {1U, 2U}) {
        operands[i].kind = EvaluationInstructionOperand::Kind::Patch;
        operands[i].patch_forms = {{0}};
    }
    const auto check_forms = [&](bool expected) {
        const auto reason = instruction_source_error(target, *selected, "$::_choose", operands);
        if (reason.has_value() == expected) {
            std::cerr << "whole-form candidate isolation failed: " << reason.value_or("accepted") << '\n';
            std::exit(1);
        }
    };
    check_forms(true);
    // Matching patch rows cannot borrow the other row's destination register.
    operands[1].patch_forms = {{1}};
    check_forms(false);
    operands[0].fixed_register = "another";
    check_forms(false); // Nor can two patch operands select different rows.
    operands[2].patch_forms = {{1}};
    check_forms(true);
    operands[1].deferred = true;
    operands[0].fixed_register = "unusual";
    check_forms(false); // An unknown initial never hides a known register error.
    operands[0].fixed_register = "another";
    operands[0].writable = false;
    check_forms(false);
}

void source_operand_widths(unsigned bits) {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("raw-source-widths.x", R"(
        static $::meta::tokens helper(in $::meta::tokens input) {
            uptr value;
            typedef uptr V [[ext_vector_type(2)]];
            V vector;
            uptr *pointer;
            if (0u32) { $::_field(value); $::_field(vector); $::_field(pointer); }
            return input;
        }
    )");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics);
    auto program = parser.parse();
    program.address_bits = bits;
    // An intentionally distinct pointer-storage service catches equating a
    // pointer operand's representation with the language's uptr width.
    program.evaluation_size_of = [bits](const TypePtr& type) -> std::optional<std::uint64_t> {
        return type->kind == Type::Kind::Pointer ? 16U : bits / 8U;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    unsigned queries{};
    program.evaluation_instruction_source = [&](std::string_view name,
        std::span<const EvaluationInstructionOperand> operands) -> std::optional<std::string> {
        if (name != "$::_field" || operands.size() != 1 || !operands[0].type ||
            operands[0].kind != EvaluationInstructionOperand::Kind::Register) std::abort();
        const auto& operand = operands[0];
        const unsigned expected = operand.type->kind == Type::Kind::Vector ? bits * 2
            : operand.type->kind == Type::Kind::Pointer ? 128 : bits;
        if (operand.bits != expected) std::abort();
        ++queries;
        return {};
    };
    ExpansionSemantics semantics(program, diagnostics, "default", {});
    if (diagnostics.errors() || !semantics.validate_source(*program.functions.front()) || queries != 3) {
        std::cerr << "source instruction width isolation failed\n" << messages.str();
        std::exit(1);
    }
}

void label_scopes() {
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    CompilerOptions options;
    options.target = "x86_64-unknown-linux-gnu";
    if (!configure_models(options, diagnostics)) std::abort();
    const auto selected = resolve_subtarget(*target_for_triple(options.target), options, diagnostics);
    if (!selected) std::abort();
    TargetInfo target;
    InstructionOperandEntry field;
    field.allow_label = true;
    field.label_scope = InstructionLabelScope::SameFunction;
    target.instructions.emplace_back("$::_transfer", "", "unused",
        std::vector{field}, std::vector<std::string_view>{},
        std::vector<std::string_view>{}, 0, InstructionControlEffect::UnconditionalBranch);
    EvaluationInstructionOperand operand;
    operand.kind = EvaluationInstructionOperand::Kind::Label;
    const auto check_scope = [&](bool accepted) {
        if (instruction_source_error(target, *selected, "$::_transfer", {&operand, 1}).has_value() == accepted) {
            std::cerr << "instruction label scope constraint failed\n";
            std::exit(1);
        }
    };
    check_scope(false);
    operand.label_same_function = true;
    check_scope(true);
    // A hypothetical target can register broader visibility independently of
    // the x86 direct-branch restriction. This does not claim such an encoding.
    target.instructions.front().operands.front().label_scope = InstructionLabelScope::AnyVisible;
    operand.label_same_function = false;
    check_scope(true);
    target.instructions.front().operands.front().allow_label = false;
    check_scope(false);
}

void address_modes() {
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    CompilerOptions options;
    options.target = "x86_64-unknown-linux-gnu";
    if (!configure_models(options, diagnostics)) std::abort();
    const auto* base = target_for_triple(options.target);
    const auto selected = resolve_subtarget(*base, options, diagnostics);
    if (!selected) std::abort();
    TargetInfo target;
    RegisterEntry reg{"base", "b", 32, "address", "", true, {}, false};
    target.registers.push_back(reg);
    reg.name = "index"; reg.storage = "i";
    target.registers.push_back(reg);
    target.instruction_address_modes = {{32, 32, "address", {17}, {1, 3}, {}, 9, ""}};
    EvaluationInstructionOperand::Memory memory;
    memory.shape = EvaluationInstructionOperand::Memory::Shape::Index;
    memory.base.type = pointer_type(builtin_type(BuiltinType::U8));
    memory.base.type->address_space = 17;
    memory.base.object = true;
    memory.base.bits = 32;
    memory.base.fixed = "base";
    memory.index.type = builtin_type(BuiltinType::U32);
    memory.index.object = true;
    memory.index.integer = true;
    memory.index.bits = 32;
    memory.index.fixed = "index";
    memory.element_bytes = 3;
    const auto check_address = [&](bool accepted, unsigned scale = 1, std::int64_t displacement = 0) {
        const auto result = select_instruction_address(target, *selected, memory);
        if (result.error.empty() != accepted ||
            (accepted && (result.scale != scale || relocation_addend_i64(result.displacement) != displacement))) {
            std::cerr << "custom instruction address constraints failed: " << result.error << '\n';
            std::exit(1);
        }
    };
    check_address(true, 3);
    memory.element_bytes = 4;
    check_address(false);
    memory.layout_deferred = true;
    memory.element_bytes.reset();
    check_address(true);
    memory.index.fixed = "missing";
    check_address(false); // Layout deferral does not hide register constraints.
    memory.index.fixed = "index";
    memory.layout_deferred = false;
    memory.element_bytes = 3;
    memory.index.object = false;
    memory.constant_index = Expr::IntegerConstant{UInt128{85}, BuiltinType::U128};
    memory.constant_bits = 128;
    check_address(true, 1, 255);
    memory.constant_index->value = UInt128{86};
    check_address(false);
    memory.constant_index = Expr::IntegerConstant{UInt128{static_cast<std::uint64_t>(-85)}, BuiltinType::I64};
    memory.constant_bits = 64;
    memory.constant_signed = true;
    check_address(true, 1, -255);
    memory.constant_index->value = UInt128{static_cast<std::uint64_t>(-86)};
    check_address(false);
    memory.constant_index->value = UInt128{0};
    memory.base.type->address_space = 0;
    check_address(false);
    memory.base.type->address_space = 17;
    memory.base.bits = 64;
    check_address(false);
}

void check(unsigned bits, std::string_view initial, bool sink,
           std::vector<EvaluationPatchValueCapabilities> candidates, std::string_view expected,
           std::string_view sink_type = "uptr", bool raw = true, unsigned relocation_bits = 64) {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("patch-capabilities.x",
        "static u32 object; static " + std::string(sink_type) + " sink;"
        "struct Record { u8 prefix; u32 member; u32 values[2]; }; static struct Record record;"
        "static u32 matrix[2][3];"
        "static $::meta::tokens helper(in $::meta::tokens input) {"
        "if (0u32) " + (raw ? std::string("$::_field(0u64, ") : std::string{}) +
        "$::patch(" + std::string(initial) + (sink ? ", sink" : "") +
        (raw ? "));" : ");") + " return input; }");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics);
    auto program = parser.parse();
    program.address_bits = bits;
    // Enable normal pointer typing, but source-only symbolic proofs must not
    // resolve emitted storage or depend on a target's physical address bits.
    program.evaluation_pointer_resolver = [](std::unique_ptr<Expr>&, const TypePtr&,
                                            const FunctionDecl*, std::span<const NameKey>) {
        std::abort();
        return false;
    };
    unsigned queries{};
    program.evaluation_patch_operand_capabilities = [&](std::string_view name, std::size_t index,
        std::size_t count, const TypePtr& type) {
        if (name != "$::_field" || index != 1 || count != 2 || !type ||
            type->kind != Type::Kind::Builtin || type->builtin != BuiltinType::Uptr) std::abort();
        ++queries;
        return candidates;
    };
    // A raw field must not inherit a generic materializer requirement.
    program.evaluation_patch_value_capabilities = [&](const TypePtr&)
        -> std::optional<EvaluationPatchValueCapabilities> {
        if (raw) return {};
        ++queries;
        return candidates.empty() ? std::nullopt : std::optional{candidates.front()};
    };
    program.evaluation_size_of = [&program, bits](const TypePtr& type) -> std::optional<std::uint64_t> {
        if (type && type->kind == Type::Kind::Record) return 16;
        if (type && type->kind == Type::Kind::Array) {
            const auto element = program.evaluation_size_of(type->element);
            return element ? std::optional{*element * type->lanes} : std::nullopt;
        }
        if (type && (type->kind == Type::Kind::Pointer ||
                (type->kind == Type::Kind::Builtin &&
                    (type->builtin == BuiltinType::Uptr || type->builtin == BuiltinType::Iptr)))) return bits / 8;
        return type_bits(type) / 8;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    program.evaluation_member_layout = [](const TypePtr& type, const MemberName& member)
        -> std::optional<EvaluationMemberLayout> {
        // Deliberately unlike the shipped layout's offset four. The source
        // proof must retain these service-supplied layout facts.
        if (type && type->kind == Type::Kind::Record && member == MemberName{"member", {}})
            return EvaluationMemberLayout{12, 4, {}, 0};
        if (type && type->kind == Type::Kind::Record && member == MemberName{"values", {}})
            return EvaluationMemberLayout{4, 4, {}, 0};
        return {};
    };
    program.evaluation_relocation_addend = [relocation_bits](const RelocationAddend& addend) {
        return relocation_addend_fits_signed(addend, relocation_bits);
    };
    ExpansionSemantics semantics(program, diagnostics, "default", {});
    const bool valid = diagnostics.errors() == 0 && program.functions.size() == 1 &&
        semantics.validate_source(*program.functions.front());
    if (!queries || valid != expected.empty() ||
        (!expected.empty() && messages.str().find(expected) == std::string::npos)) {
        std::cerr << "patch candidate constraint failed: " << initial << " sink=" << sink
                  << " expected=" << expected << '\n' << messages.str();
        std::exit(1);
    }
}
}

int main() {
    label_scopes();
    whole_instruction_forms();
    source_operand_widths(32);
    source_operand_widths(64);
    address_modes();
    const auto uptr = builtin_type(BuiltinType::Uptr);
    const auto u32 = builtin_type(BuiltinType::U32);
    const auto u64 = builtin_type(BuiltinType::U64);
    for (const unsigned bits : {32U, 64U}) {
        check(bits, "1uptr", false, {{nullptr, false}}, {});
        check(bits, "1uptr", true, {{nullptr, false}}, "does not support an address sink");
        check(bits, "1uptr", true, {{uptr, false}}, {});
        check(bits, "(uptr)&object", false, {{nullptr, false}}, "cannot encode a symbol relocation");
        check(bits, "(uptr)&object", false, {{nullptr, true}}, {});
        check(bits, "(uptr)&object", true, {{uptr, false}, {nullptr, true}}, "cannot encode a symbol relocation");
        check(bits, "(uptr)&object", true, {{uptr, false}, {uptr, true}}, {});
        check(bits, "1uptr", false, {}, "no patch field accepting exactly");
        // Deliberately non-native sink types test the semantic service, not a
        // claim of target scalar/relocation implementation. No frontend uptr
        // or equal-width shortcut may replace the supplied type requirement.
        for (const bool raw : {false, true}) {
            check(bits, "1uptr", true, {{u32, false}}, {}, "u32", raw);
            check(bits, "1uptr", true, {{u64, false}}, {}, "u64", raw);
            check(bits, "1uptr", true, {{u64, false}}, "non-atomic u64 subobject", "uptr", raw);
            check(bits, "1uptr", true, {{uptr, false}}, "non-atomic uptr subobject", "u64", raw);
        }
        check(bits, "(uptr)&object", true, {{u64, false}, {uptr, true}},
            "cannot encode a symbol relocation", "u64");
        check(bits, "(uptr)&object", true, {{u64, true}, {uptr, false}}, {}, "u64");
        check(bits, "1uptr", true, {{u64, false}, {uptr, false}},
            "target-registered patch-address subobject", "u32");
        for (const bool raw : {false, true}) {
            check(bits, "(uptr)((uptr)&object + 0x80000000u64)", false, {{nullptr, true}},
                "exceeds the supported relocation range", "uptr", raw, 32);
            check(bits, "(uptr)((uptr)&object + 0x80000000u64)", false, {{nullptr, true}},
                {}, "uptr", raw, 64);
            check(bits, "(uptr)((uptr)&object + 0x10000000000000000u128)", false, {{nullptr, true}},
                {}, "uptr", raw, 128);
            check(bits, "(uptr)(&object + 1u32)", false, {{nullptr, true}}, {}, "uptr", raw, 4);
            check(bits, "(uptr)(&object + 2u32)", false, {{nullptr, true}},
                "exceeds the supported relocation range", "uptr", raw, 4);
            check(bits, "(uptr)&object + (uptr)-1i32", false, {{nullptr, true}},
                bits == 32 ? "" : "exceeds the supported relocation range", "uptr", raw, 64);
            check(bits, "(uptr)&record.member", false, {{nullptr, true}},
                "exceeds the supported relocation range", "uptr", raw, 4);
            check(bits, "(uptr)&record.member", false, {{nullptr, true}}, {}, "uptr", raw, 5);
            check(bits, "(uptr)(&record + 1u32)", false, {{nullptr, true}},
                "exceeds the supported relocation range", "uptr", raw, 5);
            check(bits, "(uptr)(&record + 1u32)", false, {{nullptr, true}}, {}, "uptr", raw, 6);
            for (const auto initial : {"(uptr)(record.values + 1u32)",
                                       "(uptr)(matrix[1] + 1u32)",
                                       "(uptr)(*(matrix + 1u32) + 1u32)"}) {
                check(bits, initial, false, {{nullptr, true}}, {}, "uptr", raw, 6);
                check(bits, initial, false, {{nullptr, false}},
                    "cannot encode a symbol relocation", "uptr", raw, 6);
            }
        }
    }
}
