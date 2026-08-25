// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/dynamic_abi_plan.hpp"

#include "model/model.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace cross::x86_64 {
namespace {

std::size_t align_up(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

bool is_void(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin == BuiltinType::Void;
}

std::optional<ScalarMode> scalar_mode(const hir::Module& module,
                                      hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) {
        return ScalarMode::pointer();
    }
    if (type.kind != hir::Type::Kind::Builtin) return std::nullopt;
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8:
        return ScalarMode::integer(8);
    case BuiltinType::I16:
    case BuiltinType::U16:
        return ScalarMode::integer(16);
    case BuiltinType::I32:
    case BuiltinType::U32:
        return ScalarMode::integer(32);
    case BuiltinType::I64:
    case BuiltinType::U64:
    case BuiltinType::Iptr:
    case BuiltinType::Uptr:
    case BuiltinType::Label:
        return ScalarMode::integer(64);
    case BuiltinType::I128:
    case BuiltinType::U128:
        return ScalarMode::integer(128);
    case BuiltinType::F32:
        return ScalarMode::floating(32);
    case BuiltinType::F64:
    case BuiltinType::Fptr:
        return ScalarMode::floating(64);
    case BuiltinType::Void:
        return ScalarMode::zero();
    case BuiltinType::F80:
    case BuiltinType::F128:
        return std::nullopt;
    }
    return std::nullopt;
}

std::uint16_t carrier_bits(std::uint16_t bits) {
    return bits <= 8 ? 8 : bits <= 16 ? 16 : bits <= 32 ? 32 : 64;
}

std::optional<std::size_t> returned_parameter(
    const mir::ManagedFunction& function) {
    std::optional<std::size_t> result;
    bool saw_return = false;
    for (const auto& block : function.blocks) {
        if (block.terminator.kind != mir::TerminatorKind::Return ||
            !block.terminator.value) {
            continue;
        }
        saw_return = true;
        const auto& value =
            function.values[block.terminator.value->value];
        if (value.kind != mir::ValueKind::Parameter) return std::nullopt;
        const auto index = static_cast<std::size_t>(value.parameter_index);
        if (result && *result != index) return std::nullopt;
        result = index;
    }
    return saw_return ? result : std::nullopt;
}

bool has_precise_straight_line_scalar_clobbers(
    const hir::Module& module, const mir::ManagedFunction& managed,
    const hir::Function& function) {
    // Keep this proof deliberately smaller than the native selector.  The
    // admitted operations use only their allocated integer destination plus
    // the allocator's RAX/RCX fallback; R8-R10 are the remaining writable
    // colors. Direct dynamic calls contribute their exact argument-placement
    // and transitive callee effects in the whole-module pass below. Any
    // operation whose emitter has a less explicit fixed-register contract
    // retains the conservative dynamic-ABI set.
    if (managed.blocks.size() != 1 ||
        managed.blocks.front().terminator.kind !=
            mir::TerminatorKind::Return) {
        return false;
    }
    const auto simple_integer = [&](hir::TypeId type, bool allow_void) {
        const auto mode = scalar_mode(module, type);
        return mode &&
            ((allow_void && mode->bits == 0) ||
             (mode->kind == ScalarKind::Integer && mode->bits != 0 &&
              mode->bits <= 64));
    };
    if (!simple_integer(function.result_type, true) ||
        !std::all_of(
            function.parameters.begin(), function.parameters.end(),
            [&](const hir::Parameter& parameter) {
                return parameter.mode == ParameterMode::In &&
                       simple_integer(parameter.type, false);
            })) {
        return false;
    }

    for (const auto id : managed.blocks.front().values) {
        const auto& value = managed.values[id.value];
        const bool metadata_only =
            value.kind == mir::ValueKind::LifetimeStart ||
            value.kind == mir::ValueKind::LifetimeEnd;
        if (!simple_integer(
                value.type,
                value.kind == mir::ValueKind::Call || metadata_only)) {
            return false;
        }
        switch (value.kind) {
        case mir::ValueKind::Parameter:
        case mir::ValueKind::ConstantInteger:
        case mir::ValueKind::Unary:
        case mir::ValueKind::Select:
        case mir::ValueKind::LifetimeStart:
        case mir::ValueKind::LifetimeEnd:
            break;
        case mir::ValueKind::Binary:
            switch (value.binary) {
            case mir::BinaryOperation::Add:
            case mir::BinaryOperation::Subtract:
            case mir::BinaryOperation::Multiply:
            case mir::BinaryOperation::BitAnd:
            case mir::BinaryOperation::BitOr:
            case mir::BinaryOperation::BitXor:
            case mir::BinaryOperation::ShiftLeft:
            case mir::BinaryOperation::ShiftRightArithmetic:
            case mir::BinaryOperation::ShiftRightLogical:
            case mir::BinaryOperation::RotateLeft:
            case mir::BinaryOperation::RotateRight:
            case mir::BinaryOperation::Equal:
            case mir::BinaryOperation::NotEqual:
            case mir::BinaryOperation::SignedLess:
            case mir::BinaryOperation::SignedLessEqual:
            case mir::BinaryOperation::SignedGreater:
            case mir::BinaryOperation::SignedGreaterEqual:
            case mir::BinaryOperation::UnsignedLess:
            case mir::BinaryOperation::UnsignedLessEqual:
            case mir::BinaryOperation::UnsignedGreater:
            case mir::BinaryOperation::UnsignedGreaterEqual:
                break;
            case mir::BinaryOperation::SignedDivide:
            case mir::BinaryOperation::UnsignedDivide:
            case mir::BinaryOperation::SignedRemainder:
            case mir::BinaryOperation::UnsignedRemainder:
                return false;
            }
            break;
        case mir::ValueKind::Cast:
            if (value.cast != mir::CastOperation::SignExtend &&
                value.cast != mir::CastOperation::ZeroExtend &&
                value.cast != mir::CastOperation::Truncate &&
                value.cast != mir::CastOperation::Reinterpret) {
                return false;
            }
            break;
        case mir::ValueKind::Intrinsic:
            if (value.intrinsic != mir::IntrinsicOperation::Expect) {
                return false;
            }
            break;
        case mir::ValueKind::Call: {
            if (!value.callee) return false;
            const auto& callee = module.function(*value.callee);
            if (callee.abi_contract != hir::AbiContract::Dynamic ||
                callee.ownership != hir::BodyOwnership::ManagedMir ||
                value.call_arguments.size() != callee.parameters.size() ||
                value.call_arguments.size() > 7) {
                return false;
            }
            for (std::size_t index = 0;
                 index < value.call_arguments.size(); ++index) {
                const auto& argument = value.call_arguments[index];
                if (!argument.value || argument.cell ||
                    callee.parameters[index].mode != ParameterMode::In ||
                    !simple_integer(argument.type, false)) {
                    return false;
                }
            }
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

std::vector<std::size_t> allocation_order(
    const mir::ManagedFunction& function, std::size_t parameter_count,
    std::optional<std::size_t> return_affinity) {
    const auto never = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> first_use(parameter_count, never);
    std::vector<std::optional<std::size_t>> parameter_for_value(
        function.values.size());
    for (const auto parameter : function.parameters) {
        const auto index = function.values[parameter.value].parameter_index;
        if (index < parameter_count) {
            parameter_for_value[parameter.value] =
                static_cast<std::size_t>(index);
        }
    }
    std::size_t order = 0;
    const auto observe = [&](mir::ValueId id, std::size_t at) {
        if (id.value < parameter_for_value.size() &&
            parameter_for_value[id.value]) {
            auto& use = first_use[*parameter_for_value[id.value]];
            use = std::min(use, at);
        }
    };
    for (const auto& block : function.blocks) {
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            for (const auto operand : value.operands) observe(operand, order);
            for (const auto& argument : value.call_arguments) {
                if (argument.value) observe(*argument.value, order);
            }
            ++order;
        }
        if (block.terminator.value) {
            observe(*block.terminator.value, order);
        }
        ++order;
    }

    std::vector<std::size_t> result(parameter_count);
    std::iota(result.begin(), result.end(), 0);
    std::stable_sort(
        result.begin(), result.end(),
        [&](std::size_t left, std::size_t right) {
            if (return_affinity) {
                if (left == *return_affinity) return true;
                if (right == *return_affinity) return false;
            }
            return first_use[left] < first_use[right];
        });
    return result;
}

bool build_plan(const hir::Module& module,
                const mir::ManagedFunction& managed,
                const hir::Function& function,
                DynamicAbiPlan& plan) {
    static constexpr std::array<std::string_view, 7> gprs{
        "rax", "r11", "r10", "r9", "r8", "rcx", "rdx"};
    static constexpr std::array<std::string_view, 6> simd{
        "xmm0", "xmm5", "xmm4", "xmm3", "xmm2", "xmm1"};

    std::vector<std::optional<ScalarMode>> modes;
    modes.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        auto mode = scalar_mode(module, parameter.type);
        if (!mode) return false;
        modes.push_back(mode);
    }
    const auto result_mode = scalar_mode(module, function.result_type);
    if (!result_mode) return false;

    plan.function = function.id;
    plan.call.arguments.resize(function.parameters.size());
    const auto affinity = returned_parameter(managed);
    const auto order =
        allocation_order(managed, function.parameters.size(), affinity);
    std::size_t next_gpr = 0;
    std::size_t next_simd = 0;
    std::size_t stack_offset = 0;

    for (const auto index : order) {
        const auto& parameter = function.parameters[index];
        const bool by_reference = parameter.mode != ParameterMode::In;
        const auto logical = *modes[index];
        const auto transported =
            by_reference ? ScalarMode::pointer() : logical;
        const auto pieces =
            transported.kind == ScalarKind::Floating
                ? 1U
                : static_cast<unsigned>((transported.bits + 63U) / 64U);
        auto& assignment = plan.call.arguments[index];
        assignment.argument_index = index;
        assignment.value =
            AbiValue{logical, by_reference ? ValueTransport::ByReference
                                           : ValueTransport::Direct};
        const bool use_simd =
            !by_reference && logical.kind == ScalarKind::Floating;
        const bool has_registers =
            use_simd ? next_simd + pieces <= simd.size()
                     : next_gpr + pieces <= gprs.size();
        if (has_registers) {
            for (unsigned piece = 0; piece < pieces; ++piece) {
                const auto bits = static_cast<std::uint16_t>(
                    std::min<unsigned>(
                        64U, transported.bits - piece * 64U));
                assignment.pieces.push_back(
                    {{LocationKind::Register,
                      std::string(use_simd ? simd[next_simd++]
                                           : gprs[next_gpr++]),
                      0},
                     static_cast<std::uint16_t>(piece * 64U), bits,
                     carrier_bits(bits)});
            }
        } else {
            const auto bytes =
                std::max<std::size_t>(1, (transported.bits + 7U) / 8U);
            const auto alignment = std::min<std::size_t>(8, bytes);
            stack_offset = align_up(stack_offset, alignment);
            assignment.stack_size = align_up(bytes, 8);
            assignment.stack_alignment = alignment;
            for (unsigned piece = 0; piece < pieces; ++piece) {
                const auto bits = static_cast<std::uint16_t>(
                    std::min<unsigned>(
                        64U, transported.bits - piece * 64U));
                assignment.pieces.push_back(
                    {{LocationKind::Stack, {},
                      stack_offset + piece * 8U},
                     static_cast<std::uint16_t>(piece * 64U), bits,
                     carrier_bits(bits)});
            }
            stack_offset += assignment.stack_size;
        }
    }
    plan.call.used_stack_size = stack_offset;
    plan.call.outgoing_area_alignment = 16;
    plan.call.outgoing_area_size =
        stack_offset == 0 ? 0 : align_up(stack_offset, 16);

    if (!is_void(module, function.result_type)) {
        ReturnAssignment result;
        result.value = AbiValue{*result_mode};
        result.mode = *result_mode;
        bool reused = false;
        if (affinity && *affinity < plan.call.arguments.size() &&
            function.parameters[*affinity].mode == ParameterMode::In) {
            const auto& argument = plan.call.arguments[*affinity];
            if (argument.value.mode.kind == result.mode.kind &&
                argument.value.mode.bits == result.mode.bits &&
                std::all_of(
                    argument.pieces.begin(), argument.pieces.end(),
                    [](const ValuePiece& piece) {
                        return piece.location.kind ==
                               LocationKind::Register;
                    })) {
                result.pieces = argument.pieces;
                reused = true;
            }
        }
        if (!reused) {
            const bool floating =
                result.mode.kind == ScalarKind::Floating;
            const auto count = floating
                                   ? 1U
                                   : static_cast<unsigned>(
                                         (result.mode.bits + 63U) / 64U);
            for (unsigned piece = 0; piece < count; ++piece) {
                const auto bits = static_cast<std::uint16_t>(
                    std::min<unsigned>(
                        64U, result.mode.bits - piece * 64U));
                const auto reg =
                    floating
                        ? std::string(simd[piece])
                        : std::string(gprs[piece]);
                result.pieces.push_back(
                    {{LocationKind::Register, reg, 0},
                     static_cast<std::uint16_t>(piece * 64U), bits,
                     carrier_bits(bits)});
            }
        }
        plan.result = std::move(result);
    }

    const bool precise_straight_line =
        has_precise_straight_line_scalar_clobbers(
            module, managed, function);
    if (precise_straight_line) {
        plan.clobbers = {
            "rax", "rcx", "r8", "r9", "r10", "flags",
        };
        if (std::any_of(
                managed.blocks.front().values.begin(),
                managed.blocks.front().values.end(),
                [&](mir::ValueId id) {
                    return managed.values[id.value].kind ==
                           mir::ValueKind::Call;
                })) {
            // Parallel integer argument cycles use XMM2 as a bit bucket.
            plan.clobbers.push_back("xmm2");
        }
    } else {
        // This is the complete conservative scratch set used by native
        // selection. More MIR families can move to the audited path above as
        // their fixed-register effects become explicit.
        plan.clobbers = {
            "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
            "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",
            "flags",
        };
    }
    return true;
}

} // namespace

const DynamicAbiPlan* DynamicAbiPlans::find(hir::FunctionId id) const {
    const auto found = std::find_if(
        entries_.begin(), entries_.end(),
        [id](const DynamicAbiPlan& entry) {
            return entry.function == id;
        });
    return found == entries_.end() ? nullptr : &*found;
}

void DynamicAbiPlans::add(DynamicAbiPlan plan) {
    entries_.push_back(std::move(plan));
}

DynamicAbiPlans build_dynamic_abi_plans(
    const mir::ManagedModule& managed, const hir::Module& hir_module,
    const ManualAbiPlans& manual_plans, const CompilerOptions& options,
    Diagnostics&) {
    DynamicAbiPlans result;
    if (!options.private_abi) return result;
    for (const auto& function : hir_module.functions) {
        if (function.abi_contract != hir::AbiContract::Dynamic ||
            function.ownership != hir::BodyOwnership::ManagedMir ||
            manual_plans.find(function.id)) {
            continue;
        }
        const auto* body = managed.find(function.id);
        if (!body) continue;
        DynamicAbiPlan plan;
        if (build_plan(hir_module, *body, function, plan)) {
            if (!options.ipa_ra) {
                if (const auto* fallback =
                        model_registry().find_abi(function.abi)) {
                    plan.clobbers = fallback->call_clobbers;
                }
            }
            result.add(std::move(plan));
        }
    }

    // A private contract describes the complete effect visible to its direct
    // callers, including calls that survive inlining inside the callee. Build
    // the transitive clobber union to a fixed point so recursive components
    // agree. This is the interprocedural counterpart of caller-save splitting:
    // a private function may simply expose a broader effect instead of saving
    // a stable ABI's entire preserved set around an inner call.
    const auto append_unique = [](std::vector<std::string>& destination,
                                  const std::vector<std::string>& source) {
        bool changed = false;
        for (const auto& name : source) {
            if (std::find(destination.begin(), destination.end(), name) !=
                destination.end()) {
                continue;
            }
            destination.push_back(name);
            changed = true;
        }
        return changed;
    };
    for (auto& plan : result.mutable_entries()) {
        append_unique(plan.clobbers,
                      hir_module.function(plan.function).clobbers);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& plan : result.mutable_entries()) {
            const auto* body = managed.find(plan.function);
            if (!body) continue;
            for (const auto& value : body->values) {
                if (value.kind != mir::ValueKind::Call || !value.callee) {
                    continue;
                }
                const auto& callee = hir_module.function(*value.callee);
                if (const auto* private_callee = result.find(callee.id)) {
                    changed |= append_unique(
                        plan.clobbers, private_callee->clobbers);
                    for (const auto& argument :
                         private_callee->call.arguments) {
                        for (const auto& piece : argument.pieces) {
                            if (piece.location.kind ==
                                LocationKind::Register) {
                                changed |= append_unique(
                                    plan.clobbers,
                                    std::vector<std::string>{
                                        piece.location.reg});
                            }
                        }
                    }
                } else if (const auto* abi =
                               model_registry().find_abi(callee.abi)) {
                    changed |= append_unique(
                        plan.clobbers, abi->call_clobbers);
                }
                changed |= append_unique(plan.clobbers, callee.clobbers);
            }
        }
    }
    return result;
}

} // namespace cross::x86_64
