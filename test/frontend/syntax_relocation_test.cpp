// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
void function_placements(unsigned bits) {
    using namespace cross;
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto require = [&](bool condition, const char* message) {
        if (condition) return;
        std::cerr << message << '\n' << messages.str();
        std::exit(1);
    };
    const auto no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
    auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
        no_layout, no_layout, EvaluationLimits{}, EvaluationLayout{});
    const auto* source = sources.add("functions.x", R"SOURCE(
namespace Source {
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::tokens raw = $::meta::tokens(function);
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    $::meta::syntax header = $::meta::parse("function_header",
        $::meta::slice(raw, 0uptr, $::meta::len(raw) - 1uptr), $::syntax::context(body));
    return $::quote {
        namespace Left { $::unquote(header); $::unquote(function) }
        namespace Right { $::unquote(raw) }
        namespace Header { $::unquote(header) $::unquote(body) }
    };
}
syntax Copy : item { prefix "copy"; match function:function_def; expand copy; }
syntax Copy;
static u32 recur(in u32 value);
static u32 recur(in u32 value);
copy static u32 recur(in u32 value) {
    if (value == 0u32) return 1u32;
    return value + Source::recur(value - 1u32);
}
copy static T generic<T, u32 N>(in T value) {
    if (value == (T)0) return (T)N;
    return value + generic<T, N>(value - (T)1);
}
[[syntax_expander]] static $::meta::tokens copy_variadic(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::tokens raw = $::meta::tokens(function);
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    $::meta::syntax header = $::meta::parse("function_header",
        $::meta::slice(raw, 0uptr, $::meta::len(raw) - 1uptr), $::syntax::context(function));
    return $::quote {
        namespace Left { $::unquote(function) }
        namespace Right { $::unquote(raw) }
        namespace Header { $::unquote(header) $::unquote(body) }
    };
}
syntax CopyVariadic : item { prefix "copy_variadic"; match function:function_def; expand copy_variadic; }
syntax CopyVariadic;
copy_variadic [[variadic(u32 state "custom_state")]]
static u32 variadic(in u32 tag, ...) { return state; }
}
)SOURCE");
    Parser parser(execution->prepare(*source), diagnostics, execution, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    require(diagnostics.errors() == 0, "function declaration copies were not parsed");
    const auto find = [&](std::string_view name, unsigned ordinal = 0) -> const FunctionDecl& {
        for (const auto& function : program.functions)
            if (function->name == name && ordinal-- == 0) return *function;
        require(false, "copied function missing");
        std::abort();
    };
    const auto& first = find("Source::recur").binding;
    const auto& second = find("Source::recur", 1).binding;
    require(first == second && first.declaration != second.declaration &&
        ValueBindingHash{}(first) == ValueBindingHash{}(second),
        "prototypes did not share entity identity independently of declaring tokens");
    for (const auto name : {"recur", "generic"}) {
        require(find(std::string("Source::Left::") + name).binding ==
            find(std::string("Source::Left::") + name, 1).binding,
            "structured definition did not join its destination prototype identity");
        std::shared_ptr<const ValuePlacementIdentity> previous;
        std::shared_ptr<const ValuePlacementIdentity> previous_parameter;
        std::shared_ptr<const ValuePlacementIdentity> previous_generic;
        TokenIdentity declaration;
        for (const auto space : {"Left", "Right", "Header"}) {
            const auto& function = find(std::string("Source::") + space + "::" + name,
                std::string_view(space) == "Left" ? 1 : 0);
            require(function.binding.kind == ValueBinding::Kind::Function &&
                function.binding.placement && function.binding.placement != previous &&
                function.binding.placement != first.placement,
                "copied functions did not receive separate placement identities");
            if (previous) require(declaration == function.binding.declaration,
                "copied function lost its lexical declaring token");
            declaration = function.binding.declaration;
            previous = function.binding.placement;
            require(previous->name == function.name, "function placement lost its source name");
            require(previous->generic_parameters.size() == function.generic_parameters.size(),
                "function placement lost its generic classification");
            const auto& parameter = function.parameters.front();
            require(parameter.binding.placement && parameter.binding.placement != previous_parameter,
                "copied ordinary parameters shared placement identity");
            previous_parameter = parameter.binding.placement;
            require(parameter.binding.declaration == token_origin(parameter.location).identity,
                "parameter binding does not name its declaring identifier");
            if (!function.generic_parameters.empty()) {
                const auto& generic = function.generic_parameters.back();
                require(generic.binding.placement && generic.binding.placement != previous_generic &&
                    generic.binding.declaration == token_origin(generic.location).identity,
                    "copied value-generic parameters lost their binder placement");
                previous_generic = generic.binding.placement;
            }
            const auto& result = function.body->statements.back()->expression;
            require(result && result->left && result->left->name_context &&
                result->left->name_context->value_binding == parameter.binding,
                (std::string("composed body did not follow its copied ordinary parameter in ") + function.name).c_str());
            require(result && result->right && result->right->kind == Expr::Kind::Call &&
                result->right->left && result->right->left->name_context &&
                result->right->left->name_context->value_binding == function.binding,
                "recursive call did not follow its copied function declaration");
            if (!function.generic_parameters.empty()) {
                require(result->right->generic_visible_at_call,
                    "composed body lost its relocated generic signature");
                const auto& argument = result->right->generic_arguments.back().value;
                require(argument && argument->name_context &&
                    argument->name_context->value_binding == function.generic_parameters.back().binding,
                    "composed body did not follow its copied value-generic parameter");
            }
        }
    }
    std::shared_ptr<const ValuePlacementIdentity> previous_state;
    TokenIdentity state_declaration;
    for (const auto space : {"Left", "Right", "Header"}) {
        const auto& function = find(std::string("Source::") + space + "::variadic");
        const auto* attribute = function.attribute("variadic");
        require(attribute && attribute->variadic_bindings.size() == 1,
            "copied variadic attribute lost its state binder");
        const auto& state = attribute->variadic_bindings.front();
        require(state.binding.kind == ValueBinding::Kind::Local && state.binding.placement &&
            state.binding.placement != previous_state,
            "copied variadic states did not receive distinct placements");
        if (previous_state) require(state.binding.declaration == state_declaration,
            "copied variadic state lost its lexical declaring token");
        previous_state = state.binding.placement;
        state_declaration = state.binding.declaration;
        require(state_declaration == token_origin(state.location).identity && state.state == "custom_state",
            "variadic placement changed its declaring token or model state name");
        const auto& result = function.body->statements.back()->expression;
        require(result && result->name_context && result->name_context->value_binding == state.binding,
            "composed body did not follow its copied variadic state");
    }
    require(expand_semantics(program, diagnostics, false), "moved function semantics failed");
}

void object_placements(unsigned bits) {
    using namespace cross;
    for (const auto input : {
        "copy_local static void captured() { static u32 first = 5u32, *second = &first; }",
        "copy static u32 first = 5u32, *second = &first;",
        "copy static u32 first = 5u32, *second = &Source::first;"}) {
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto require = [&](bool condition, const char* message) {
            if (condition) return;
            std::cerr << message << '\n' << messages.str();
            std::exit(1);
        };
        const auto no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
        auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
            no_layout, no_layout, EvaluationLimits{}, EvaluationLayout{});
        const auto* source = sources.add("objects.x", std::string(R"SOURCE(
namespace Source {
static $::meta::tokens declarations(in $::meta::syntax node, in bool project) {
    if ($::meta::is_production(node, "declaration")) {
        if (project) return $::meta::tokens(node);
        return $::quote { $::unquote(node) };
    }
    $::meta::tokens result = $::quote {};
    if ($::meta::is_kind(node, "core"))
        for (uptr at = 0uptr; at < $::meta::child_count(node); ++at)
            result = $::meta::concat(result, declarations($::meta::child(node, at), project));
    return result;
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    return $::quote {
        namespace Left { $::unquote(declarations(value, 0)) }
        namespace Right { $::unquote(declarations(value, 1)) }
    };
}
syntax CopyLocal : item { prefix "copy_local"; match value:function_def; expand copy; }
syntax Copy : item { prefix "copy"; match value:declaration; expand copy; }
syntax CopyLocal, Copy;
)SOURCE") + input + "\n}");
        Parser parser(execution->prepare(*source), diagnostics, execution, bits);
        auto program = parser.parse();
        program.address_bits = bits;
        require(diagnostics.errors() == 0 && program.objects.size() == 4,
            "object declaration copies were not parsed");
        const auto& left = *program.objects[0];
        const auto& right = *program.objects[2];
        require(left.binding.kind == ValueBinding::Kind::Object &&
            right.binding.kind == ValueBinding::Kind::Object &&
            left.binding.declaration == right.binding.declaration &&
            left.binding.placement != right.binding.placement,
            "copied objects did not receive separate placement identities");
        require(left.binding.placement->name == "Source::Left::first" &&
            right.binding.placement->name == "Source::Right::first",
            "namespace object identity lost its source symbol name");
        for (std::size_t at : {0u, 2u}) {
            const auto& initializer = program.objects[at + 1]->initializer;
            require(initializer && initializer->left && initializer->left->name_context &&
                initializer->left->name_context->value_binding == program.objects[at]->binding,
                "pointer initializer did not follow its copied object declaration");
        }
        require(expand_semantics(program, diagnostics, false), "moved object semantics failed");
    }
}
} // namespace

int main() {
    using namespace cross;
    for (const unsigned bits : {32u, 64u, 128u}) {
      function_placements(bits);
      object_placements(bits);
      for (const auto input : {
          "duplicate_local static void captured() { enum Moved { First = 4u32, Second = First + 1u32 }; }",
          "duplicate enum Moved { First = 4u32, Second = First + 1u32 };",
          "duplicate enum Moved { First = 4u32, Second = Source::First + 1u32 };"}) {
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto require = [&](bool condition, const char* message) {
            if (condition) return;
            std::cerr << message << '\n' << messages.str();
            std::exit(1);
        };
        const auto no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
        auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
            no_layout, no_layout, EvaluationLimits{}, EvaluationLayout{});
        const auto* source = sources.add("relocation.x", std::string(R"SOURCE(
namespace Source {
static $::meta::syntax declaration(in $::meta::syntax node) {
    if ($::meta::is_production(node, "declaration")) return node;
    if ($::meta::is_kind(node, "core"))
        for (uptr at = 0uptr; at < $::meta::child_count(node); ++at) {
            $::meta::syntax child = declaration($::meta::child(node, at));
            if ($::meta::is_production(child, "declaration")) return child;
        }
    return node;
}
[[syntax_expander]] static $::meta::tokens duplicate(in $::meta::syntax_match input) {
    $::meta::syntax value = declaration($::syntax::node(input, "value"));
    return $::quote {
        namespace Left { $::unquote(value) }
        namespace Right { $::unquote(value) }
    };
}
syntax DuplicateLocal : item { prefix "duplicate_local"; match value:function_def; expand duplicate; }
syntax Duplicate : item { prefix "duplicate"; match value:declaration; expand duplicate; }
syntax DuplicateLocal, Duplicate;
)SOURCE") + input + "\n}");
        Parser parser(execution->prepare(*source), diagnostics, execution, bits);
        auto program = parser.parse();
        program.address_bits = bits;
        require(diagnostics.errors() == 0 && program.enumerations.size() == 2,
            "two moved enum placements were not parsed");
        const auto& left = program.enumerations[0];
        const auto& right = program.enumerations[1];
        require(left.name == "Source::Left::Moved" && right.name == "Source::Right::Moved" &&
            !left.local && !right.local && !left.nominal_identity && !right.nominal_identity,
            "moved declarations did not retain ordinary namespace type identity");
        require(left.enumerators.size() == 2 && right.enumerators.size() == 2,
            "moved enum contents changed");
        require(left.enumerators[0].binding.declaration == right.enumerators[0].binding.declaration &&
            left.enumerators[0].binding.enumeration != right.enumerators[0].binding.enumeration,
            "copied lexical tokens did not receive distinct enum placement owners");
        for (const auto& enumeration : program.enumerations) {
            const auto& initial = enumeration.enumerators[1].initializer;
            require(initial && initial->left && initial->left->name_context &&
                initial->left->name_context->value_binding == enumeration.enumerators[0].binding,
                "moved initializer selected a different placement's enumerator");
        }
        require(expand_semantics(program, diagnostics, false), "moved enum evaluation failed");
        for (const auto& enumeration : program.enumerations)
            require(enumeration.enumerators[1].value &&
                enumeration.enumerators[1].value->value == UInt128{5},
                "moved enum value was not retained");
      }
    }
}
