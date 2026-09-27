// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "middle/hir.hpp"

#include <cstdlib>
#include <sstream>

int main() {
    using namespace cross;
    const auto require = [](bool condition) {
        if (!condition) std::abort();
    };
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto* source = sources.add(
        "callable_type.x",
        R"(typedef i32 (*callback)(in i32 value "rdi") -> "rax"
               [[abi("sysv_abi"), clobber("memory"), stack_cleanup("caller")]];
           callback pointer;)" );
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics);
    auto program = parser.parse();
    require(diagnostics.errors() == 0);
    require(program.objects.size() == 1);
    const auto original = program.objects.front()->type;
    require(original->kind == Type::Kind::Pointer);
    require(original->pointee->kind == Type::Kind::Function);
    const auto& function = *original->pointee->function;
    require(function.abi == "sysv_abi");
    require(function.result_location == "rax");
    require(function.parameters.size() == 1);
    require(function.parameters.front().location_name == "rdi");
    require(function.clobbers == std::vector<std::string>{"memory"});
    require(function.stack_cleanup == "caller");

    hir::Module module;
    module.abi_names.emplace("sysv_abi", AbiId{1});
    const auto original_id = module.intern_type(original);
    const auto distinguish = [&](auto change) {
        auto changed = std::make_shared<Type>(*original);
        changed->pointee = std::make_shared<Type>(*original->pointee);
        changed->pointee->function =
            std::make_shared<FunctionType>(*original->pointee->function);
        change(*changed->pointee->function);
        require(!same_type(original, changed));
        require(canonical_type_name(original) != canonical_type_name(changed));
        require(module.intern_type(original) != module.intern_type(changed));
    };
    distinguish([](FunctionType& value) { value.result_location = "rdx"; });
    distinguish([](FunctionType& value) {
        value.parameters.front().location_name = "rsi";
    });
    distinguish([](FunctionType& value) { value.clobbers.clear(); });
    distinguish([](FunctionType& value) {
        value.stack_cleanup = "callee";
    });
    require(module.intern_type(original) == original_id);

    auto copied = copy_type(original);
    require(copied != original && copied->pointee != original->pointee);
    require(copied->pointee->function != original->pointee->function);
    require(same_type(copied, original));
    require(copied->pointee->function->result_location == "rax");
    copied->pointee->function->parameters.front().type->is_volatile = true;
    copied->pointee->function->result_location = "custom_result";
    copied->pointee->function->abi = "custom_abi";
    require(!original->pointee->function->parameters.front().type->is_volatile);
    require(original->pointee->function->result_location == "rax");
    require(original->pointee->function->abi == "sysv_abi");

    // DAG edges remain shared inside the copy, never with the source graph.
    auto scalar = builtin_type(BuiltinType::U32);
    ParameterDecl first_parameter;
    first_parameter.name = "first";
    first_parameter.type = scalar;
    ParameterDecl second_parameter;
    second_parameter.name = "second";
    second_parameter.type = pointer_type(scalar);
    auto dag = function_type(scalar, {first_parameter, second_parameter});
    auto dag_copy = copy_type(dag);
    require(dag_copy->function->result == dag_copy->function->parameters[0].type);
    require(dag_copy->function->result == dag_copy->function->parameters[1].type->pointee);
    require(dag_copy->function->result != scalar);

    const auto* isolation_source = sources.add("alias-isolation.x", R"(
        typedef u32 (*Callback)(in u32 value "input") -> "result"
            [[abi("original"), clobber("memory")]];
        Callback first [[abi("changed")]];
        Callback second;
        struct Mixed { u32 *pointer, scalar; };
    )");
    Parser isolation_parser(Lexer(*isolation_source, diagnostics).lex(), diagnostics);
    auto isolation = isolation_parser.parse();
    require(diagnostics.errors() == 0);
    require(isolation.objects.size() == 2 && isolation.records.size() == 1);
    require(isolation.objects[0]->type->pointee->function->abi == "changed");
    require(isolation.objects[1]->type->pointee->function->abi == "original");
    require(isolation.objects[1]->type->pointee->function->result_location == "result");
    require(isolation.records[0].members[0].type->kind == Type::Kind::Pointer);
    require(isolation.records[0].members[1].type->kind == Type::Kind::Builtin);
    require(isolation.records[0].members[1].type->builtin == BuiltinType::U32);

    const auto* lists_source = sources.add("declaration-lists.x", R"(
        typedef u8 Word;
        namespace nested {
            typedef u16 Word;
            Word namespace_value;
            global u32 lists() {
                typedef u32 Word, *Pointer;
                Word first = 3u32, second = first + 4u32;
                Pointer p = &first, q = &second;
                { typedef u64 Word; Word inner; }
                Word after;
                return second;
            }
        }
        Word global_value;
        typedef u32 Scalar [[aligned(8)]], Vector [[ext_vector_type(4)]];
        Scalar scalar;
        Vector vector;
    )");
    Parser lists_parser(Lexer(*lists_source, diagnostics).lex(), diagnostics);
    auto lists = lists_parser.parse();
    require(diagnostics.errors() == 0);
    require(lists.objects.size() == 4 && lists.functions.size() == 1);
    require(lists.objects[0]->type->builtin == BuiltinType::U16);
    require(lists.objects[1]->type->builtin == BuiltinType::U8);
    require(lists.objects[2]->type->kind == Type::Kind::Builtin);
    require(lists.objects[3]->type->kind == Type::Kind::Vector);
    auto& body = *lists.functions[0]->body;
    require(body.statements.size() == 6);
    const auto& values = *body.statements[1];
    require(values.kind == Statement::Kind::DeclarationList && values.statements.size() == 2);
    require(values.statements[0]->declaration->type->builtin == BuiltinType::U32);
    require(values.statements[1]->declaration->type->builtin == BuiltinType::U32);
    const auto& pointers = *body.statements[2];
    require(pointers.kind == Statement::Kind::DeclarationList && pointers.statements.size() == 2);
    require(pointers.statements[0]->declaration->type->kind == Type::Kind::Pointer);
    require(pointers.statements[1]->declaration->type->kind == Type::Kind::Pointer);
    require(body.statements[3]->statements[1]->declaration->type->builtin == BuiltinType::U64);
    require(body.statements[4]->declaration->type->builtin == BuiltinType::U32);

    const auto* interleaved_source = sources.add("interleaved-specifiers.x", R"(
        typedef u32 [[ext_vector_type(4)]] LaneGroup;
        LaneGroup lanes;
        [[atomic]] u32 counter;
        u32 [[address_space(0)]] *pointer;
        u32 [[aligned(8)]] object;
        u32 [[atomic, aligned(8)]] combined;
        const [[atomic]] u32 qualified;
        struct Fields { const [[aligned(8)]] u32 value; };
    )");
    Parser interleaved_parser(Lexer(*interleaved_source, diagnostics).lex(), diagnostics);
    auto interleaved = interleaved_parser.parse();
    require(diagnostics.errors() == 0);
    require(interleaved.objects.size() == 6 && interleaved.records.size() == 1);
    require(interleaved.objects[0]->type->kind == Type::Kind::Vector);
    require(interleaved.objects[0]->type->lanes == 4);
    require(interleaved.objects[1]->type->is_atomic);
    require(interleaved.objects[2]->type->kind == Type::Kind::Pointer);
    require(interleaved.objects[2]->type->address_space_location.valid());
    require(interleaved.objects[3]->attributes.size() == 1 &&
            interleaved.objects[3]->attributes[0].name == "aligned");
    require(interleaved.objects[4]->type->is_atomic &&
            interleaved.objects[4]->attributes.size() == 1 &&
            interleaved.objects[4]->attributes[0].name == "aligned");
    require(interleaved.objects[5]->type->is_const &&
            interleaved.objects[5]->type->is_atomic);
    require(interleaved.records[0].members.size() == 1 &&
            interleaved.records[0].members[0].attributes.size() == 1 &&
            interleaved.records[0].members[0].attributes[0].name == "aligned");

    const auto* target_vector_source = sources.add("target-vector-size.x", R"(
        typedef uptr WidthVector [[vector_size(16)]];
        typedef iptr SignedWidthVector [[vector_size(16)]];
        WidthVector value;
        SignedWidthVector signed_value;
    )");
    for (const auto [address_bits, expected_lanes] :
         std::vector<std::pair<unsigned, std::uint32_t>>{{32, 4}, {64, 2}}) {
        Parser width_parser(Lexer(*target_vector_source, diagnostics).lex(),
                            diagnostics, {}, address_bits);
        auto width_program = width_parser.parse();
        require(diagnostics.errors() == 0 && width_program.objects.size() == 2);
        for (const auto& object : width_program.objects) {
            require(object->type->kind == Type::Kind::Vector);
            require(object->type->lanes == expected_lanes);
        }
    }
    std::ostringstream unresolved_output;
    Diagnostics unresolved_diagnostics(unresolved_output);
    Parser unresolved_parser(Lexer(*target_vector_source, unresolved_diagnostics).lex(),
                             unresolved_diagnostics);
    (void)unresolved_parser.parse();
    require(unresolved_diagnostics.errors() != 0 &&
            unresolved_output.str().find("requires a resolved target") != std::string::npos);
}
