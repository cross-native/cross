// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"
#include "target/subtarget.hpp"
#include "target/x86_64/manual_abi_plan.hpp"
#include "target/x86_64/target.hpp"

#include <iostream>
#include <sstream>
#include <string_view>

namespace {

using namespace cross;
using namespace cross::x86_64;

hir::Parameter parameter(hir::TypeId type, ParameterMode mode,
                         std::string_view location = {}) {
    hir::Parameter result;
    result.type = type;
    result.mode = mode;
    if (!location.empty()) result.physical_location = location;
    return result;
}

hir::Function function(std::uint32_t id, hir::TypeId result,
                       std::string_view abi = "ms_abi") {
    hir::Function item;
    item.id = {id};
    item.result_type = result;
    if (const auto* model =
            find_abi(x86_64_target(), abi, {})) {
        item.abi = model->id;
    }
    return item;
}

bool register_is(const ManualBoundary& boundary, std::string_view name,
                 ManualBoundaryKind kind) {
    return boundary.kind == kind && boundary.register_view &&
           boundary.register_view->name == name;
}

int fail(std::string_view message, const std::ostringstream& diagnostics) {
    std::cerr << "manual ABI planner test failed: " << message << '\n';
    if (!diagnostics.str().empty()) std::cerr << diagnostics.str();
    return 1;
}

} // namespace

int main() {
    constexpr hir::TypeId void_id{0};
    constexpr hir::TypeId i32_id{1};
    constexpr hir::TypeId f80_id{2};

    hir::Module module;
    module.types = {
        {hir::Type::Kind::Builtin, BuiltinType::Void, std::nullopt,
         false, false, {}, std::nullopt, 0, false, false, std::nullopt},
        {hir::Type::Kind::Builtin, BuiltinType::I32, std::nullopt,
         false, false, {}, std::nullopt, 0, false, false, std::nullopt},
        {hir::Type::Kind::Builtin, BuiltinType::F80, std::nullopt,
         false, false, {}, std::nullopt, 0, false, false, std::nullopt},
    };

    auto registers = function(0, void_id);
    registers.parameters = {
        parameter(i32_id, ParameterMode::In, "eax"),
        parameter(i32_id, ParameterMode::InOut, "*r11"),
        parameter(i32_id, ParameterMode::Out, "auto=>r10d"),
        parameter(i32_id, ParameterMode::In),
    };
    module.functions.push_back(std::move(registers));

    auto stack = function(1, i32_id);
    stack.parameters = {
        parameter(i32_id, ParameterMode::InOut, "push=>pop"),
        parameter(i32_id, ParameterMode::In, "stack+16"),
    };
    stack.result_location = "stack+32";
    module.functions.push_back(std::move(stack));

    auto x87 = function(2, f80_id, "sysv_abi");
    x87.parameters = {
        parameter(f80_id, ParameterMode::In, "st0"),
        parameter(f80_id, ParameterMode::InOut, "st1"),
        parameter(f80_id, ParameterMode::Out, "st0"),
    };
    x87.result_location = "st2";
    module.functions.push_back(std::move(x87));

    FunctionDecl cleanup_declaration;
    cleanup_declaration.attributes.push_back(
        {"stack_cleanup", {"\"callee\""}, {}});
    auto cleanup = function(3, void_id);
    cleanup.parameters = {
        parameter(i32_id, ParameterMode::In, "push=>discard"),
        parameter(i32_id, ParameterMode::Out, "r10d"),
    };
    cleanup.declarations.push_back(&cleanup_declaration);
    module.functions.push_back(std::move(cleanup));

    auto memory = function(4, f80_id);
    memory.parameters = {
        parameter(f80_id, ParameterMode::In),
        parameter(f80_id, ParameterMode::InOut, "stack"),
    };
    memory.result_location = "stack+32";
    module.functions.push_back(std::move(memory));

    auto channel = function(5, i32_id);
    channel.parameters = {parameter(i32_id, ParameterMode::In, "ecx")};
    channel.result_location = "*r9";
    module.functions.push_back(std::move(channel));

    // The pointed-to type of an indirect call is planned like a function,
    // including its own cleanup rule and extra clobbers.
    hir::FunctionSignature pointer_signature;
    pointer_signature.result_type = i32_id;
    pointer_signature.abi = module.functions.front().abi;
    pointer_signature.parameters = {
        parameter(i32_id, ParameterMode::In, "push=>discard"),
        parameter(i32_id, ParameterMode::In, "edx"),
    };
    pointer_signature.result_location = "*r8";
    pointer_signature.clobbers = {"rbx"};
    pointer_signature.stack_cleanup = "callee";
    const hir::TypeId pointer_signature_id{
        static_cast<std::uint32_t>(module.types.size())};
    hir::Type pointer_function;
    pointer_function.kind = hir::Type::Kind::Function;
    pointer_function.function = pointer_signature;
    module.types.push_back(std::move(pointer_function));
    mir::ManagedModule managed;
    mir::ManagedFunction caller;
    mir::ManagedValue indirect_call;
    indirect_call.kind = mir::ValueKind::Call;
    indirect_call.call_signature = pointer_signature_id;
    caller.values.push_back(indirect_call);
    caller.values.push_back(indirect_call);
    managed.functions.push_back(std::move(caller));

    CompilerOptions options;
    options.target = "x86_64-w64-windows-gnu";
    std::ostringstream diagnostic_text;
    Diagnostics diagnostics(diagnostic_text);
    const auto& target = x86_64_target();
    const auto subtarget =
        resolve_subtarget(target, options, diagnostics);
    if (!subtarget) return fail("failed to resolve test subtarget",
                                diagnostic_text);

    const auto plans = build_manual_abi_plans(
        module, target, *subtarget, options, diagnostics, &managed);
    if (diagnostics.errors() != 0) {
        return fail("unexpected planner diagnostic", diagnostic_text);
    }
    if (plans.entries().size() != 7) {
        return fail("unexpected number of manual plans", diagnostic_text);
    }

    const auto* register_plan = plans.find(hir::FunctionId{0});
    if (!register_plan || !register_plan->valid ||
        !register_is(register_plan->parameters[0].input, "eax",
                     ManualBoundaryKind::DirectRegister) ||
        !register_is(register_plan->parameters[1].input, "r11",
                     ManualBoundaryKind::IndirectRegister) ||
        !register_is(register_plan->parameters[1].output, "r11",
                     ManualBoundaryKind::IndirectRegister) ||
        !register_is(register_plan->parameters[2].output, "r10d",
                     ManualBoundaryKind::DirectRegister) ||
        !register_is(register_plan->parameters[3].input, "r9d",
                     ManualBoundaryKind::DirectRegister)) {
        return fail("register or automatic placement mismatch",
                    diagnostic_text);
    }

    const auto* stack_plan = plans.find(hir::FunctionId{1});
    if (!stack_plan || !stack_plan->valid ||
        stack_plan->parameters[0].input.source_lifo !=
            LifoEndpointForm::push_pop ||
        stack_plan->stack.input_offsets[0] != 0 ||
        stack_plan->stack.output_offsets[0] != 0 ||
        stack_plan->stack.input_offsets[1] != 16 ||
        stack_plan->stack.result_offset != 32 ||
        stack_plan->stack.extent != 36 ||
        stack_plan->stack.home_space_size != 32 ||
        stack_plan->stack.outgoing_area_size != 80) {
        return fail("stack or LIFO layout mismatch", diagnostic_text);
    }

    const auto* x87_plan = plans.find(hir::FunctionId{2});
    if (!x87_plan || !x87_plan->valid || !x87_plan->x87.has_x87 ||
        x87_plan->x87.input_depth != 2 ||
        x87_plan->x87.output_depth != 3 ||
        x87_plan->x87.inputs[0] != 0 ||
        x87_plan->x87.inputs[1] != 1 ||
        x87_plan->x87.outputs[0] != 2 ||
        x87_plan->x87.outputs[1] != 1 ||
        x87_plan->x87.result != 2) {
        return fail("x87 layout mismatch", diagnostic_text);
    }

    const auto* cleanup_plan = plans.find(hir::FunctionId{3});
    if (!cleanup_plan || !cleanup_plan->valid ||
        !cleanup_plan->callee_cleanup ||
        cleanup_plan->stack.input_offsets[0] != 0 ||
        cleanup_plan->stack.outgoing_area_size != 48) {
        return fail("callee-cleanup layout mismatch", diagnostic_text);
    }

    const auto* memory_plan = plans.find(hir::FunctionId{4});
    if (!memory_plan || !memory_plan->valid ||
        // The base Win64 hidden f80 result channel consumes slot zero before
        // the explicit stack result overlays it, so later automatic inputs
        // retain their full-signature slot.
        !register_is(memory_plan->parameters[0].input, "rdx",
                     ManualBoundaryKind::IndirectRegister) ||
        memory_plan->parameters[1].input.kind !=
            ManualBoundaryKind::Stack ||
        memory_plan->parameters[1].output.kind !=
            ManualBoundaryKind::Stack ||
        memory_plan->stack.input_offsets[1] != 0 ||
        memory_plan->stack.output_offsets[1] != 0 ||
        memory_plan->stack.result_offset != 32) {
        return fail("f80 memory or automatic placement mismatch",
                    diagnostic_text);
    }

    const auto* channel_plan = plans.find(hir::FunctionId{5});
    if (!channel_plan || !channel_plan->valid ||
        !register_is(channel_plan->result.output, "r9",
                     ManualBoundaryKind::IndirectRegister) ||
        !register_is(channel_plan->parameters[0].input, "ecx",
                     ManualBoundaryKind::DirectRegister)) {
        return fail("explicit indirect result mismatch", diagnostic_text);
    }

    const auto* pointer_plan = plans.find(pointer_signature_id);
    if (!pointer_plan || !pointer_plan->valid ||
        plans.find(hir::FunctionId{0}) == pointer_plan ||
        plans.find(std::nullopt, pointer_signature_id) != pointer_plan ||
        !pointer_plan->callee_cleanup ||
        pointer_plan->clobbers != std::vector<std::string>{"rbx"} ||
        pointer_plan->parameters[0].input.source_lifo !=
            LifoEndpointForm::push_discard ||
        pointer_plan->stack.input_offsets[0] != 0 ||
        pointer_plan->stack.outgoing_area_size != 48 ||
        !register_is(pointer_plan->parameters[1].input, "edx",
                     ManualBoundaryKind::DirectRegister) ||
        !register_is(pointer_plan->result.output, "r8",
                     ManualBoundaryKind::IndirectRegister)) {
        return fail("indirect call signature plan mismatch",
                    diagnostic_text);
    }

    // A pointed-to type is diagnosed like a declaration, at its call.
    hir::Module invalid = module;
    invalid.types[pointer_signature_id.value].function->result_location =
        "*rsp";
    std::ostringstream invalid_text;
    Diagnostics invalid_diagnostics(invalid_text);
    const auto rejected = build_manual_abi_plans(
        invalid, target, *subtarget, options, invalid_diagnostics, &managed);
    const auto* rejected_plan = rejected.find(pointer_signature_id);
    if (!rejected_plan || rejected_plan->valid ||
        invalid_text.str().find(
            "indirect result cannot use compiler-owned register 'rsp'") ==
            std::string::npos) {
        return fail("invalid indirect call signature was accepted",
                    invalid_text);
    }

    return 0;
}
