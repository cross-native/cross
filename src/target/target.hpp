// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/options.hpp"
#include "common/code_address.hpp"
#include "common/patch_address.hpp"

#include <optional>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cross {

class Subtarget;
struct SubtargetTable;

struct AbiId {
    static constexpr std::uint32_t invalid_value =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t value{invalid_value};

    [[nodiscard]] constexpr bool valid() const { return value != invalid_value; }
    [[nodiscard]] constexpr bool empty() const { return !valid(); }
    friend constexpr bool operator==(AbiId, AbiId) = default;
};

// Variadic state IDs are dense only within their owning ABI model.
struct AbiStateId {
    static constexpr std::uint16_t invalid_value =
        std::numeric_limits<std::uint16_t>::max();
    std::uint16_t value{invalid_value};

    [[nodiscard]] constexpr bool valid() const { return value != invalid_value; }
    [[nodiscard]] constexpr bool empty() const { return !valid(); }
    friend constexpr bool operator==(AbiStateId, AbiStateId) = default;
};

enum class AbiValueKind {
    Any,
    Integer,
    Floating,
    Pointer,
    Pair,
    Aggregate,
    Array,
    Vector,
    Zero,
};

enum class AbiRuleAction {
    Direct,
    Split,
    Flatten,
    Coerce,
    Indirect,
    Stack,
    Ignore,
};

// A register transport can define physical bits wider than its declared
// carrier. This is an ABI wire-format guarantee, not source signedness.
enum class AbiExtensionKind : std::uint8_t { None, Zero, Sign };

enum class AbiRegisterFailure {
    Stack,
    Partial,
    Error,
};

enum class AbiStackLayout {
    Packed,
    Slots,
};

enum class AbiStackRegion {
    Arguments,
    Results,
    ArgumentSpills,
};

// Variadic policy remains model data.  The common ABI interpreter and target
// emitters consume these actions without recognizing conventional ABI names.
enum class AbiVariadicStateKind {
    CursorOffset,
    StackAddress,
    CursorAddress,
    RegisterSaveAddress,
};

struct AbiVariadicState {
    AbiStateId id;
    std::string canonical_name;
    std::string type;
    AbiVariadicStateKind kind{AbiVariadicStateKind::CursorOffset};
    std::string cursor;
    unsigned base{};
    unsigned stride{};
    unsigned alignment{1};
    // Optional layout hook used only by the LLVM-text debug serializer.  The
    // production compiler derives the same state directly from the ABI plan.
    std::optional<unsigned> llvm_va_list_offset;
};

struct AbiVariadicShadow {
    std::string canonical_name;
    std::string source_bank;
    std::string target_bank;
    bool fixed_arguments{true};
    bool unnamed_arguments{true};
};

struct AbiRegisterBank {
    std::string canonical_name;
    std::string register_class;
    std::string cursor;
    std::vector<std::string> arguments;
    std::vector<std::string> results;
    unsigned register_bits{};
};

struct AbiRule {
    std::string canonical_name;
    std::vector<AbiValueKind> matches;
    AbiRuleAction action{AbiRuleAction::Direct};
    std::string bank;
    unsigned min_bits{};
    unsigned max_bits{};
    unsigned unit_bits{};
    unsigned carrier_bits{};
    AbiExtensionKind extension{AbiExtensionKind::None};
    unsigned max_elements{~0U};
    // Register cursors are counted in model-defined slots rather than bytes.
    // Some ABIs align a value to an even slot and/or consume more slots than
    // the number of physical endpoints used by the value (for example a
    // double in the MIPS o32 paired-FPR convention).
    unsigned cursor_alignment{1};
    // Align instead to the placed value's alignment, at most the entry's
    // stack alignment, counted in `stack_slot_bytes` positions.
    bool cursor_alignment_value{};
    unsigned cursor_advance{};
    // Zero means unlimited.  This makes leading-argument conventions model
    // data without giving the common interpreter an architecture name.
    unsigned argument_limit{};
    // A rule can remain eligible only until another bank has classified a
    // value.  This expresses stateful conventions such as "leading floating
    // arguments use FPRs until a GPR argument is encountered".
    std::vector<std::string> requires_unused_banks;
    unsigned stack_alignment{};
    unsigned stack_size{};
    // A flatten rule may merge recursively classified fields into fixed-size
    // chunks. Earlier banks dominate later banks when unlike field classes
    // overlap one chunk. This expresses policies such as the SysV INTEGER
    // over SSE eightbyte merge without teaching the interpreter an ABI name.
    std::vector<std::string> merge_banks;
    bool require_natural_alignment{};
    std::vector<std::string> required_features;
    std::vector<std::string> forbidden_features;
    bool arguments{true};
    bool fixed_arguments{true};
    bool variadic_arguments{true};
    bool results{true};
    bool failure_override{};
    AbiRegisterFailure failure{AbiRegisterFailure::Stack};
};

struct AbiEntry {
    AbiId id;
    std::string canonical_name;
    std::string source;  // "file:line" of the model declaration
    std::vector<std::string> aliases;
    std::string architecture;
    unsigned address_bits{};
    std::string llvm_calling_convention;
    std::string gcc_calling_attribute;
    bool compilation_selectable{true};
    bool function_selectable{true};
    // One of the target's elf_abi_tags; empty keeps the object writer's
    // default.
    std::string elf_abi_tag;
    // Nonzero marks a private-call convention candidate for subtargets whose
    // native integer registers have this width.
    unsigned private_carrier_bits{};
    std::vector<AbiRegisterBank> banks;
    std::vector<AbiRule> rules;
    std::vector<std::string> call_clobbers;
    AbiRegisterFailure argument_register_failure{AbiRegisterFailure::Stack};
    AbiRegisterFailure result_register_failure{AbiRegisterFailure::Error};
    AbiStackLayout stack_layout{AbiStackLayout::Packed};
    unsigned argument_stack_base{};
    unsigned stack_alignment{};
    unsigned stack_slot_bytes{};
    unsigned return_address_bytes{};
    std::vector<AbiStackRegion> stack_order{AbiStackRegion::Arguments};

    bool variadic_supported{};
    std::string variadic_count_cursor;
    std::string variadic_count_register;
    unsigned variadic_count_bits{};
    std::vector<AbiVariadicShadow> variadic_shadows;
    std::vector<AbiVariadicState> variadic_states;
    std::vector<std::string> variadic_save_banks;
    unsigned variadic_save_alignment{1};
    std::string variadic_home_bank;
    unsigned variadic_home_base{};
    unsigned variadic_home_stride{};
    unsigned variadic_va_list_bytes{};
    unsigned variadic_va_list_alignment{1};
};

struct RegisterEntry {
    std::string_view name;
    std::string_view storage;
    unsigned bits;
    std::string_view register_class;
    std::string_view feature;
    bool address_capable{};
    // Empty means that the view is an ABI/instruction endpoint only (for
    // example an ordered register-stack position).  Otherwise these modes
    // describe the scalar values that a managed hard-bound local may carry.
    // This keeps target register legality out of the middle end.
    struct ScalarMode {
        unsigned bits{};
        bool floating{};
    };
    std::vector<ScalarMode> hard_scalar_modes;
    // Stack/frame pointers and analogous architectural state can be exposed
    // to ABI and instruction registries while remaining unavailable to the
    // managed allocator and user hard bindings.
    bool compiler_owned{};
    // Instruction source constraints differ from managed hard-binding policy
    // (for example an ordered floating register can be instruction-only).
    std::vector<ScalarMode> instruction_scalar_modes{};
    bool instruction_vector_values{};
    bool instruction_writable{true};
};

enum class InstructionOperandRole { Input, Output, InOut };
enum class InstructionLabelScope { AnyVisible, SameFunction };

struct InstructionOperandEntry {
    InstructionOperandRole role{InstructionOperandRole::Input};
    bool allow_register{};
    bool allow_immediate{};
    unsigned register_bits{};
    unsigned immediate_bits{};
    bool immediate_signed{};
    bool allow_label{};
    bool patchable{};
    // Empty accepts any target register class with the requested width.
    // Target instruction forms should name a class whenever two classes can
    // expose equal-width source registers (for example x86-64 GPR and opmask
    // storage).
    std::string_view register_class{};
    // Request the target assembler's indirect-register decoration. On x86
    // GNU syntax this renders `*%reg` for a computed control transfer.
    bool assembly_indirect{};
    // A typed lvalue may occupy this operand. A nonzero `memory_bits` is the
    // fixed accessed object width. Zero accepts any complete pointee and is
    // reserved for an address-only or hardware-sized region (for example a
    // cache line or an XSAVE area). Ordinary forms reject atomic-qualified
    // lvalues unless the target explicitly marks the form atomic.
    bool allow_memory{};
    unsigned memory_bits{};
    bool allow_atomic_memory{};
    // Optional exact physical storage constraint. Register class and width
    // alone cannot describe accumulator or ordered-register-stack forms.
    std::string_view register_storage{};
    // Exact scalar source types accepted by this patch field. This is distinct
    // from the range accepted for an ordinary (non-patch) immediate operand.
    std::vector<std::string_view> patch_types{};
    PatchAddressRepresentation patch_address{PatchAddressRepresentation::Unavailable};
    bool patch_supports_symbol_relocation{};
    // A direct label field can impose an owner constraint independently of
    // its spelling or the source function's ABI.
    InstructionLabelScope label_scope{InstructionLabelScope::AnyVisible};
    // Nonzero: a register operand carries a value of exactly this width.
    // Forms of one mnemonic can differ only by value width, such as single
    // and double precision in the same floating registers.
    unsigned value_bits{};
    // The immediate names an architectural register by number and prints in
    // register syntax, such as a MIPS coprocessor register `$12`.
    bool assembly_register_number{};
};

// A pipeline resource the hardware does not interlock. A writer makes it
// available at `stage`; a user reads it at `stage`. A user that follows a
// writer of the same resource needs writer.stage - (user.stage + 1)
// instructions between them. With `operand`, the resource is qualified by
// that immediate operand's value (resource "cp0" and operand value 12 name
// "cp0.12").
struct HazardFact {
    std::string_view resource;
    unsigned stage{};
    std::optional<unsigned> operand{};
    // When set, the fact applies only while immediate operand
    // `when_operand` has one of `when_values`.
    std::optional<unsigned> when_operand{};
    std::vector<std::uint64_t> when_values{};
};

// Classes of instructions that implicitly use or write hazard resources.
enum class HazardEvent : std::uint8_t {
    Instruction,  // every instruction
    Load,
    Store,
    Cache,        // a cache-maintenance instruction
    Coprocessor,  // a coprocessor instruction
};

struct HazardEventEntry {
    HazardEvent event{HazardEvent::Instruction};
    HazardFact fact;
    bool writes{};
    // Instructions of these classes do not separate this write from a user.
    std::vector<HazardEvent> unseparated_by{};
};

enum class InstructionControlEffect {
    None,
    UnconditionalBranch,
    ConditionalBranch,
    RawReturn,
    // The instruction has no normal successor (for example x86 UD2).  Unlike
    // RawReturn it need not restore the entry stack depth.
    Trap,
};

struct InstructionEntry {
    std::string_view name;
    // `feature` is the primary feature retained for concise diagnostics and
    // registry output. Some instruction forms additionally require every
    // entry in `required_features` (for example AVX-512DQ + AVX-512VL).
    std::string_view feature;
    std::string_view assembly_mnemonic;
    std::vector<InstructionOperandEntry> operands;
    std::vector<std::string_view> implicit_reads;
    std::vector<std::string_view> implicit_writes;
    int stack_delta{};
    InstructionControlEffect control{InstructionControlEffect::None};
    std::vector<std::string_view> required_features{};
    // Ordered floating stacks are independent of the ordinary call stack.
    // Positive values push entries and negative values pop them.
    int ordered_stack_delta{};
    bool ordered_stack_reset{};
    // Optional assembler decorations used by targets whose encoded operands
    // are not printed as ordinary comma-separated operands. The designated
    // mask operand remains typed and feature-checked but is rendered as a
    // destination decorator. A broadcast decorates its memory operand, and
    // operand_prefix precedes the ordinary list (for example x86 EVEX
    // embedded rounding).
    std::optional<unsigned> assembly_mask_operand;
    bool assembly_zeroing{};
    std::optional<unsigned> assembly_broadcast_operand;
    unsigned assembly_broadcast_count{};
    std::string_view assembly_operand_prefix{};
    // The form is unavailable while any of these features is enabled.
    std::vector<std::string_view> forbidden_features{};
    // The instruction executes only in a privileged mode.
    bool privileged{};
    // Every execution is observable: the instruction reads state that can
    // change independently of the program (a timer, for example) or has an
    // effect beyond its operands. It is never merged, removed, or reordered
    // across another effect.
    bool volatile_effect{};
    // Scheduling facts for resources the hardware does not interlock.
    std::vector<HazardFact> hazard_writes{};
    std::vector<HazardFact> hazard_uses{};

    InstructionEntry() = default;

    InstructionEntry(
        std::string_view entry_name, std::string_view entry_feature,
        std::string_view mnemonic,
        std::vector<InstructionOperandEntry> explicit_operands,
        std::vector<std::string_view> reads,
        std::vector<std::string_view> writes, int raw_stack_delta,
        InstructionControlEffect control_effect,
        std::vector<std::string_view> extra_features = {},
        int floating_stack_delta = 0, bool resets_floating_stack = false)
        : name(entry_name), feature(entry_feature),
          assembly_mnemonic(mnemonic),
          operands(std::move(explicit_operands)),
          implicit_reads(std::move(reads)),
          implicit_writes(std::move(writes)), stack_delta(raw_stack_delta),
          control(control_effect),
          required_features(std::move(extra_features)),
          ordered_stack_delta(floating_stack_delta),
          ordered_stack_reset(resets_floating_stack) {}
};

// Dense identity of one instruction form: its index in
// `TargetInfo::instructions`.
struct InstructionFormId {
    std::uint32_t value{};
    friend bool operator==(InstructionFormId, InstructionFormId) = default;
};

struct InstructionFeatureConflict {
    std::string_view feature;
    // The feature is enabled but forbidden, rather than required but absent.
    bool forbidden{};
};

// The first feature that makes `form` unavailable under `enabled`, if any.
template <typename Enabled>
std::optional<InstructionFeatureConflict> instruction_feature_conflict(
    const InstructionEntry& form, const Enabled& enabled) {
    if (!enabled(form.feature)) return InstructionFeatureConflict{form.feature};
    for (const auto feature : form.required_features) {
        if (!enabled(feature)) return InstructionFeatureConflict{feature};
    }
    for (const auto feature : form.forbidden_features) {
        if (enabled(feature)) return InstructionFeatureConflict{feature, true};
    }
    return std::nullopt;
}

struct PatchValueMaterializerEntry {
    std::string_view type_name;
    unsigned bits{};
    std::string_view feature;
    PatchAddressRepresentation patch_address{PatchAddressRepresentation::Unavailable};
    // The contiguous encoded field accepts a link-time symbol plus addend.
    bool supports_symbol_relocation{};
    // Repeated lexical uses can read one immutable cell without duplicating
    // its physical field. The target owns the read/encoding sequence.
    bool supports_shared_cell{};
};

struct AtomicWidthEntry {
    unsigned bits{};
    std::string_view feature;
};

// Native fixed-vector widths exposed to target-independent vectorization.
// Empty feature names mean that the width is part of the architecture
// baseline. Integer and floating operations may become legal at different
// feature levels (for example AVX2 versus AVX on x86-64).
struct VectorWidthEntry {
    unsigned bits{};
    std::string_view integer_feature;
    std::string_view floating_feature;
};

// Source-level pointer spaces are distinct from Machine IR frame/outgoing
// memory regions. A zero width uses the selected ABI's address width. A
// target must opt into native lowering before a numbered space is accepted.
struct AddressSpaceEntry {
    std::uint32_t number{};
    unsigned pointer_bits{};
    std::uint64_t null_low{};
    std::uint64_t null_high{};
    bool generic{};
    bool readable{};
    bool writable{};
    bool executable{};
    bool volatile_by_default{};
    bool abi_transport{};
    bool native_lowering{};
};

enum class ByteOrder { Little, Big };
enum class BitFieldOrder { LeastSignificantFirst, MostSignificantFirst };
enum class BitFieldUnitSharing { SameUnqualifiedBase, SameStorageSize };
enum class BitFieldPlacement { AlignedUnits, NextAvailableBit };

// Target storage facts used before instruction selection. Pointer width comes
// from the selected ABI because architectures such as MIPS may expose more
// than one address model through one target module.
struct TargetDataLayout {
    ByteOrder byte_order{ByteOrder::Little};
    unsigned natural_alignment_limit{1};
    unsigned f80_storage_bytes{10};
    unsigned f80_alignment{1};
    BitFieldOrder bit_field_order{BitFieldOrder::LeastSignificantFirst};
    BitFieldUnitSharing bit_field_unit_sharing{
        BitFieldUnitSharing::SameUnqualifiedBase};
    BitFieldPlacement bit_field_placement{BitFieldPlacement::AlignedUnits};
    CodeAddressRepresentation code_addresses{CodeAddressRepresentation::Opaque};
};

// Target-independent optimizations describe the value they need priced;
// target modules decide how expensive that value is for the resolved ISA and
// tuning CPU.  Costs are relative rematerialization units rather than literal
// instruction counts: one means cheap enough to recreate at a use, while a
// larger value can justify extending a live range.
struct IntegerConstantCostQuery {
    unsigned bits{};
    std::uint64_t low{};
    std::uint64_t high{};
    bool is_signed{};
};

struct IntegerOperationCostQuery {
    unsigned bits{};
    bool is_signed{};
};

struct TargetCostModel {
    using IntegerConstantMaterializationCost =
        unsigned (*)(const Subtarget&, const IntegerConstantCostQuery&);
    // Latency in units of a simple integer ALU operation, or no value when
    // the subtarget has no single instruction for the operation at that
    // width.
    using IntegerOperationCost = std::optional<unsigned> (*)(
        const Subtarget&, const IntegerOperationCostQuery&);

    IntegerConstantMaterializationCost integer_constant_materialization{};
    // A quotient or remainder.
    IntegerOperationCost integer_division{};
    // The high half of the double-width product. Its presence also makes
    // the MIR multiply-high operations legal at that width.
    IntegerOperationCost integer_multiply_high{};
};

// Direct typed instruction-memory addressing. These are encoding constraints,
// independent of ABI result/argument transport and managed address legalization.
struct InstructionAddressMode {
    unsigned pointer_bits{};
    unsigned register_bits{};
    std::string_view register_class;
    std::vector<std::uint32_t> address_spaces;
    std::vector<unsigned> index_scales;
    std::vector<std::string_view> forbidden_index_storage;
    unsigned displacement_bits{};
    std::string_view feature;
};

// An optional `$::feature::NAME` the target implements. A nonempty `option`
// gates it on that resolved boolean target option.
struct LanguageFeatureEntry {
    std::string_view name;
    std::string_view option;
};

// An ELF ABI tag that model ABI entries of the architecture may request
// through `elf_abi_tag`; a nonzero `address_bits` restricts it to entries of
// that address width.
struct ElfAbiTagEntry {
    std::string_view name;
    unsigned address_bits{};
};

enum class NakedLowering { Raw, Constrained };

// A compiler intrinsic that the target lowers to a registry instruction is
// usable only where a form of that instruction is.
struct IntrinsicFormEntry {
    std::string_view intrinsic;
    std::string_view instruction;
};

struct TargetInfo {
    std::string_view architecture;
    std::vector<std::string_view> triple_prefixes;
    TargetDataLayout data_layout;
    std::vector<RegisterEntry> registers;
    std::vector<PatchValueMaterializerEntry> patch_value_materializers;
    std::vector<InstructionEntry> instructions;
    // Widths that this architecture can implement without a runtime helper.
    // Individual backends still choose the concrete LL/SC, CAS, AMO, or CISC
    // sequence and may gate it on resolved subtarget features.
    std::vector<AtomicWidthEntry> lock_free_atomic_widths;
    std::vector<VectorWidthEntry> native_vector_widths;
    std::vector<OptionDefinition> options;
    const SubtargetTable* subtargets{};
    TargetCostModel cost_model;
    std::vector<AddressSpaceEntry> address_spaces;
    std::vector<InstructionAddressMode> instruction_address_modes{};
    std::vector<LanguageFeatureEntry> language_features{};
    std::vector<ElfAbiTagEntry> elf_abi_tags{};
    // Hazard resources that classes of instructions use or write implicitly.
    std::vector<HazardEventEntry> hazard_events{};
    // How `[[naked]]` bodies lower: through the raw instruction lowerer, or
    // through managed MIR with frameless, spill-free allocation restricted
    // to the function's declared registers.
    NakedLowering naked_lowering{NakedLowering::Raw};
    std::vector<IntrinsicFormEntry> intrinsic_forms{};

    [[nodiscard]] bool matches(std::string_view triple) const;
    [[nodiscard]] std::string_view default_abi(std::string_view triple) const;
};

const std::vector<const TargetInfo*>& all_targets();
const TargetInfo* target_for_triple(std::string_view triple);
// Language feature names (without `$::feature::`) available for the resolved
// target and options; target instruction-set extensions are listed separately.
std::vector<std::string_view> language_features(const CompilerOptions& options);
const AbiEntry* find_abi(const TargetInfo& target, std::string_view name,
                         std::string_view triple);
const AbiEntry* find_abi(const TargetInfo& target, AbiId id);
const RegisterEntry* find_register(const TargetInfo& target, std::string_view name);
const PatchValueMaterializerEntry* find_patch_value_materializer(
    const TargetInfo& target, std::string_view type_name);
bool patch_operand_accepts_type(const InstructionOperandEntry& operand, std::string_view type_name,
                               unsigned source_bits);
const InstructionEntry* find_instruction(const TargetInfo& target, std::string_view name);
// A source mnemonic can expose several typed forms.  `find_instruction` is
// retained for callers that need only an existence check or a canonical form;
// lowering should inspect every entry returned here.
std::vector<const InstructionEntry*> find_instruction_forms(
    const TargetInfo& target, std::string_view name);
bool target_has_instruction(const TargetInfo& target, std::string_view name);
[[nodiscard]] inline InstructionFormId instruction_form_id(
    const TargetInfo& target, const InstructionEntry& form) {
    return {static_cast<std::uint32_t>(&form - target.instructions.data())};
}
[[nodiscard]] inline const InstructionEntry& instruction_form(
    const TargetInfo& target, InstructionFormId id) {
    return target.instructions.at(id.value);
}
const AddressSpaceEntry* find_address_space(const TargetInfo& target,
                                            std::uint32_t number);
// Source pointer types require an explicitly registered native representation.
// Shared by ordinary HIR validation and pre-erasure expansion validation.
std::optional<std::string> address_space_type_error(const TargetInfo& target,
                                                   std::uint32_t number);

} // namespace cross
