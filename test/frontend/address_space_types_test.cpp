// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "middle/hir.hpp"

#include <cstdlib>
#include <sstream>

int main() {
    using namespace cross;
    const auto require = [](bool condition) {
        if (!condition) std::abort();
    };
    auto ordinary = pointer_type(builtin_type(BuiltinType::U32));
    auto first = pointer_type(builtin_type(BuiltinType::U32));
    auto second = pointer_type(builtin_type(BuiltinType::U32));
    first->address_space = 1;
    second->address_space = 2;

    require(!same_type(ordinary, first));
    require(!same_type(first, second));
    require(canonical_type_name(ordinary) != canonical_type_name(first));
    require(canonical_type_name(first) != canonical_type_name(second));

    auto function_one = function_type(
        builtin_type(BuiltinType::Void),
        {ParameterDecl{.location = {}, .name = "value", .type = first,
                       .mode = ParameterMode::In, .explicit_mode = false,
                       .location_name = {}, .declared_array_type = {}}});
    auto function_two = function_type(
        builtin_type(BuiltinType::Void),
        {ParameterDecl{.location = {}, .name = "value", .type = second,
                       .mode = ParameterMode::In, .explicit_mode = false,
                       .location_name = {}, .declared_array_type = {}}});
    require(!same_type(function_one, function_two));
    require(canonical_type_name(function_one) !=
            canonical_type_name(function_two));

    hir::Module module;
    const auto ordinary_id = module.intern_type(ordinary);
    const auto first_id = module.intern_type(first);
    const auto second_id = module.intern_type(second);
    require(ordinary_id != first_id && first_id != second_id);
    require(module.pointer_to(*module.type(ordinary_id).pointee) == ordinary_id);
    require(module.intern_type(function_one) !=
            module.intern_type(function_two));

    first->is_const = true;
    const auto qualified = module.intern_type(first);
    const auto unqualified = module.unqualified(qualified);
    require(module.type(unqualified).address_space == 1);
    require(unqualified == first_id);

    // Registry identity/native support, not space zero or a shipped profile,
    // decides source-type availability. Width alone cannot grant support.
    TargetInfo target;
    target.architecture = "synthetic";
    require(address_space_type_error(target, 0).has_value());
    AddressSpaceEntry supported;
    supported.number = 19;
    supported.pointer_bits = 32;
    supported.native_lowering = true;
    target.address_spaces.push_back(supported);
    require(!address_space_type_error(target, 19));
    require(address_space_type_error(target, 7).has_value());
    target.address_spaces.front().native_lowering = false;
    require(address_space_type_error(target, 19).has_value());

    for (const bool native : {false, true}) {
        target.address_spaces.front().native_lowering = native;
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto* source = sources.add("source-space.x", R"(
            static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) { u32 [[address_space(19)]] *pointer; }
                return input;
            }
            global u32 entry() { return 7u32; }
        )");
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, 32);
        auto program = parser.parse();
        program.address_bits = 32;
        unsigned queries{};
        program.evaluation_address_space_type_error = [&](std::uint32_t space) {
            ++queries;
            return address_space_type_error(target, space);
        };
        require(diagnostics.errors() == 0);
        require(expand_semantics(program, diagnostics, false) == native);
        require(queries != 0);
        require(native ? diagnostics.errors() == 0 :
            messages.str().find("address space 19 has no native lowering") != std::string::npos);
    }
}
