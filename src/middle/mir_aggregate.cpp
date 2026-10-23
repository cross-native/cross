// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross::mir {

namespace {

// The alignment that `offset` bytes past an address aligned to `alignment`
// keep.
unsigned aligned_at(unsigned alignment, std::uint64_t offset) {
    while (alignment > 1 && offset % alignment != 0) alignment /= 2;
    return std::max(alignment, 1U);
}

} // namespace

std::vector<MemoryUnit> plan_memory_units(std::uint64_t size,
                                          std::optional<unsigned> load_alignment,
                                          unsigned store_alignment,
                                          const Subtarget& subtarget,
                                          const CompilerOptions& options) {
    struct Width {
        unsigned bytes;
        bool vector;
    };
    std::vector<Width> widths;
    const auto register_bytes = std::max(1U, subtarget.integer_register_bits() / 8U);
    for (auto bytes = preferred_vector_bits(subtarget, options, false, 64) / 8U;
         bytes >= 16 && bytes > register_bytes; bytes /= 2)
        widths.push_back({bytes, true});
    for (auto bytes = register_bytes; bytes >= 1; bytes /= 2) widths.push_back({bytes, false});

    std::vector<MemoryUnit> units;
    for (std::uint64_t offset = 0; offset < size;) {
        const auto store = aligned_at(store_alignment, offset);
        const auto load = load_alignment ? aligned_at(*load_alignment, offset) : 0U;
        std::optional<Width> best;
        unsigned best_cost{};
        bool best_aligned{};
        for (const auto width : widths) {
            if (width.bytes > size - offset) continue;
            const auto stored = subtarget.memory_access_instructions(
                {width.bytes, store, true, width.vector});
            const auto loaded = load_alignment
                ? subtarget.memory_access_instructions({width.bytes, load, false, width.vector})
                : 0U;
            if (stored == 0 || (load_alignment && loaded == 0)) continue;
            const auto cost = stored + loaded;
            const bool natural = store >= width.bytes && (!load_alignment || load >= width.bytes);
            // Fewer instructions per byte first, then natural alignment,
            // then the wider unit (widths come widest first).
            if (best) {
                const auto mine = static_cast<std::uint64_t>(cost) * best->bytes;
                const auto theirs = static_cast<std::uint64_t>(best_cost) * width.bytes;
                if (mine > theirs || (mine == theirs && (best_aligned || !natural))) continue;
            }
            best = width;
            best_cost = cost;
            best_aligned = natural;
        }
        // One byte at a time always has a sequence.
        const auto chosen = best.value_or(Width{1, false});
        units.push_back({offset, chosen.bytes, chosen.vector});
        offset += chosen.bytes;
    }
    return units;
}

unsigned straight_unit_limit(const CompilerOptions& options) {
    // A loop costs about six instructions besides its accesses.
    return options.optimize_for == OptimizationGoal::Size ||
                   options.optimize_for == OptimizationGoal::MinimumSize
               ? 6U : 16U;
}

hir::TypeId memory_unit_type(hir::Module& module, const MemoryUnit& unit) {
    if (unit.vector) {
        return module.add_qualifiers(
            module.vector_of(*module.builtin(BuiltinType::U64), unit.bytes / 8U),
            false, false, true);
    }
    const auto builtin = unit.bytes == 1 ? BuiltinType::U8
        : unit.bytes == 2 ? BuiltinType::U16
        : unit.bytes == 4 ? BuiltinType::U32 : BuiltinType::U64;
    return module.add_qualifiers(*module.builtin(builtin), false, false, true);
}

namespace {

bool load_kind(ValueKind kind) {
    return kind == ValueKind::Load || kind == ValueKind::GlobalLoad ||
           kind == ValueKind::PointerLoad || kind == ValueKind::IndexedLoad;
}

bool store_kind(ValueKind kind) {
    return kind == ValueKind::Store || kind == ValueKind::GlobalStore ||
           kind == ValueKind::PointerStore;
}

// The operand that a store writes.
std::size_t stored_operand(const ManagedValue& store) {
    return store.kind == ValueKind::PointerStore ? 1U : 0U;
}

// Replaces a record load whose value only stores consume, through qualifier
// conversions, with loads and stores of register-wide units, so that no
// temporary holds the record. A copy whose one store immediately follows its
// load, from a source that overlaps the destination exactly or not at all,
// loads each unit just before its store, in address order. Another copy
// loads its units where the record was loaded, which keeps the source's
// value for every store, when the target's registers hold them, and
// otherwise keeps the record.
class AggregateCopyExpansion {
public:
    AggregateCopyExpansion(ManagedFunction& function, hir::Module& module,
                           const Subtarget& subtarget, const CompilerOptions& options)
        : function_(function), module_(module), subtarget_(subtarget),
          options_(options), target_(subtarget.target()),
          uptr_(*module.builtin(BuiltinType::Uptr)),
          byte_(module.add_qualifiers(*module.builtin(BuiltinType::U8), false, false, true)) {}

    bool run() {
        const bool dropped = drop_qualifier_conversions();
        // Units beat a record's home only where registers hold them.
        if (options_.register_allocation) plan();
        if (copies_.empty()) return dropped;
        rebuild();
        // The replaced operations, no longer in any block, and their effects.
        remove_effectful_values(function_, dropped_);
        return true;
    }

private:
    // Where a copy's units load: all where the record was loaded, or each
    // just before its store, in address order.
    enum class Strategy : std::uint8_t { Gather, Forward };

    struct Copy {
        ValueId load;
        std::vector<ValueId> stores;
        std::vector<MemoryUnit> units;
        Strategy strategy{Strategy::Gather};
        // More units than the registers hold: long runs of them loop.
        bool large{};
    };

    // A store takes a record of its pointee's type with other qualifiers,
    // so a qualifier conversion that only stores read, which a target would
    // copy to another home, goes.
    bool drop_qualifier_conversions() {
        const UseLists uses(function_);
        ValueReplacements replacements;
        std::unordered_set<std::uint32_t> dropped;
        for (const auto& block : function_.blocks) {
            for (const auto id : block.values) {
                const auto& value = function_.values[id.value];
                if (value.kind != ValueKind::Cast || value.cast != CastOperation::Reinterpret ||
                    value.operands.size() != 1 || !record(value.type) ||
                    module_.type(function_.values[value.operands[0].value].type).record !=
                        module_.type(value.type).record) continue;
                const auto& users = uses.uses(id);
                if (users.empty() ||
                    !std::all_of(users.begin(), users.end(), [&](const ValueUse& use) {
                        if (!use.user || use.kind != UseKind::Operand) return false;
                        const auto& user = function_.values[use.user->value];
                        return store_kind(user.kind) && use.index == stored_operand(user);
                    })) continue;
                replacements.add(id, value.operands[0]);
                dropped.insert(id.value);
            }
        }
        if (dropped.empty()) return false;
        replacements.apply(function_);
        for (auto& block : function_.blocks)
            std::erase_if(block.values, [&](ValueId id) { return dropped.contains(id.value); });
        compact_managed_values(function_);
        return true;
    }

    // ---- Planning ----

    bool record(hir::TypeId id) const {
        const auto& type = module_.type(id);
        return type.kind == hir::Type::Kind::Record && type.record &&
               !type.is_atomic && module_.record(*type.record).complete &&
               module_.record(*type.record).size != 0;
    }

    unsigned natural(hir::TypeId type) const {
        return static_cast<unsigned>(
            std::max<std::uint64_t>(1, hir::natural_alignment(module_, type, target_).value_or(1)));
    }

    // The alignment proven for the record a load or store accesses.
    unsigned alignment(const ManagedValue& access, hir::TypeId type) const {
        if (access.slot) {
            const auto& slot = function_.slots[access.slot->value];
            return std::max({natural(slot.type), slot.minimum_alignment,
                static_cast<unsigned>(hir::layout_alignment(module_, slot.type, target_).value_or(1))});
        }
        if (access.object) {
            const auto& object = module_.object(*access.object);
            return std::max(natural(object.type), object.minimum_alignment);
        }
        if (access.memory_alignment != 0) return access.memory_alignment;
        return natural(type);
    }

    // The register budget of a copy that loads all its units first: half
    // of each bank's allocatable registers.
    std::array<unsigned, 2> budget() const {
        const auto describe = target_.cost_model.register_pressure;
        if (!describe) return {8, 0};
        RegisterPressureQuery query;
        query.interfaces.push_back(
            find_abi(target_, module_.function(function_.source).abi));
        const auto model = describe(subtarget_, options_, query);
        const auto half = [](const RegisterPressureBank& bank) {
            const auto colors = bank.registers.size() > bank.scratch_registers
                ? static_cast<unsigned>(bank.registers.size()) - bank.scratch_registers : 0U;
            return std::max(2U, colors / 2U);
        };
        return {half(model.integer), model.floating.vector_bits != 0 ? half(model.floating) : 0U};
    }

    // The units of a copy from a slot that the stores before it in its block
    // wrote: each unit is what one store wrote, where the target can move
    // it, so that the loads of the units take the stored values. Elsewhere
    // the ordinary plan applies.
    std::vector<MemoryUnit> source_units(const ManagedBlock& block, std::size_t position,
                                         const MemoryAccess& source, std::uint64_t size,
                                         unsigned load_alignment, unsigned store_alignment,
                                         const AccessAnalysis& analysis) const {
        std::vector<std::optional<std::size_t>> writer(size);
        std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;
        for (auto index = position; index-- > 0;) {
            const auto& value = function_.values[block.values[index].value];
            if ((value.kind == ValueKind::LifetimeStart || value.kind == ValueKind::LifetimeEnd) &&
                value.slot && source.address.base == MemoryAddress::Base::Slot &&
                value.slot->value == source.address.id) break;
            if (!store_kind(value.kind)) continue;
            const auto access = analysis.access(value);
            if (!access || access->address.base != source.address.base ||
                access->address.id != source.address.id || access->address.index ||
                source.address.index) continue;
            const auto begin = access->address.offset - source.address.offset;
            const auto end = begin + static_cast<std::int64_t>(access->size);
            const auto limit = static_cast<std::int64_t>(size);
            if (end <= 0 || begin >= limit) continue;
            const auto first = static_cast<std::uint64_t>(std::max<std::int64_t>(begin, 0));
            const auto last = static_cast<std::uint64_t>(std::min(end, limit));
            // A store that spills past the copy writes bytes of no unit.
            extents.push_back({begin >= 0 && end <= limit ? first : size, last});
            for (auto at = first; at < last; ++at)
                if (!writer[at]) writer[at] = extents.size() - 1;
        }
        if (extents.empty())
            return plan_memory_units(size, load_alignment, store_alignment, subtarget_, options_);
        const auto register_bytes = std::max(1U, subtarget_.integer_register_bits() / 8U);
        const auto vector_bytes = preferred_vector_bits(subtarget_, options_, false, 64) / 8U;
        std::vector<MemoryUnit> units;
        for (std::uint64_t offset = 0; offset < size;) {
            if (const auto store = writer[offset]; store && extents[*store].first == offset) {
                const auto bytes = extents[*store].second - offset;
                bool whole = true;
                for (auto at = offset; at < offset + bytes; ++at) whole = whole && writer[at] == store;
                const bool vector = bytes > register_bytes;
                if (whole && std::has_single_bit(bytes) &&
                    (!vector || (bytes >= 16 && bytes <= vector_bytes)) &&
                    subtarget_.memory_access_instructions(
                        {static_cast<unsigned>(bytes), aligned_at(load_alignment, offset), false, vector}) &&
                    subtarget_.memory_access_instructions(
                        {static_cast<unsigned>(bytes), aligned_at(store_alignment, offset), true, vector})) {
                    units.push_back({offset, static_cast<unsigned>(bytes), vector});
                    offset += bytes;
                    continue;
                }
            }
            // Up to where the next store's bytes begin.
            auto next = offset + 1;
            while (next < size && !(writer[next] && extents[*writer[next]].first == next)) ++next;
            for (auto unit : plan_memory_units(next - offset, aligned_at(load_alignment, offset),
                                               aligned_at(store_alignment, offset), subtarget_, options_)) {
                unit.offset += offset;
                units.push_back(unit);
            }
            offset = next;
        }
        return units;
    }

    // Whether each unit of a copy can load just before its store: nothing
    // between the load and its one store writes the source, and the two
    // overlap exactly or not at all. The language promises the latter for an
    // assignment or initialization that copies an object, and no pass before
    // this one forwards a record, so such a copy is a load and a store of the
    // same inlined body. A record that an inlined call returns and its caller
    // stores is not; the analysis must show the two apart or identical.
    bool forward(const Copy& copy, const ManagedValue& load,
                 const std::optional<MemoryAccess>& source, const AccessAnalysis& analysis) const {
        if (copy.stores.size() != 1) return false;
        const auto& store = function_.values[copy.stores.front().value];
        if (store.effect_input != load.effect_output) return false;
        if (store.inline_site == load.inline_site) return true;
        const auto to = analysis.access(store);
        return source && to && (!analysis.may_alias(*source, *to) || source->address == to->address);
    }

    void plan() {
        const auto& entity = module_.function(function_.source);
        if (entity.naked) return;
        const UseLists uses(function_);
        const AccessAnalysis analysis(function_, module_, target_);
        const auto limits = budget();
        std::vector<bool> placed(function_.values.size());
        for (const auto& block : function_.blocks)
            for (const auto id : block.values) placed[id.value] = true;
        for (const auto& block : function_.blocks) {
            for (std::size_t position = 0; position < block.values.size(); ++position) {
                const auto id = block.values[position];
                const auto& value = function_.values[id.value];
                if (!load_kind(value.kind) || value.is_volatile_access ||
                    !value.effect_output || !record(value.type)) continue;
                Copy copy{id, {}, {}, Strategy::Gather};
                std::vector<ValueId> casts;
                std::vector<ValueId> pending{id};
                bool valid = true;
                while (valid && !pending.empty()) {
                    const auto current = pending.back();
                    pending.pop_back();
                    for (const auto& use : uses.uses(current)) {
                        const auto* user = use.user ? &function_.values[use.user->value] : nullptr;
                        if (!user || use.kind != UseKind::Operand || !placed[use.user->value]) {
                            valid = false;
                        } else if (user->kind == ValueKind::Cast &&
                                   user->cast == CastOperation::Reinterpret &&
                                   record(user->type) &&
                                   module_.type(user->type).record == module_.type(value.type).record) {
                            casts.push_back(*use.user);
                            pending.push_back(*use.user);
                        } else if (store_kind(user->kind) && !user->is_volatile_access &&
                                   use.index == stored_operand(*user) &&
                                   std::find(copy.stores.begin(), copy.stores.end(), *use.user) ==
                                       copy.stores.end()) {
                            copy.stores.push_back(*use.user);
                        } else {
                            valid = false;
                        }
                    }
                }
                if (!valid || copy.stores.empty()) continue;
                const auto size = module_.record(*module_.type(value.type).record).size;
                unsigned store_alignment = 0;
                for (const auto store : copy.stores) {
                    const auto& access = function_.values[store.value];
                    const auto at = alignment(access, value.type);
                    store_alignment = store_alignment == 0 ? at : std::min(store_alignment, at);
                }
                const auto source = analysis.access(value);
                copy.units = source && source->address.base == MemoryAddress::Base::Slot
                    ? source_units(block, position, *source, size, alignment(value, value.type),
                                   store_alignment, analysis)
                    : plan_memory_units(size, alignment(value, value.type), store_alignment,
                                        subtarget_, options_);
                std::array<unsigned, 2> counts{};
                for (const auto& unit : copy.units) ++counts[unit.vector ? 1U : 0U];
                copy.large = counts[0] > limits[0] || counts[1] > limits[1];
                if (forward(copy, value, source, analysis)) copy.strategy = Strategy::Forward;
                else if (copy.large) continue;
                for (const auto cast : casts) casts_.insert(cast.value);
                for (const auto store : copy.stores) stores_[store.value] = copies_.size();
                loads_[id.value] = copies_.size();
                copies_.push_back(std::move(copy));
            }
        }
    }

    // ---- Rewriting ----

    // Blocks are rebuilt in order. `add` appends to the block being rebuilt,
    // `chained` follows the current effect, and a loop splits the block,
    // whose remaining values then go to its continuation.

    ValueId add(ManagedValue value) {
        value.id = ValueId{static_cast<std::uint32_t>(function_.values.size())};
        value.location = location_;
        value.inline_site = inline_site_;
        function_.values.push_back(std::move(value));
        current_.push_back(function_.values.back().id);
        return function_.values.back().id;
    }

    EffectId new_effect(EffectKind kind) {
        ManagedEffect effect;
        effect.id = EffectId{static_cast<std::uint32_t>(function_.effects.size())};
        effect.location = location_;
        effect.kind = kind;
        function_.effects.push_back(effect);
        return effect.id;
    }

    ValueId chained(ManagedValue value) {
        const auto effect = new_effect(EffectKind::Operation);
        value.effect_input = effect_;
        value.effect_output = effect;
        const auto id = add(std::move(value));
        function_.effects[effect.value].input = effect_;
        function_.effects[effect.value].operation = id;
        effect_ = effect;
        return id;
    }

    void redirect(EffectId from, EffectId to) {
        for (auto& value : function_.values)
            if (value.effect_input == from) value.effect_input = to;
        for (auto& effect : function_.effects) {
            if (effect.input == from) effect.input = to;
            for (auto& incoming : effect.incoming)
                if (incoming.effect == from) incoming.effect = to;
        }
        for (auto& block : function_.blocks)
            if (block.terminator.effect == from) block.terminator.effect = to;
    }

    // Ends the expansion of an operation that goes: what read its effect
    // follows what replaced it.
    void finish(const ManagedValue& original) {
        if (original.effect_output && *original.effect_output != effect_)
            redirect(*original.effect_output, effect_);
        dropped_.insert(original.id.value);
    }

    // A block, empty, whose entry effect joins what its predecessors leave.
    BlockId new_block() {
        ManagedBlock block;
        block.id = BlockId{static_cast<std::uint32_t>(function_.blocks.size())};
        block.location = location_;
        block.effect = new_effect(EffectKind::Phi);
        function_.blocks.push_back(std::move(block));
        return function_.blocks.back().id;
    }

    // Ends the block being rebuilt here: what follows, with its terminator,
    // moves to a new block, which is returned.
    BlockId split() {
        const auto head = block_;
        function_.blocks[head.value].values = std::move(current_);
        current_.clear();
        const auto after = new_block();
        auto& rest = function_.blocks[after.value];
        auto& from = function_.blocks[head.value];
        rest.terminator = std::exchange(from.terminator, ManagedTerminator{});
        for (const auto successor : rest.terminator.successors) {
            auto& target = function_.blocks[successor.value];
            std::replace(target.predecessors.begin(), target.predecessors.end(), head, after);
            for (auto& incoming : function_.effects[target.effect.value].incoming)
                if (incoming.predecessor == head) incoming.predecessor = after;
            for (const auto id : target.values)
                for (auto& incoming : function_.values[id.value].incoming)
                    if (incoming.predecessor == head) incoming.predecessor = after;
        }
        const auto entry = function_.blocks[after.value].effect;
        if (rest.terminator.effect == effect_) rest.terminator.effect = entry;
        redirect(effect_, entry);
        return after;
    }

    void terminate(TerminatorKind kind, std::optional<ValueId> condition,
                   std::vector<BlockId> successors) {
        auto& block = function_.blocks[block_.value];
        block.values = std::move(current_);
        current_.clear();
        for (const auto successor : successors) {
            auto& target = function_.blocks[successor.value];
            target.predecessors.push_back(block_);
            function_.effects[target.effect.value].incoming.push_back({block_, effect_});
        }
        block.terminator = {kind, location_, condition, std::move(successors), effect_, inline_site_};
    }

    void open(BlockId block) {
        block_ = block;
        effect_ = function_.blocks[block.value].effect;
        current_.clear();
    }

    ValueId constant(hir::TypeId type, std::uint64_t integer) {
        ManagedValue value;
        value.kind = ValueKind::ConstantInteger;
        value.type = type;
        value.integer = integer;
        return add(std::move(value));
    }

    ValueId constant(std::uint64_t integer) { return constant(uptr_, integer); }

    ValueId binary(BinaryOperation operation, hir::TypeId type, ValueId left, ValueId right) {
        ManagedValue value;
        value.kind = ValueKind::Binary;
        value.binary = operation;
        value.type = type;
        value.operands = {left, right};
        return add(std::move(value));
    }

    // A pointer to `pointee` in the address space of `like`.
    hir::TypeId pointer(hir::TypeId pointee, hir::TypeId like) {
        return module_.pointer_to(pointee, module_.type(like).address_space);
    }

    ValueId reinterpret(ValueId source, hir::TypeId type) {
        if (function_.values[source.value].type == type) return source;
        ManagedValue value;
        value.kind = ValueKind::Cast;
        value.cast = CastOperation::Reinterpret;
        value.type = type;
        value.operands = {source};
        return add(std::move(value));
    }

    ValueId indexed(ValueId base, ValueId index) {
        ManagedValue address;
        address.kind = ValueKind::IndexedAddress;
        address.type = function_.values[base.value].type;
        address.operands = {base, index};
        return add(std::move(address));
    }

    // The bytes of the record that a load or store accesses.
    ValueId record_bytes(const ManagedValue& access) {
        ValueId address;
        if (access.slot) {
            auto& slot = function_.slots[access.slot->value];
            slot.address_taken = true;
            ManagedValue value;
            value.kind = ValueKind::SlotAddress;
            value.type = module_.pointer_to(slot.type);
            value.slot = access.slot;
            address = add(std::move(value));
        } else if (access.object) {
            ManagedValue value;
            value.kind = ValueKind::GlobalAddress;
            value.type = module_.pointer_to(module_.object(*access.object).type);
            value.object = access.object;
            address = add(std::move(value));
        } else if (access.kind == ValueKind::IndexedLoad) {
            address = indexed(access.operands[0], access.operands[1]);
        } else {
            address = access.operands[0];
        }
        return reinterpret(address, pointer(byte_, function_.values[address.value].type));
    }

    // A pointer to the unit type at `offset` bytes into `bytes`.
    ValueId unit_pointer(ValueId bytes, std::uint64_t offset, hir::TypeId type) {
        const auto at = offset == 0 ? bytes : indexed(bytes, constant(offset));
        return reinterpret(at, pointer(type, function_.values[bytes.value].type));
    }

    unsigned unit_alignment(const ManagedValue& access, hir::TypeId record_type,
                            const MemoryUnit& unit) const {
        const auto at = aligned_at(alignment(access, record_type), unit.offset);
        return at >= unit.bytes ? 0U : at;
    }

    ValueId load_unit(ValueId address, hir::TypeId type, unsigned alignment) {
        ManagedValue load;
        load.kind = ValueKind::PointerLoad;
        load.type = type;
        load.operands = {address};
        load.memory_alignment = alignment;
        return chained(std::move(load));
    }

    void store_unit(ValueId address, ValueId source, unsigned alignment) {
        ManagedValue store;
        store.kind = ValueKind::PointerStore;
        store.type = *module_.builtin(BuiltinType::Void);
        store.operands = {address, source};
        store.memory_alignment = alignment;
        (void)chained(std::move(store));
    }

    // Runs `body(index)` for each index below `count`, in a loop of its own
    // block.
    template <typename Body>
    void loop(std::uint64_t count, const Body& body) {
        const auto head = block_;
        const auto start = constant(0);
        const auto after = split();
        const auto repeat = new_block();
        current_ = std::move(function_.blocks[head.value].values);
        terminate(TerminatorKind::Branch, std::nullopt, {repeat});
        open(repeat);
        ManagedValue phi;
        phi.kind = ValueKind::Phi;
        phi.type = uptr_;
        phi.incoming = {{head, start}};
        const auto index = add(std::move(phi));
        body(index);
        const auto next = binary(BinaryOperation::Add, uptr_, index, constant(1));
        const auto more = binary(BinaryOperation::NotEqual, *module_.builtin(BuiltinType::Bool),
                                 next, constant(count));
        function_.values[index.value].incoming.push_back({repeat, next});
        terminate(TerminatorKind::ConditionalBranch, more, {repeat, after});
        open(after);
    }

    // Copies the units of `copy` from `source` to `destination` in address
    // order, in groups of up to four whose loads precede their stores; in a
    // large copy, a run of equal units longer than the straight-line limit
    // becomes a loop that moves a group in each pass, or one unit for size.
    void move_units(const Copy& copy, const ManagedValue& load, ValueId source,
                    const ManagedValue& store, ValueId destination) {
        constexpr std::uint64_t group = 4;
        const std::uint64_t pass_units =
            options_.optimize_for == OptimizationGoal::Size ||
                    options_.optimize_for == OptimizationGoal::MinimumSize
                ? 1U : group;
        const auto record_type = load.type;
        const auto limit = straight_unit_limit(options_);
        struct Run {
            std::size_t first, last;
        };
        std::vector<Run> runs;
        for (std::size_t first = 0; first < copy.units.size();) {
            auto last = first;
            while (last + 1 < copy.units.size() &&
                   copy.units[last + 1].bytes == copy.units[first].bytes &&
                   copy.units[last + 1].vector == copy.units[first].vector) ++last;
            runs.push_back({first, last});
            first = last + 1;
        }
        for (const auto& run : runs) {
            const auto& unit = copy.units[run.first];
            const auto type = memory_unit_type(module_, unit);
            const auto from = unit_alignment(load, record_type, unit);
            const auto to = unit_alignment(store, record_type, unit);
            const auto count = run.last - run.first + 1;
            // The units of the run from `first`, `size` of them, in order.
            const auto straight = [&](std::uint64_t first, std::uint64_t size) {
                for (std::uint64_t start = 0; start < size; start += group) {
                    const auto members = std::min(group, size - start);
                    std::vector<std::pair<const MemoryUnit*, ValueId>> values;
                    for (std::uint64_t step = 0; step < members; ++step) {
                        const auto& at = copy.units[run.first + first + start + step];
                        values.push_back({&at, load_unit(unit_pointer(source, at.offset, type), type,
                                                         unit_alignment(load, record_type, at))});
                    }
                    for (const auto& [at, value] : values)
                        store_unit(unit_pointer(destination, at->offset, type), value,
                                   unit_alignment(store, record_type, *at));
                }
            };
            if (!copy.large || count <= limit) {
                straight(0, count);
                continue;
            }
            const auto passes = count / pass_units;
            const auto rest = count % pass_units;
            const auto source_run = unit_pointer(source, unit.offset, type);
            const auto destination_run = unit_pointer(destination, unit.offset, type);
            loop(passes, [&](ValueId pass) {
                const auto first = pass_units == 1 ? pass
                    : binary(BinaryOperation::ShiftLeft, uptr_, pass,
                             constant(static_cast<std::uint64_t>(std::countr_zero(pass_units))));
                std::vector<ValueId> values;
                for (std::uint64_t step = 0; step < pass_units; ++step) {
                    const auto index = step == 0
                        ? first : binary(BinaryOperation::Add, uptr_, first, constant(step));
                    values.push_back(load_unit(indexed(source_run, index), type, from));
                }
                for (std::uint64_t step = 0; step < pass_units; ++step) {
                    const auto index = step == 0
                        ? first : binary(BinaryOperation::Add, uptr_, first, constant(step));
                    store_unit(indexed(destination_run, index), values[step], to);
                }
            });
            // The units no whole pass takes are last in the run.
            straight(passes * pass_units, rest);
        }
    }

    void expand_load(const ManagedValue& value, const Copy& copy) {
        const auto bytes = record_bytes(value);
        auto& units = unit_values_[value.id.value];
        for (const auto& unit : copy.units) {
            const auto type = memory_unit_type(module_, unit);
            units.push_back(load_unit(unit_pointer(bytes, unit.offset, type), type,
                                      unit_alignment(value, value.type, unit)));
        }
    }

    void expand_store(const ManagedValue& value, const Copy& copy) {
        // A copy: adding values moves the function's values.
        const auto load = function_.values[copy.load.value];
        const auto destination = record_bytes(value);
        if (copy.strategy == Strategy::Gather) {
            const auto& units = unit_values_[copy.load.value];
            for (std::size_t index = 0; index < copy.units.size(); ++index) {
                const auto type = memory_unit_type(module_, copy.units[index]);
                store_unit(unit_pointer(destination, copy.units[index].offset, type), units[index],
                           unit_alignment(value, load.type, copy.units[index]));
            }
            return;
        }
        move_units(copy, load, record_bytes(load), value, destination);
    }

    void rebuild() {
        const auto count = static_cast<std::uint32_t>(function_.blocks.size());
        for (std::uint32_t index = 0; index < count; ++index) {
            open(BlockId{index});
            const auto original = function_.blocks[index].values;
            for (const auto id : original) {
                const auto value = function_.values[id.value];
                location_ = value.location;
                inline_site_ = value.inline_site;
                if (casts_.contains(id.value)) continue;
                if (const auto found = loads_.find(id.value); found != loads_.end()) {
                    if (copies_[found->second].strategy == Strategy::Gather)
                        expand_load(value, copies_[found->second]);
                    finish(value);
                    continue;
                }
                if (const auto found = stores_.find(id.value); found != stores_.end()) {
                    expand_store(value, copies_[found->second]);
                    finish(value);
                    continue;
                }
                current_.push_back(id);
                if (value.effect_output) effect_ = *value.effect_output;
            }
            function_.blocks[block_.value].values = std::move(current_);
            current_.clear();
        }
    }

    ManagedFunction& function_;
    hir::Module& module_;
    const Subtarget& subtarget_;
    const CompilerOptions& options_;
    const TargetInfo& target_;
    hir::TypeId uptr_;
    hir::TypeId byte_;
    std::vector<Copy> copies_;
    std::unordered_map<std::uint32_t, std::size_t> loads_;
    std::unordered_map<std::uint32_t, std::size_t> stores_;
    std::unordered_map<std::uint32_t, std::vector<ValueId>> unit_values_;
    std::unordered_set<std::uint32_t> casts_;
    std::unordered_set<std::uint32_t> dropped_;
    BlockId block_;
    EffectId effect_;
    std::vector<ValueId> current_;
    SourceLocation location_;
    std::optional<InlineSiteId> inline_site_;
};

} // namespace

bool copy_records_by_units(ManagedFunction& function, hir::Module& module,
                           const Subtarget& subtarget, const CompilerOptions& options) {
    return AggregateCopyExpansion(function, module, subtarget, options).run();
}

} // namespace cross::mir
