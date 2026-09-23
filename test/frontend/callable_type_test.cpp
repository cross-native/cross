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
}
