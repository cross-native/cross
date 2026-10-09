// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <new>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
bool forbid_release_allocation{};
// Out of line so GCC does not pair an inlined free() with an operator new call.
[[gnu::noinline]] void release(void* memory) noexcept { std::free(memory); }
}

void* operator new(std::size_t bytes) {
    if (forbid_release_allocation) {
        std::fputs("allocation during normal AST release\n", stderr);
        std::abort();
    }
    if (auto* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { release(memory); }
void operator delete[](void* memory) noexcept { release(memory); }
void operator delete(void* memory, std::size_t) noexcept { release(memory); }
void operator delete[](void* memory, std::size_t) noexcept { release(memory); }

namespace {
using namespace cross;

struct NoReleaseAllocation {
    bool previous{forbid_release_allocation};
    NoReleaseAllocation() { forbid_release_allocation = true; }
    ~NoReleaseAllocation() { forbid_release_allocation = previous; }
};

struct Witness {
    ContinuationSchedule*& expected;
    bool& continuous;
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    bool await_suspend(std::coroutine_handle<Promise> frame) const noexcept {
        if (!expected) expected = frame.promise().schedule;
        else if (expected != frame.promise().schedule) continuous = false;
        return false;
    }
    void await_resume() const noexcept {}
};

void require(bool value, const char* detail,
    std::source_location location = std::source_location::current()) {
    if (!value) {
        std::cerr << location.line() << ": " << detail << '\n';
        std::abort();
    }
}

// Synthetic source owners are released without recursive unique_ptr teardown.
// Deep host fixtures do not accept source beyond the language's own limits.
struct Cleanup {
    Program& program;
    static void expression(std::unique_ptr<Expr>& root) {
        std::vector<std::unique_ptr<Expr>> pending;
        pending.push_back(std::move(root));
        while (!pending.empty()) {
            auto node = std::move(pending.back());
            pending.pop_back();
            if (!node) continue;
            pending.push_back(std::move(node->left));
            pending.push_back(std::move(node->right));
            pending.push_back(std::move(node->third));
            for (auto& child : node->arguments) pending.push_back(std::move(child));
            for (auto& argument : node->generic_arguments) pending.push_back(std::move(argument.value));
            for (auto& entry : node->initializer_entries) {
                pending.push_back(std::move(entry.value));
                for (auto& designator : entry.designators) pending.push_back(std::move(designator.index));
            }
        }
    }
    ~Cleanup() {
        std::vector<std::unique_ptr<Statement>> pending;
        for (auto& function : program.functions) pending.push_back(std::move(function->body));
        for (auto& function : program.evaluation_definitions) pending.push_back(std::move(function->body));
        while (!pending.empty()) {
            auto node = std::move(pending.back());
            pending.pop_back();
            if (!node) continue;
            expression(node->expression);
            expression(node->condition);
            for (auto& increment : node->increments) expression(increment);
            if (node->declaration) {
                expression(node->declaration->initializer);
                expression(node->declaration->dynamic_array_bound);
            }
            pending.push_back(std::move(node->first));
            pending.push_back(std::move(node->second));
            for (auto& child : node->statements) pending.push_back(std::move(child));
        }
    }
};

void deep_statements() {
    Program program;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    Cleanup cleanup{program};
    auto function = std::make_unique<FunctionDecl>();
    function->name = "deep";
    function->return_type = builtin_type(BuiltinType::Void);
    function->body = std::make_unique<Statement>();
    function->body->kind = Statement::Kind::Expression;
    function->body->expression = std::make_unique<Expr>();
    function->body->expression->kind = Expr::Kind::Integer;
    function->body->expression->text = "1u32";
    for (unsigned index = 0; index < 50000; ++index) {
        auto parent = std::make_unique<Expr>();
        parent->kind = Expr::Kind::Parenthesized;
        parent->left = std::move(function->body->expression);
        function->body->expression = std::move(parent);
    }
    for (unsigned index = 0; index < 50000; ++index) {
        auto parent = std::make_unique<Statement>();
        parent->kind = Statement::Kind::Compound;
        parent->statements.push_back(std::move(function->body));
        function->body = std::move(parent);
    }
    program.functions.push_back(std::move(function));
    require(expand_evaluation_async(program, diagnostics, false).run() && !diagnostics.errors(),
        "deep source validation/evaluation rewriting recursed");
}

void deep_case_labels() {
    for (const unsigned bits : {32U, 64U}) {
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto* source = sources.add("deep-case.x",
            "[[eval_only]] static u32 deep() { switch (0u32) { case 0u32: return 1u32; default: return 2u32; } }");
        auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
        require(!diagnostics.errors(), "deep case fixture did not parse");
        Cleanup cleanup{program};
        program.address_bits = bits;
        auto& label = *program.functions.front()->body->statements.front()->first->statements.front();
        require(label.kind == Statement::Kind::Case && label.expression, "deep case owner changed");
        for (unsigned index = 0; index < 50000; ++index) {
            auto parent = std::make_unique<Expr>();
            parent->kind = Expr::Kind::Parenthesized;
            parent->location = label.expression->location;
            parent->left = std::move(label.expression);
            label.expression = std::move(parent);
        }
        const bool valid = expand_evaluation_async(program, diagnostics, false).run();
        if (!valid) std::cerr << messages.str();
        require(valid && !diagnostics.errors() && program.functions.empty() &&
            program.evaluation_definitions.size() == 1 && label.expression->kind == Expr::Kind::Parenthesized,
            "deep case dependency validation recursed or replaced retained translation-only source");
    }
}

void deep_private_statements() {
    for (const unsigned bits : {32U, 64U}) {
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto* source = sources.add("deep-block.x", "[[eval_only]] static u32 deep() { return 7u32; }");
        auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
        require(!diagnostics.errors(), "deep block fixture did not parse");
        program.address_bits = bits;
        auto& body = program.functions.front()->body;
        for (unsigned index = 0; index < 50000; ++index) {
            auto parent = std::make_unique<Statement>();
            parent->kind = Statement::Kind::Compound;
            parent->statements.push_back(std::move(body));
            body = std::move(parent);
        }
        const auto* original = body.get();
        const bool valid = expand_evaluation_async(program, diagnostics, false).run();
        if (!valid) std::cerr << messages.str();
        require(valid && !diagnostics.errors() && program.functions.empty() &&
            program.evaluation_definitions.size() == 1 && program.evaluation_definitions.front()->body.get() == original,
            "deep private block copying/validation recursed or replaced retained source ownership");
    }
}

void deep_type_copies(unsigned first_edge = 0) {
    const auto same_location = [](SourceLocation a, SourceLocation b) {
        return a.file == b.file && a.offset == b.offset && a.line == b.line && a.column == b.column;
    };
    for (unsigned edge = first_edge; edge < 4; ++edge) {
        SourceManager sources;
        const auto* file = sources.add("type-copy.x", "copy");
        auto context = std::make_shared<NameLookupContext>();
        auto nominal = std::make_shared<NominalTypeIdentity>();
        nominal->source_unit = "type-copy.x";
        auto obligations = std::make_shared<CapturedTypeErrors>();
        obligations->push_back({{file, 0, 1, 1}, "retained obligation"});
        ObjectDecl source;
        source.name = "deep_type";
        source.type = enum_type("Leaf", BuiltinType::U32);
        source.type->nominal_identity = nominal;
        for (unsigned index = 0; index < 50000; ++index) {
            if (edge == 0) source.type = pointer_type(std::move(source.type));
            else if (edge == 1) {
                if (index % 2) source.type = array_type(std::move(source.type), 3);
                else source.type = vector_type(std::move(source.type), 4, index % 3 == 0);
            } else if (edge == 2) {
                ParameterDecl parameter;
                parameter.name = "parameter";
                parameter.location = {file, 1, 1, 2};
                parameter.mode = ParameterMode::InOut;
                parameter.explicit_mode = true;
                parameter.location_name = "selected-input";
                parameter.type = builtin_type(BuiltinType::U8);
                TypePtr result = builtin_type(BuiltinType::U32);
                if (index % 3 == 0) result = std::move(source.type);
                else if (index % 3 == 1) parameter.type = std::move(source.type);
                else parameter.declared_array_type = std::move(source.type);
                source.type = function_type(std::move(result), {parameter}, true, "selected-alias");
                source.type->function->result_location = "selected-result";
                source.type->function->clobbers = {"selected-clobber"};
                source.type->function->stack_cleanup = "selected-cleanup";
            } else {
                auto bound = std::make_shared<Expr>();
                bound->kind = Expr::Kind::Integer;
                bound->text = "4u32";
                bound->location = {file, 2, 1, 3};
                bound->name_context = context;
                if (index % 3 == 0) bound->type = std::move(source.type);
                else if (index % 3 == 1) bound->deferred_generic_signature = std::move(source.type);
                else bound->generic_arguments.push_back({std::move(source.type), {}});
                if (index % 2) {
                    source.type = array_type(builtin_type(BuiltinType::U8), 0);
                    source.type->array_bound = std::move(bound);
                    source.type->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
                } else {
                    source.type = vector_type(builtin_type(BuiltinType::U8), 0, false);
                    source.type->vector_bound = std::move(bound);
                    source.type->vector_bound_unit = Type::VectorBoundUnit::Bytes;
                    source.type->vector_extent_dependency = Type::VectorExtentDependency::ExpansionContext;
                }
            }
            source.type->is_const = index % 2 == 0;
            source.type->is_volatile = index % 3 == 0;
            source.type->is_atomic = index % 5 == 0;
            source.type->is_restrict = index % 7 == 0;
        }
        source.type->address_space = 3;
        source.type->address_space_location = {file, 0, 1, 1};
        source.type->pending_address_space = std::pair{4U, SourceLocation{file, 1, 1, 2}};
        source.type->captured_errors = *obligations;
        source.type->captured_tag_errors = obligations;
        auto copy = copy_evaluation_declaration(source);
        require(copy && copy->type && copy->type != source.type, "deep type copy reused its mutable source");
        std::vector<std::pair<const Type*, const Type*>> types{{source.type.get(), copy->type.get()}};
        std::vector<std::pair<const Expr*, const Expr*>> expressions;
        const auto type_pair = [&](const TypePtr& a, const TypePtr& b) {
            require(bool(a) == bool(b), "deep type copy changed a type edge");
            if (a) types.emplace_back(a.get(), b.get());
        };
        const auto expression_pair = [&](const auto& a, const auto& b) {
            require(bool(a) == bool(b), "deep type copy changed a bound-expression edge");
            if (a) expressions.emplace_back(a.get(), b.get());
        };
        while (!types.empty() || !expressions.empty()) {
            if (!types.empty()) {
                const auto [a, b] = types.back();
                types.pop_back();
                require(a != b && a->kind == b->kind && a->builtin == b->builtin &&
                    a->nominal_key() == b->nominal_key() && a->is_union == b->is_union &&
                    a->is_const == b->is_const && a->is_volatile == b->is_volatile &&
                    a->is_atomic == b->is_atomic && a->is_restrict == b->is_restrict &&
                    a->address_space == b->address_space && same_location(a->address_space_location, b->address_space_location) &&
                    a->lanes == b->lanes && a->scalable == b->scalable &&
                    a->array_extent_dependency == b->array_extent_dependency &&
                    a->vector_bound_unit == b->vector_bound_unit && a->vector_extent_dependency == b->vector_extent_dependency &&
                    a->captured_tag_errors == b->captured_tag_errors && a->captured_errors.size() == b->captured_errors.size(),
                    "deep type copy changed qualifiers, nominal identity or retained metadata");
                require(bool(a->pending_address_space) == bool(b->pending_address_space),
                    "deep type copy changed pending address-space metadata");
                if (a->pending_address_space) require(a->pending_address_space->first == b->pending_address_space->first &&
                    same_location(a->pending_address_space->second, b->pending_address_space->second),
                    "deep type copy changed a pending address-space location");
                for (std::size_t index = 0; index < a->captured_errors.size(); ++index)
                    require(a->captured_errors[index].message == b->captured_errors[index].message &&
                        same_location(a->captured_errors[index].location, b->captured_errors[index].location),
                        "deep type copy changed a retained diagnostic");
                type_pair(a->pointee, b->pointee);
                type_pair(a->element, b->element);
                expression_pair(a->array_bound, b->array_bound);
                expression_pair(a->vector_bound, b->vector_bound);
                require(bool(a->function) == bool(b->function), "deep type copy changed a callable edge");
                if (a->function) {
                    const auto& af = *a->function;
                    const auto& bf = *b->function;
                    require(&af != &bf && af.variadic == bf.variadic && af.abi == bf.abi &&
                        af.result_location == bf.result_location && af.clobbers == bf.clobbers &&
                        af.stack_cleanup == bf.stack_cleanup && af.parameters.size() == bf.parameters.size(),
                        "deep type copy changed callable source identity");
                    type_pair(af.result, bf.result);
                    for (std::size_t index = 0; index < af.parameters.size(); ++index) {
                        const auto& ap = af.parameters[index];
                        const auto& bp = bf.parameters[index];
                        require(ap.name == bp.name && ap.binding == bp.binding && ap.mode == bp.mode &&
                            ap.explicit_mode == bp.explicit_mode && ap.location_name == bp.location_name &&
                            same_location(ap.location, bp.location), "deep type copy changed callable parameter metadata");
                        type_pair(ap.type, bp.type);
                        type_pair(ap.declared_array_type, bp.declared_array_type);
                    }
                }
            } else {
                const auto [a, b] = expressions.back();
                expressions.pop_back();
                require(a != b && a->kind == b->kind && a->text == b->text &&
                    a->name_context == b->name_context && same_location(a->location, b->location) &&
                    a->generic_arguments.size() == b->generic_arguments.size(), "deep type copy changed bound identity/shape");
                type_pair(a->type, b->type);
                type_pair(a->deferred_generic_signature, b->deferred_generic_signature);
                for (std::size_t index = 0; index < a->generic_arguments.size(); ++index) {
                    type_pair(a->generic_arguments[index].type, b->generic_arguments[index].type);
                    expression_pair(a->generic_arguments[index].value, b->generic_arguments[index].value);
                }
            }
        }
        {
            NoReleaseAllocation guard;
            copy->type.reset();
            source.type.reset();
        }
        require(context.use_count() == 1 && nominal.use_count() == 1 && obligations.use_count() == 1,
            "deep type-copy fixture leaked shared metadata during normal release");
    }
}

void shared_release_controls() {
    const auto deep = [] {
        auto type = builtin_type(BuiltinType::U32);
        for (unsigned index = 0; index < 50000; ++index) type = pointer_type(std::move(type));
        return type;
    };
    {
        auto child = deep();
        const auto* pointee = child->pointee.get();
        const auto weak = std::weak_ptr<Type>(child);
        auto parent = pointer_type(child);
        { NoReleaseAllocation guard; parent.reset(); }
        require(child->pointee.get() == pointee && !weak.expired(), "release edited a still-owned Type subgraph");
        { NoReleaseAllocation guard; child.reset(); }
        require(weak.expired(), "normal Type release retained an intrusive self-owner");
    }
    {
        auto signature = std::make_shared<FunctionType>();
        signature->result = deep();
        const auto* result = signature->result.get();
        ParameterDecl parameter;
        parameter.type = signature->result;
        signature->parameters.push_back(std::move(parameter));
        auto parent = function_type({}, {}, false, "selected-alias");
        parent->function = signature;
        { NoReleaseAllocation guard; parent.reset(); }
        require(signature->result.get() == result && signature->parameters.front().type.get() == result,
            "release edited shared callable metadata before its last owner");
        { NoReleaseAllocation guard; signature.reset(); }
    }
    {
        auto child = deep();
        const auto weak = std::weak_ptr<Type>(child);
        const auto* pointee = child->pointee.get();
        TypePtr observed, copied;
        auto parent = pointer_type(std::move(child));
        parent->function = std::shared_ptr<FunctionType>(new FunctionType,
            [&](FunctionType* function) {
                observed = weak.lock();
                require(bool(observed), "queued Type lost normal weak-observer lifetime");
                copied = std::make_shared<Type>(*observed);
                delete function;
            });
        parent.reset();
        require(observed->pointee.get() == pointee && copied->pointee.get() == pointee,
            "queued Type was detached after another owner acquired it");
        { NoReleaseAllocation guard; observed.reset(); }
        require(weak.expired() && copied->pointee.get() == pointee,
            "Type copy inherited queued ownership state or lost a shared pointee");
        { NoReleaseAllocation guard; copied.reset(); }
    }
    {
        auto expression = std::make_shared<Expr>();
        expression->kind = Expr::Kind::Integer;
        expression->text = "1u32";
        expression->type = deep();
        const auto* annotation = expression->type.get();
        const auto weak = std::weak_ptr<const Expr>(expression);
        std::shared_ptr<const Expr> observed;
        auto parent = array_type(builtin_type(BuiltinType::U8), 1);
        parent->array_bound = std::move(expression);
        parent->function = std::shared_ptr<FunctionType>(new FunctionType,
            [&](FunctionType* function) { observed = weak.lock(); delete function; });
        parent.reset();
        require(observed && observed->type.get() == annotation,
            "queued bound expression was detached after another owner acquired it");
        { NoReleaseAllocation guard; observed.reset(); }
        require(weak.expired(), "normal shared Expr release retained intrusive ownership state");
    }
    {
        // An aliasing shared_ptr owns its enclosing Expr, not the selected
        // child. Releasing the alias must use its real control block/deleter.
        auto enclosing = std::make_shared<Expr>();
        enclosing->left = std::make_unique<Expr>();
        enclosing->left->type = deep();
        auto parent = array_type(builtin_type(BuiltinType::U8), 1);
        parent->array_bound = std::shared_ptr<const Expr>(enclosing, enclosing->left.get());
        const auto weak = std::weak_ptr<Expr>(enclosing);
        enclosing.reset();
        { NoReleaseAllocation guard; parent.reset(); }
        require(weak.expired(), "aliasing bound expression leaked its enclosing owner");
    }
}

void statement_copies() {
    const auto same_location = [](SourceLocation a, SourceLocation b) {
        return a.file == b.file && a.offset == b.offset && a.line == b.line && a.column == b.column;
    };
    for (unsigned edge = 0; edge < 3; ++edge) {
        SourceManager sources;
        const auto* file = sources.add("statement-copy.x", "copy");
        auto context = std::make_shared<NameLookupContext>();
        context->kind = NameLookupContext::Kind::Exact;
        auto fresh = std::make_shared<FreshIdentifier>();
        fresh->prefix = "label";
        const auto integer = [&] {
            auto node = std::make_unique<Expr>();
            node->kind = Expr::Kind::Integer;
            node->text = "7u32";
            node->location = {file, 2, 1, 3};
            node->name_context = context;
            return node;
        };
        const auto child = [&](Statement::Kind kind) {
            auto node = std::make_unique<Statement>();
            node->kind = kind;
            node->location = {file, 1, 1, 2};
            node->expression = integer();
            return node;
        };
        FunctionDecl source;
        source.name = "copy";
        source.return_type = builtin_type(BuiltinType::U32);
        source.body = child(Statement::Kind::Label);
        auto& root = *source.body;
        root.label_name = "label";
        root.label_location = {file, 0, 1, 1};
        root.label_binding.kind = LabelBinding::Kind::Definition;
        root.label_binding.scope = source.function_scope;
        root.label_fresh = fresh;
        root.global_label = true;
        root.assertion_message = "retained message";
        root.condition = integer();
        root.increments.push_back(integer());
        root.attributes.emplace_back();
        auto& attribute = root.attributes.back();
        attribute.name = "align";
        attribute.arguments = {"7u32"};
        attribute.location = root.location;
        attribute.expression_argument = integer();
        root.declaration = std::make_unique<VariableDecl>();
        auto& variable = *root.declaration;
        variable.name = "local";
        variable.location = root.location;
        variable.binding.kind = ValueBinding::Kind::Local;
        variable.type = array_type(builtin_type(BuiltinType::U8, true), 7);
        variable.initializer = integer();
        variable.dynamic_array_bound = integer();
        variable.storage_stack = true;
        variable.explicit_alignment = 8;
        variable.location_name = "selected-storage";
        variable.attributes = root.attributes;
        root.statements.push_back(child(Statement::Kind::Empty));
        root.statements.push_back(child(Statement::Kind::Return));
        root.first = child(Statement::Kind::Break);
        root.second = child(Statement::Kind::Continue);
        auto& deep = edge == 0 ? root.statements.front() : edge == 1 ? root.first : root.second;
        for (unsigned index = 0; index < 50000; ++index) {
            auto parent = std::make_unique<Statement>();
            parent->kind = edge == 0 ? Statement::Kind::Compound : Statement::Kind::If;
            parent->location = root.location;
            if (edge == 0) parent->statements.push_back(std::move(deep));
            else if (edge == 1) parent->first = std::move(deep);
            else parent->second = std::move(deep);
            deep = std::move(parent);
        }
        auto copy = copy_evaluation_declaration(source);
        require(copy && copy->body && copy->body.get() != source.body.get() &&
            copy->function_scope == source.function_scope, "statement copy changed source function ownership");
        const auto expression = [&](const auto& a, const auto& b) {
            require(bool(a) == bool(b), "statement copy changed a payload edge");
            if (a) require(a.get() != b.get() && a->kind == b->kind && a->text == b->text &&
                a->name_context == b->name_context && same_location(a->location, b->location),
                "statement copy changed payload identity/location or retained a mutable source");
        };
        const auto attributes = [&](const std::vector<Attribute>& a, const std::vector<Attribute>& b) {
            require(a.size() == b.size(), "statement copy changed attribute count");
            for (std::size_t index = 0; index < a.size(); ++index) {
                require(a[index].name == b[index].name && a[index].arguments == b[index].arguments &&
                    same_location(a[index].location, b[index].location), "statement copy changed attribute metadata");
                expression(a[index].expression_argument, b[index].expression_argument);
            }
        };
        std::vector<std::pair<const Statement*, const Statement*>> pending{{source.body.get(), copy->body.get()}};
        const auto pair = [&](const auto& a, const auto& b) {
            require(bool(a) == bool(b), "statement copy changed child presence");
            if (a) pending.emplace_back(a.get(), b.get());
        };
        while (!pending.empty()) {
            const auto [a, b] = pending.back();
            pending.pop_back();
            require(a != b && a->kind == b->kind && same_location(a->location, b->location) &&
                a->label_name == b->label_name && same_location(a->label_location, b->label_location) &&
                a->label_binding == b->label_binding && a->label_fresh == b->label_fresh &&
                a->global_label == b->global_label && a->assertion_message == b->assertion_message &&
                a->statements.size() == b->statements.size(), "statement copy changed shape or label identity");
            attributes(a->attributes, b->attributes);
            expression(a->expression, b->expression);
            expression(a->condition, b->condition);
            require(a->increments.size() == b->increments.size(), "statement copy changed increments");
            for (std::size_t index = 0; index < a->increments.size(); ++index)
                expression(a->increments[index], b->increments[index]);
            require(bool(a->declaration) == bool(b->declaration), "statement copy changed declaration presence");
            if (a->declaration) {
                const auto& av = *a->declaration;
                const auto& bv = *b->declaration;
                require(&av != &bv && av.name == bv.name && av.binding == bv.binding &&
                    same_location(av.location, bv.location) && same_type(av.type, bv.type) &&
                    av.storage_register == bv.storage_register && av.storage_stack == bv.storage_stack &&
                    av.storage_static == bv.storage_static && av.explicit_alignment == bv.explicit_alignment &&
                    av.location_name == bv.location_name, "statement copy changed declaration metadata");
                expression(av.initializer, bv.initializer);
                expression(av.dynamic_array_bound, bv.dynamic_array_bound);
                attributes(av.attributes, bv.attributes);
            }
            pair(a->first, b->first);
            pair(a->second, b->second);
            for (std::size_t index = 0; index < a->statements.size(); ++index)
                pair(a->statements[index], b->statements[index]);
        }
        // Normal owners exercise all three deep edge families; no Cleanup
        // helper detaches this fixture. Payload/shared metadata must release too.
        copy.reset();
        source.body.reset();
        require(context.use_count() == 1 && fresh.use_count() == 1,
            "statement release leaked a child or shared payload/label metadata");
    }
}

void expression_copies() {
    const auto same_location = [](SourceLocation a, SourceLocation b) {
        return a.file == b.file && a.offset == b.offset && a.line == b.line && a.column == b.column;
    };
    const auto same_optional_type = [](const TypePtr& a, const TypePtr& b) {
        return !a && !b ? true : same_type(a, b);
    };
    for (unsigned edge = 0; edge < 3; ++edge) {
        SourceManager sources;
        const auto* file = sources.add("expression-copy.x", "copy");
        auto context = std::make_shared<NameLookupContext>();
        context->kind = NameLookupContext::Kind::Exact;
        auto member = std::make_shared<FreshIdentifier>();
        member->prefix = "field";
        const auto integer = [&] {
            auto node = std::make_unique<Expr>();
            node->kind = Expr::Kind::Integer;
            node->text = "7u32";
            node->location = {file, 2, 1, 3};
            node->name_context = context;
            node->evaluated_integer = Expr::IntegerConstant{UInt128{7}, BuiltinType::U32};
            return node;
        };
        ObjectDecl source;
        source.name = "copy";
        source.type = builtin_type(BuiltinType::U32);
        source.initializer = integer();
        auto& root = *source.initializer;
        root.kind = Expr::Kind::Conditional;
        root.string_value = "retained metadata";
        root.type = pointer_type(builtin_type(BuiltinType::U8, true));
        root.deferred_generic_signature = function_type(builtin_type(BuiltinType::U32), {}, false, "selected_alias");
        root.generic_visible_at_call = true;
        root.quote_fragments.emplace_back();
        root.evaluated_floating = Expr::FloatingConstant{UInt128{17}, BuiltinType::F32};
        AddressConstant address;
        address.kind = AddressConstant::Kind::Object;
        address.object = &source;
        address.addend = 3;
        root.evaluated_address = address;
        root.object_relocations.push_back({3, 4, source.type, address});
        root.left = integer();
        root.right = integer();
        root.right->kind = Expr::Kind::Call;
        root.right->left = integer();
        root.right->arguments.push_back(integer());
        root.right->generic_arguments.push_back({pointer_type(builtin_type(BuiltinType::U32)), integer()});
        root.third = integer();
        root.third->kind = Expr::Kind::AggregateInitializer;
        root.third->initializer_entries.emplace_back();
        auto& entry = root.third->initializer_entries.back();
        entry.location = {file, 1, 1, 2};
        entry.value = integer();
        entry.designators.emplace_back();
        entry.designators.back().kind = Expr::InitializerDesignator::Kind::Index;
        entry.designators.back().index = integer();
        entry.designators.emplace_back();
        entry.designators.back().kind = Expr::InitializerDesignator::Kind::Member;
        entry.designators.back().member = "field";
        entry.designators.back().member_fresh = member;
        auto& deep = edge == 0 ? root.left : edge == 1 ? root.right->generic_arguments.front().value
            : entry.designators.front().index;
        for (unsigned index = 0; index < 50000; ++index) {
            auto parent = std::make_unique<Expr>();
            parent->kind = Expr::Kind::Parenthesized;
            parent->left = std::move(deep);
            deep = std::move(parent);
        }
        auto copy = copy_evaluation_declaration(source);
        require(copy && copy->initializer && copy->initializer.get() != source.initializer.get(),
            "expression copy reused its mutable source owner");
        const auto& copied = *copy->initializer;
        require(copied.evaluated_floating && copied.evaluated_floating->bits == UInt128{17} &&
            copied.evaluated_address == root.evaluated_address && copied.quote_fragments.size() == 1 &&
            copied.object_relocations.size() == 1 &&
            std::get<AddressConstant>(copied.object_relocations.front().address).object == &source &&
            same_type(copied.deferred_generic_signature, root.deferred_generic_signature),
            "expression copy lost retained symbolic/callable/quote metadata");
        std::vector<std::pair<const Expr*, const Expr*>> pending{{source.initializer.get(), copy->initializer.get()}};
        while (!pending.empty()) {
            const auto [old, fresh] = pending.back();
            pending.pop_back();
            require(old && fresh && old != fresh && old->kind == fresh->kind && old->text == fresh->text &&
                old->name_context == fresh->name_context && old->translation_context == fresh->translation_context &&
                old->string_value == fresh->string_value && same_location(old->location, fresh->location) &&
                old->generic_visible_at_call == fresh->generic_visible_at_call && same_optional_type(old->type, fresh->type) &&
                old->arguments.size() == fresh->arguments.size() &&
                old->generic_arguments.size() == fresh->generic_arguments.size() &&
                old->initializer_entries.size() == fresh->initializer_entries.size(),
                "expression copy changed metadata, type, source span or tree shape");
            const auto pair = [&](const std::unique_ptr<Expr>& a, const std::unique_ptr<Expr>& b) {
                require(static_cast<bool>(a) == static_cast<bool>(b), "expression copy dropped an owned edge");
                if (a) pending.emplace_back(a.get(), b.get());
            };
            pair(old->left, fresh->left);
            pair(old->right, fresh->right);
            pair(old->third, fresh->third);
            for (std::size_t index = 0; index < old->arguments.size(); ++index)
                pair(old->arguments[index], fresh->arguments[index]);
            for (std::size_t index = 0; index < old->generic_arguments.size(); ++index) {
                require(same_optional_type(old->generic_arguments[index].type, fresh->generic_arguments[index].type),
                    "expression copy changed a generic type argument");
                pair(old->generic_arguments[index].value, fresh->generic_arguments[index].value);
            }
            for (std::size_t index = 0; index < old->initializer_entries.size(); ++index) {
                const auto& a = old->initializer_entries[index];
                const auto& b = fresh->initializer_entries[index];
                require(same_location(a.location, b.location) && a.designators.size() == b.designators.size(),
                    "expression copy changed initializer ownership");
                for (std::size_t designator = 0; designator < a.designators.size(); ++designator) {
                    const auto& ad = a.designators[designator];
                    const auto& bd = b.designators[designator];
                    require(ad.kind == bd.kind && same_location(ad.location, bd.location) && ad.member == bd.member &&
                        ad.member_fresh == bd.member_fresh, "expression copy changed private member identity");
                    pair(ad.index, bd.index);
                }
                pair(a.value, b.value);
            }
        }
        // Use normal owner release, not the synthetic Cleanup helper: this
        // independently exercises allocation-free deep Expr destruction.
        copy.reset();
        source.initializer.reset();
        require(context.use_count() == 1 && member.use_count() == 1,
            "expression teardown leaked shared metadata or an owned child");
    }
}

void case_dependency_controls() {
    for (const unsigned bits : {32U, 64U}) {
        for (const auto query : {"sizeof(value)", "$::alignof(value)"}) {
            SourceManager sources;
            std::ostringstream messages;
            Diagnostics diagnostics(messages);
            const auto* source = sources.add("fixed-case.x", std::string(
                "[[eval_only]] static u32 helper(in u32 value) { switch (0u32) { case ") + query +
                ": return 1u32; case 4u32: return 2u32; } return 0u32; }");
            auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
            require(!diagnostics.errors(), "fixed case control did not parse");
            program.address_bits = bits;
            program.evaluation_size_of = [](const TypePtr&) -> std::optional<std::uint64_t> { return 4; };
            program.evaluation_align_of = program.evaluation_size_of;
            require(!expand_evaluation_async(program, diagnostics, false).run() && diagnostics.errors() == 1 &&
                messages.str().find("duplicate case value in switch") != std::string::npos,
                "layout operand acquired invocation-value dependence");
        }
        for (const auto body : {
                "switch (0u32) { case value: return 1u32; }",
                "u8 object[value]; switch (0u32) { case sizeof(object): return 1u32; }"}) {
            SourceManager sources;
            std::ostringstream messages;
            Diagnostics diagnostics(messages);
            const auto* source = sources.add("dependent-case.x", std::string(
                "[[eval_only]] static u32 helper(in u32 value) { ") + body + " return 0u32; }");
            auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
            require(!diagnostics.errors(), "dependent case control did not parse");
            program.address_bits = bits;
            const bool valid = expand_evaluation_async(program, diagnostics, false).run();
            if (!valid) std::cerr << messages.str();
            require(valid && !diagnostics.errors() && program.evaluation_definitions.size() == 1,
                "invocation-dependent case lost its existing definition-time deferral");
        }
    }
}

void ordinary(unsigned bits, EvaluationByteOrder order, bool optional) {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("rewriting.x", R"cross(
static uptr proof() { return sizeof(uptr); }
uptr forced() { return $::eval(proof()); }
uptr optional() { return proof(); }
uptr runtime() { return $::runtime(proof()); }
uptr tail() { [[musttail]] return proof(); }
[[eval_only]] static uptr required_helper() { return sizeof(uptr); }
uptr required_call() { return required_helper(); }
static $::meta::bytes make_bytes() {
    $::meta::buffer storage = $::meta::alloc(sizeof(uptr));
    u8 *data = $::meta::data(storage);
    for (uptr index = 0uptr; index < sizeof(uptr); ++index) data[index] = (u8)index;
    return $::meta::freeze(storage, sizeof(uptr));
}
u8 materialized[] = make_bytes();
uptr width = proof();
f64 floating = (f64)proof();
uptr values[2] = { [1uptr] = proof(), [0uptr] = 3uptr };
$::static_assert(proof() != 0uptr, "selected width");
)cross");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits).parse();
    require(!diagnostics.errors() && program.functions.size() == 8, "rewriting fixture did not parse");
    program.address_bits = bits;
    program.evaluation_layout.byte_order = order;
    ContinuationSchedule* expected{};
    bool continuous = true;
    unsigned queries{};
    program.evaluation_size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++queries;
        co_return type->builtin == BuiltinType::Uptr ? bits / 8U : type_bits(type) / 8U;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    const auto run = [&]() -> ContinuationTask<bool> {
        co_await Witness{expected, continuous};
        co_return co_await expand_evaluation_async(program, diagnostics, optional);
    };
    const bool valid = run().run();
    if (!valid) std::cerr << messages.str();
    require(valid && queries && continuous, "rewriting used a nested continuation pump");
    const auto& forced = *program.functions[1]->body->statements[0]->expression;
    require(forced.evaluated_integer && forced.evaluated_integer->value == UInt128{bits / 8U},
        "forced evaluation did not use the selected width");
    const auto& folded = *program.functions[2]->body->statements[0]->expression;
    require(static_cast<bool>(folded.evaluated_integer) == optional, "optional call folding control changed");
    const auto& runtime = *program.functions[3]->body->statements[0]->expression;
    require(runtime.kind == Expr::Kind::Call && runtime.left->text == "proof", "runtime barrier folded a call");
    const auto& tail = *program.functions[4]->body->statements[0]->expression;
    require(tail.kind == Expr::Kind::Call && tail.left->text == "proof", "musttail root call was folded");
    require(program.functions.size() == 6 && program.evaluation_definitions.size() == 2 &&
        program.functions[5]->body->statements[0]->expression->evaluated_integer,
        "required call did not fold before retaining translation-only helpers");
    require(program.objects[0]->type->lanes == bits / 8U &&
        program.objects[0]->initializer->kind == Expr::Kind::ByteSequence &&
        program.objects[0]->initializer->string_value.size() == bits / 8U,
        "byte materialization lost its target bound or meta helper");
    require(program.objects[1]->initializer->evaluated_integer &&
        program.objects[2]->initializer->evaluated_floating &&
        program.objects[3]->initializer->initializer_entries[0].value->evaluated_integer &&
        program.static_assertions[0].condition->evaluated_integer,
        "required object/assertion proofs were lost or made optional");
    require(!program.evaluation_required_integer && !program.evaluation_generic_value &&
        !program.evaluation_prepare_layout && !program.evaluation_layout_scope,
        "rewriting retained evaluator services");
}

void publication_and_exception() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("rewriting-publication.x", R"cross(
static u32 proof() { return (u32)sizeof(uptr); }
struct Bits { u32 field : proof(); };
$::static_assert(sizeof(uptr) != 0uptr, "width");
)cross");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
    require(!diagnostics.errors() && program.records.size() == 1, "publication fixture did not parse");
    unsigned queries{};
    bool published{};
    bool assertion_published{};
    program.evaluation_size_of = [&](const TypePtr&) -> std::optional<std::uint64_t> {
        ++queries;
        if (!published) {
            published = true;
            require(static_cast<bool>(program.records[0].members[0].bit_width),
                "active bit-field requirement disappeared during a nested layout query");
            program.records.reserve(program.records.capacity() + 8);
            program.records[0].members.reserve(program.records[0].members.capacity() + 8);
            auto added = std::make_unique<ObjectDecl>();
            added->name = "published";
            added->type = builtin_type(BuiltinType::U32);
            added->initializer = std::make_unique<Expr>();
            added->initializer->kind = Expr::Kind::Integer;
            added->initializer->text = "7u32";
            program.objects.reserve(program.objects.capacity() + 8);
            program.objects.push_back(std::move(added));
            throw std::runtime_error("layout callback");
        }
        if (!assertion_published && !program.static_assertions[0].condition) {
            assertion_published = true;
            StaticAssertDecl added;
            added.condition = std::make_unique<Expr>();
            added.condition->kind = Expr::Kind::Integer;
            added.condition->text = "1u32";
            program.static_assertions.reserve(program.static_assertions.capacity() + 8);
            program.static_assertions.push_back(std::move(added));
        }
        return 8;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    bool threw{};
    try { (void)expand_evaluation_async(program, diagnostics, false).run(); }
    catch (const std::runtime_error&) { threw = true; }
    require(threw && !diagnostics.errors() && program.records[0].members[0].bit_width &&
        !program.evaluation_required_integer && !program.evaluation_generic_value &&
        !program.evaluation_prepare_layout && !program.evaluation_layout_scope,
        "record requirement/source services did not restore after publication and exception");
    require(expand_evaluation_async(program, diagnostics, false).run() && queries > 1 && assertion_published &&
        program.records[0].members[0].bit_width->evaluated_integer &&
        program.records[0].members[0].bit_width->evaluated_integer->value == UInt128{8} &&
        program.objects[0]->initializer->evaluated_integer && program.static_assertions.size() == 2 &&
        program.static_assertions[0].condition->evaluated_integer &&
        program.static_assertions[1].condition->evaluated_integer,
        "fresh rewriting did not restore/finalize the published source");
}

void resource_stopping() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("rewriting-resources.x",
        "uptr first = sizeof(uptr) + 0uptr; uptr second = sizeof(u16);");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
    require(!diagnostics.errors(), "resource fixture did not parse");
    std::vector<BuiltinType> queried;
    program.evaluation_size_of = [&](const TypePtr& type) -> std::optional<std::uint64_t> {
        queried.push_back(type->builtin);
        return type->builtin == BuiltinType::Uptr ? 8 : 2;
    };
    program.evaluation_align_of = program.evaluation_size_of;
    program.evaluation_limits.steps = 1;
    const auto epoch = program.evaluation_resource_errors;
    require(!expand_evaluation_async(program, diagnostics, false).run() &&
        diagnostics.errors() == 1 && program.evaluation_resource_errors == epoch + 1 &&
        queried.empty() && !program.evaluation_required_integer && !program.evaluation_generic_value,
        "resource exhaustion continued siblings or retained evaluator services");
    // The stage's existing global error gate is preserved. Independent fresh
    // diagnostics allow a retry without clearing the earlier diagnostic owner.
    std::ostringstream fresh_messages;
    Diagnostics fresh(fresh_messages);
    program.evaluation_limits.steps = 1000000;
    require(expand_evaluation_async(program, fresh, false).run() && !fresh.errors() &&
        diagnostics.errors() == 1 && queried == std::vector{BuiltinType::Uptr, BuiltinType::U16},
        "independent fresh rewriting inherited exhaustion or erased old diagnostics");
}

void source_scopes() {
    for (const bool invalid : {false, true}) {
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto text = invalid
            ? "void test() { const u32 value = 1u32; { u32 value = 2u32; value = 3u32; } value = 4u32; }"
            : "void test() { const u32 value = 1u32; { u32 value = 2u32; value = 3u32; } }";
        const auto* source = sources.add("rewriting-scopes.x", text);
        auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
        require(!diagnostics.errors(), "scope fixture did not parse");
        const bool valid = expand_evaluation_async(program, diagnostics, false).run();
        require(valid != invalid && (invalid ? messages.str().find("cannot write a const cell") != std::string::npos
                                             : !diagnostics.errors()),
            "heap source validation changed declaration shadowing or scope restoration");
    }
}

void query_resource() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto* source = sources.add("rewriting-query-resource.x", "uptr query() { return sizeof(uptr); }");
    auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
    require(!diagnostics.errors(), "query resource fixture did not parse");
    program.evaluation_limits.steps = 0;
    unsigned queries{};
    program.evaluation_size_of = [&](const TypePtr&) -> std::optional<std::uint64_t> { ++queries; return 8; };
    program.evaluation_align_of = program.evaluation_size_of;
    const bool valid = expand_evaluation_async(program, diagnostics, false).run();
    if (diagnostics.errors() != 1)
        std::cerr << "query resource result=" << valid << " errors=" << diagnostics.errors() << '\n';
    require(!valid && diagnostics.errors() == 1 && program.evaluation_resource_errors == 1 && !queries &&
        messages.str().find("translation-time instruction budget exceeded 0") != std::string::npos,
        "a deferred layout query swallowed its new resource failure");
}

void optional_resource() {
    for (const bool optional : {false, true}) {
        SourceManager sources;
        std::ostringstream messages;
        Diagnostics diagnostics(messages);
        const auto* source = sources.add("rewriting-optional-resource.x",
            "static u32 add(in u32 value) { return value + 1u32; } u32 test() { return add(7u32); }");
        auto program = Parser(Lexer(*source, diagnostics).lex(), diagnostics).parse();
        require(!diagnostics.errors(), "optional resource fixture did not parse");
        program.evaluation_limits.steps = 1;
        const bool valid = expand_evaluation_async(program, diagnostics, optional).run();
        if (!valid) std::cerr << "optional resource result=" << valid << " errors=" << diagnostics.errors() << '\n';
        require(valid && !diagnostics.errors() && program.evaluation_resource_errors == 0 &&
            program.functions[1]->body->statements[0]->expression->kind == Expr::Kind::Call,
            "optional exhaustion became a mandatory resource error instead of a runtime call");
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::string_view(argv[1]) == "--deep-case") deep_case_labels();
        else if (std::string_view(argv[1]) == "--deep-block") deep_private_statements();
        else if (std::string_view(argv[1]) == "--deep-type") deep_type_copies();
        else if (std::string_view(argv[1]) == "--release-type") deep_type_copies();
        else if (std::string_view(argv[1]) == "--release-mixed") deep_type_copies(3);
        else deep_statements();
        return 0;
    }
    for (const auto bits : {32U, 64U})
        for (const auto order : {EvaluationByteOrder::Little, EvaluationByteOrder::Big})
            for (const bool optional : {false, true}) ordinary(bits, order, optional);
    deep_statements();
    deep_case_labels();
    deep_private_statements();
    statement_copies();
    deep_type_copies();
    shared_release_controls();
    expression_copies();
    case_dependency_controls();
    publication_and_exception();
    resource_stopping();
    source_scopes();
    query_resource();
    optional_resource();
}
