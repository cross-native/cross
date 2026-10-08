// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "middle/hir.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <sstream>

int main() {
    using namespace cross;
    const auto require = [](bool condition, const std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << at.file_name() << ':' << at.line() << ": callable type check failed\n";
            std::abort();
        }
    };
    const auto scalar_element = builtin_type(BuiltinType::U32);
    const auto qualified_vector = vector_type(scalar_element, 4, false, true, true);
    const auto vector_element = qualified_element_type(qualified_vector);
    require(vector_element->is_const && vector_element->is_volatile);
    require(!scalar_element->is_const && !scalar_element->is_volatile);
    const auto qualified_array = array_type(array_type(scalar_element, 2), 3, true, true);
    const auto row = qualified_element_type(qualified_array);
    const auto array_element = qualified_element_type(row);
    require(row->is_const && row->is_volatile);
    require(array_element->is_const && array_element->is_volatile);
    require(!qualified_array->element->is_const && !scalar_element->is_const);
    require(!qualified_element_type(pointer_type(scalar_element, true)));
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    {
        const auto* source = sources.add("prototype-parameter-identity.x", R"(
            typedef u8 input;
            global u8 (*result<T>(in T input, in u8 (*values)[sizeof(input)]))[sizeof(input)];
            global u32 outer(in u32 input, in u32 (*callback)(in u16 input,
                in u8 (*values)[sizeof(input)]), in u8 (*values)[sizeof(input)]);
        )");
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics);
        const auto program = parser.parse();
        require(diagnostics.errors() == 0 && program.functions.size() == 2);
        const auto& result = *program.functions[0];
        const auto parameter_bound = result.parameters[1].type->pointee->array_bound;
        const auto result_bound = result.return_type->pointee->array_bound;
        const auto bound_name = [&](const std::shared_ptr<const Expr>& bound) {
            auto* operand = bound->left.get();
            while (operand && operand->kind == Expr::Kind::Parenthesized) operand = operand->left.get();
            require(operand && operand->kind == Expr::Kind::Name);
            return name_key(*operand);
        };
        require(parameter_bound && result_bound && parameter_bound->left && result_bound->left);
        require(bound_name(parameter_bound) == name_key(result.parameters[0]));
        require(bound_name(result_bound) == name_key(result.parameters[0]));
        const auto& outer = *program.functions[1];
        const auto& callback = *outer.parameters[1].type->pointee->function;
        const auto callback_bound = callback.parameters[1].type->pointee->array_bound;
        const auto outer_bound = outer.parameters[2].type->pointee->array_bound;
        require(callback_bound && callback_bound->left && outer_bound && outer_bound->left);
        require(bound_name(callback_bound) == name_key(callback.parameters[0]));
        require(bound_name(outer_bound) == name_key(outer.parameters[0]));
        require(bound_name(callback_bound) != name_key(outer.parameters[0]));
        const auto copied = copy_type(outer.parameters[1].type);
        const auto& copied_callback = *copied->pointee->function;
        require(bound_name(copied_callback.parameters[1].type->pointee->array_bound) ==
            name_key(copied_callback.parameters[0]));
    }
    {
        const auto* source = sources.add("generic-bound-identity.x",
            "global u32 first<T>(in u8 (*value)[sizeof(T)]); "
            "global u32 second<T>(in u8 (*value)[sizeof(T)]);");
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics);
        const auto program = parser.parse();
        require(diagnostics.errors() == 0 && program.functions.size() == 2);
        const auto first = program.functions[0]->parameters[0].type;
        const auto second = program.functions[1]->parameters[0].type;
        require(!same_type(first, second));
        require(compare_source_types(first, second) == TypeComparison::Different);
        require(compare_generic_types(first, second) == TypeComparison::DeferredBound);
        require(compare_generic_types(first, pointer_type(array_type(builtin_type(BuiltinType::U8), 4))) ==
                TypeComparison::DeferredBound);
        require(compare_generic_types(first, pointer_type(array_type(builtin_type(BuiltinType::U8), 0))) ==
                TypeComparison::Different);
        require(compare_generic_types(first, pointer_type(array_type(builtin_type(BuiltinType::U16), 4))) ==
                TypeComparison::Different);
        auto qualified = copy_type(second);
        qualified->pointee->element->is_const = true;
        require(compare_generic_types(first, qualified) == TypeComparison::Different);
        auto callable = function_type(builtin_type(BuiltinType::U32), program.functions[0]->parameters);
        auto changed = copy_type(callable);
        changed->function->parameters[0].type = second;
        require(compare_generic_types(callable, changed) == TypeComparison::DeferredBound);
        changed->function->result_location = "private-result-bank";
        require(compare_generic_types(callable, changed) == TypeComparison::Different);
    }
    {
        const auto* generic_source = sources.add("generic-identity.x",
            "static T first<T>(in T value); static T second<T>(in T value);");
        Parser generic_parser(Lexer(*generic_source, diagnostics).lex(), diagnostics);
        auto generics = generic_parser.parse();
        require(diagnostics.errors() == 0 && generics.functions.size() == 2);
        const auto& first = *generics.functions[0];
        const auto& second = *generics.functions[1];
        require(generic_type_key(*first.return_type) == name_key(first.generic_parameters[0]));
        require(same_type(first.return_type, first.parameters[0].type));
        require(!same_type(first.return_type, second.return_type));
        const auto copied_generic = copy_type(first.return_type);
        require(copied_generic != first.return_type && same_type(copied_generic, first.return_type));
        // Internal alpha-normalization placeholders remain comparable by their
        // placeholder name; parsed source parameters always carry a binder.
        require(same_type(generic_type("placeholder"), generic_type("placeholder")));
    }
    {
        const auto* source = sources.add("grouped-function-suffix-attributes.x", R"(
            typedef u8 T;
            static u32 (*factory(in u32 value) -> "factory.result"
                [[noinline, abi("factory_abi"), clobber("factory-resource"),
                  stack_cleanup("callee")]])(in u32 argument) -> "callback.result"
                [[abi("callback_abi"), clobber("callback-resource"), stack_cleanup("caller")]];
            static T (*generic(in T value) [[generic(T), noinline]])(in T argument);
            static T (*plain(in T value))(in T argument);
        )");
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics);
        const auto program = parser.parse();
        require(diagnostics.errors() == 0 && program.functions.size() == 3);
        const auto& factory = *program.functions[0];
        require(factory.attribute("noinline") && factory.attribute("abi") &&
                factory.attribute("abi")->arguments == std::vector<std::string>{"\"factory_abi\""});
        require(factory.result_location == "factory.result" &&
                factory.attribute("clobber")->arguments == std::vector<std::string>{"\"factory-resource\""} &&
                factory.attribute("stack_cleanup")->arguments == std::vector<std::string>{"\"callee\""});
        const auto& callback = *factory.return_type->pointee->function;
        require(callback.abi == "callback_abi" && callback.result_location == "callback.result" &&
                callback.clobbers == std::vector<std::string>{"callback-resource"} &&
                callback.stack_cleanup == "caller");
        const auto& generic = *program.functions[1];
        require(generic.attribute("noinline") && generic.generic_parameters.size() == 1 &&
                generic.parameters[0].type->kind == Type::Kind::Generic);
        const auto& generic_callback = *generic.return_type->pointee->function;
        require(same_type(generic.parameters[0].type, generic_callback.result) &&
                same_type(generic_callback.result, generic_callback.parameters[0].type));
        const auto& plain = *program.functions[2];
        require(plain.generic_parameters.empty() && plain.parameters[0].type->builtin == BuiltinType::U8 &&
                plain.return_type->pointee->function->result->builtin == BuiltinType::U8);
    }
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

    // A missing expansion-dependent extent delays only that comparison; all
    // independent type and ABI fields still have to agree. Physical endpoint
    // spellings are opaque here, including custom register/memory locations.
    const auto deferred_array = [](TypePtr element) {
        auto result = array_type(std::move(element), 0);
        result->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
        return result;
    };
    auto deferred = deferred_array(copy_type(original));
    require(has_pending_type_bound(deferred));
    auto concrete = array_type(copy_type(original), 2);
    require(compare_source_types(deferred, concrete) == TypeComparison::DeferredBound);
    require(compare_source_types(concrete, deferred) == TypeComparison::DeferredBound);
    require(!same_type(deferred, concrete));
    require(compare_source_types(deferred, array_type(copy_type(original), 0)) == TypeComparison::Different);
    require(compare_source_types(deferred, array_type(builtin_type(BuiltinType::U8), 2)) == TypeComparison::Different);
    const auto independent_difference = [&](auto change) {
        auto changed = copy_type(concrete);
        change(changed->element);
        require(compare_source_types(deferred, changed) == TypeComparison::Different);
    };
    independent_difference([](TypePtr& value) { value->is_const = true; });
    independent_difference([](TypePtr& value) { value->address_space = 17; });
    independent_difference([](TypePtr& value) { value->pointee->function->abi = "custom_abi"; });
    independent_difference([](TypePtr& value) { value->pointee->function->result_location = "custom.memory"; });
    independent_difference([](TypePtr& value) { value->pointee->function->parameters[0].location_name = "stack+32"; });
    independent_difference([](TypePtr& value) { value->pointee->function->parameters[0].mode = ParameterMode::Out; });
    independent_difference([](TypePtr& value) { value->pointee->function->clobbers.clear(); });
    independent_difference([](TypePtr& value) { value->pointee->function->stack_cleanup = "callee"; });
    deferred->lanes = 2;
    deferred->array_extent_dependency = Type::ArrayExtentDependency::None;
    require(compare_source_types(deferred, concrete) == TypeComparison::Same && same_type(deferred, concrete));
    concrete->lanes = 3;
    require(compare_source_types(deferred, concrete) == TypeComparison::Different);

    auto deferred_vector = vector_type(builtin_type(BuiltinType::U32), 0);
    deferred_vector->vector_extent_dependency = Type::VectorExtentDependency::ExpansionContext;
    require(has_pending_type_bound(deferred_vector));
    auto concrete_vector = vector_type(builtin_type(BuiltinType::U32), 4);
    require(compare_source_types(deferred_vector, concrete_vector) == TypeComparison::DeferredBound);
    require(compare_pointee(deferred_vector, concrete_vector) == PointeeCompatibility::DeferredExtent);
    require(!same_type(deferred_vector, concrete_vector));
    require(copy_type(deferred_vector)->vector_extent_dependency == Type::VectorExtentDependency::ExpansionContext);
    require(compare_source_types(deferred_vector, vector_type(builtin_type(BuiltinType::U16), 4)) == TypeComparison::Different);
    require(compare_source_types(deferred_vector, vector_type(builtin_type(BuiltinType::U32), 4, true)) == TypeComparison::Different);
    require(compare_source_types(deferred_vector, vector_type(builtin_type(BuiltinType::U32), 0)) == TypeComparison::Different);
    auto deferred_callback = copy_type(original);
    auto concrete_callback = copy_type(original);
    deferred_callback->pointee->function->parameters[0].type = deferred_vector;
    concrete_callback->pointee->function->parameters[0].type = concrete_vector;
    require(compare_source_types(deferred_callback, concrete_callback) == TypeComparison::DeferredBound);
    require(compare_pointee(deferred_callback->pointee, concrete_callback->pointee) == PointeeCompatibility::DeferredExtent);
    require(!compatible_pointee(deferred_callback->pointee, concrete_callback->pointee));
    for (const auto endpoint : {"custom.result", "stack+32", "*custom.address"}) {
        auto changed = copy_type(concrete_callback);
        changed->pointee->function->result_location = endpoint;
        require(compare_source_types(deferred_callback, changed) == TypeComparison::Different);
        require(compare_pointee(deferred_callback->pointee, changed->pointee) == PointeeCompatibility::Incompatible);
    }
    deferred_vector->lanes = 4;
    deferred_vector->vector_extent_dependency = Type::VectorExtentDependency::None;
    require(!has_pending_type_bound(deferred_vector));
    require(same_type(deferred_callback, concrete_callback));

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

    const auto* late_generic_source = sources.add("late-generic.x", R"(
        namespace T { typedef u16 Word; }
        namespace Aliased {
        typedef u8 T;
        static T [[generic(T), noinline]] interleaved(in T value) { return value; }
        static T trailing(in T value) [[generic(T), noinline]] { return value; }
        [[generic(T *pointer, T)]] static T forward(in T value) { return value; }
        static T angle<T>(in T value) { return value; }
        T after;
        }
        static T::Word qualified<T>(in T value) { return 1u16; }
    )");
    Parser late_generic_parser(Lexer(*late_generic_source, diagnostics).lex(), diagnostics);
    auto late_generic = late_generic_parser.parse();
    require(diagnostics.errors() == 0 && late_generic.functions.size() == 5);
    for (std::size_t index = 0; index < 4; ++index) {
        const auto& declaration = *late_generic.functions[index];
        require(!declaration.generic_parameters.empty());
        require(declaration.return_type->kind == Type::Kind::Generic);
        require(declaration.return_type->generic_name == "T");
        require(declaration.parameters[0].type->kind == Type::Kind::Generic);
    }
    const auto& forward = *late_generic.functions[2];
    require(forward.generic_parameters.size() == 2);
    require(forward.generic_parameters[0].value_type->kind == Type::Kind::Pointer);
    require(forward.generic_parameters[0].value_type->pointee->kind == Type::Kind::Generic);
    require(late_generic.functions[4]->return_type->kind == Type::Kind::Builtin);
    require(late_generic.functions[4]->return_type->builtin == BuiltinType::U16);
    require(late_generic.objects[0]->type->kind == Type::Kind::Builtin);
    require(late_generic.objects[0]->type->builtin == BuiltinType::U8);

    const auto* grouped_source = sources.add("grouped-declarators.x", R"(
        typedef u8 T;
        static T ((identity<T>))(in T (value)) [[noinline]] { return value; }
        static u32 (callback)(in u32 value) -> "custom.result";
        u32 accepts(u32 (T), u32 (u16), u32 (named));
        T after_grouped;
    )");
    Parser grouped_parser(Lexer(*grouped_source, diagnostics).lex(), diagnostics);
    auto grouped = grouped_parser.parse();
    require(diagnostics.errors() == 0 && grouped.functions.size() == 3);
    require(grouped.functions[0]->return_type->kind == Type::Kind::Generic);
    require(grouped.functions[0]->return_type->generic_name == "T");
    require(grouped.functions[0]->parameters[0].name == "value");
    require(grouped.functions[0]->parameters[0].type->kind == Type::Kind::Generic);
    require(grouped.functions[1]->result_location == "custom.result");
    const auto& grouped_parameters = grouped.functions[2]->parameters;
    require(grouped_parameters.size() == 3);
    for (std::size_t index = 0; index < 2; ++index) {
        require(grouped_parameters[index].type->kind == Type::Kind::Pointer);
        const auto& callable = grouped_parameters[index].type->pointee;
        require(callable->kind == Type::Kind::Function);
        require(callable->function->parameters.size() == 1);
        require(callable->function->parameters[0].type->builtin ==
            (index == 0 ? BuiltinType::U8 : BuiltinType::U16));
    }
    require(grouped_parameters[2].name == "named");
    require(grouped_parameters[2].type->builtin == BuiltinType::U32);
    require(grouped.objects[0]->type->builtin == BuiltinType::U8);

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
        typedef u32 Scalar, Vector [[ext_vector_type(4)]];
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

    const auto* vector_specifier_source = sources.add("vector-specifiers.x", R"(
        [[ext_vector_type(4)]] const u32 leading, *pointer, array[2];
        u32 [[vector_size(16), aligned(32)]] object;
        typedef const volatile u32 Qualified [[ext_vector_type(4)]];
        Qualified qualified;
        struct Fields { u32 [[ext_vector_type(4)]] values; };
        static u32 [[ext_vector_type(4)]] transform(in u32 [[vector_size(16)]] value);
    )");
    Parser vector_specifier_parser(Lexer(*vector_specifier_source, diagnostics).lex(), diagnostics);
    auto vectors = vector_specifier_parser.parse();
    require(diagnostics.errors() == 0 && vectors.objects.size() == 5 &&
            vectors.records.size() == 1 && vectors.functions.size() == 1);
    require(vectors.objects[0]->type->kind == Type::Kind::Vector && vectors.objects[0]->type->is_const);
    require(vectors.objects[1]->type->kind == Type::Kind::Pointer &&
            vectors.objects[1]->type->pointee->kind == Type::Kind::Vector &&
            vectors.objects[1]->type->pointee->is_const);
    require(vectors.objects[2]->type->kind == Type::Kind::Array &&
            vectors.objects[2]->type->element->kind == Type::Kind::Vector);
    require(vectors.objects[3]->type->kind == Type::Kind::Vector &&
            vectors.objects[3]->attributes.size() == 1 &&
            vectors.objects[3]->attributes[0].name == "aligned");
    require(vectors.objects[4]->type->kind == Type::Kind::Vector &&
            vectors.objects[4]->type->is_const && vectors.objects[4]->type->is_volatile &&
            !vectors.objects[4]->type->element->is_const && !vectors.objects[4]->type->element->is_volatile);
    require(vectors.records[0].members[0].type->kind == Type::Kind::Vector);
    require(same_type(vectors.functions[0]->return_type, vectors.functions[0]->parameters[0].type));
    require(vectors.functions[0]->return_type->kind == Type::Kind::Vector &&
            vectors.functions[0]->return_type->lanes == 4);

    const auto* target_vector_source = sources.add("target-vector-size.x", R"(
        typedef uptr WidthVector [[vector_size(16)]];
        typedef iptr SignedWidthVector [[vector_size(16)]];
        WidthVector value;
        SignedWidthVector signed_value;
        uptr [[vector_size(16)]] direct;
    )");
    for (const auto [address_bits, expected_lanes] :
         std::vector<std::pair<unsigned, std::uint32_t>>{{32, 4}, {64, 2}}) {
        Parser width_parser(Lexer(*target_vector_source, diagnostics).lex(),
                            diagnostics, {}, address_bits);
        auto width_program = width_parser.parse();
        require(diagnostics.errors() == 0 && width_program.objects.size() == 3);
        for (const auto& object : width_program.objects) {
            require(object->type->kind == Type::Kind::Vector);
            require(object->type->lanes == expected_lanes);
        }
    }
    const auto* array_source = sources.add("array-parameters.x", R"(
        static uptr width() { return 4uptr; }
        void direct(in u8 values[width()], in u8 (*row)[width()]);
        typedef void (*Callback)(in u8 values[width()], in u8 (*row)[width()]);
        Callback callback;
    )");
    Parser array_parser(Lexer(*array_source, diagnostics).lex(), diagnostics);
    auto arrays = array_parser.parse();
    require(diagnostics.errors() == 0 && arrays.functions.size() == 2 && arrays.objects.size() == 1);
    const auto check_parameters = [&](const std::vector<ParameterDecl>& parameters) {
        require(parameters.size() == 2);
        require(parameters[0].type->kind == Type::Kind::Pointer &&
                parameters[0].type->pointee->kind == Type::Kind::Builtin);
        require(parameters[0].declared_array_type && parameters[0].declared_array_type->array_bound);
        require(parameters[1].type->kind == Type::Kind::Pointer &&
                parameters[1].type->pointee->kind == Type::Kind::Array &&
                parameters[1].type->pointee->array_bound);
        require(!parameters[1].declared_array_type);
    };
    check_parameters(arrays.functions[1]->parameters);
    const auto callback_source = arrays.objects[0]->type;
    check_parameters(callback_source->pointee->function->parameters);
    auto callback_copy = copy_type(callback_source);
    check_parameters(callback_copy->pointee->function->parameters);
    callback_copy->pointee->function->parameters[0].declared_array_type->lanes = 5;
    require(callback_source->pointee->function->parameters[0].declared_array_type->lanes == 0);

    std::ostringstream unresolved_output;
    const auto* bound_vector_source = sources.add("vector-bounds.x", R"(
        typedef u32 Unused [[ext_vector_type(2uptr + 2uptr)]];
        static T make<T, uptr N>(in T value) {
            typedef T Local [[ext_vector_type(N)]];
            return value;
        }
    )");
    Parser bound_vector_parser(Lexer(*bound_vector_source, diagnostics).lex(), diagnostics, {}, 32);
    auto bound_vectors = bound_vector_parser.parse();
    require(diagnostics.errors() == 0 && bound_vectors.required_types.size() == 1);
    require(bound_vectors.required_types[0].type->vector_bound &&
            bound_vectors.required_types[0].type->lanes == 0);
    require(bound_vectors.functions.size() == 1 && bound_vectors.functions[0]->required_types.size() == 1);
    require(bound_vectors.functions[0]->required_types[0].type->vector_bound &&
            bound_vectors.functions[0]->required_types[0].type->element->kind == Type::Kind::Generic);
    Diagnostics unresolved_diagnostics(unresolved_output);
    Parser unresolved_parser(Lexer(*target_vector_source, unresolved_diagnostics).lex(),
                             unresolved_diagnostics);
    (void)unresolved_parser.parse();
    require(unresolved_diagnostics.errors() != 0 &&
            unresolved_output.str().find("requires a resolved target") != std::string::npos);
}
