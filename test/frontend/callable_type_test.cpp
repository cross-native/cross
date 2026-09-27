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
}
