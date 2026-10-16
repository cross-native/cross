// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross::mir {
namespace {

bool pointer_type(const hir::Module& module, hir::TypeId id) {
    return module.type(id).kind == hir::Type::Kind::Pointer;
}

bool load_kind(ValueKind kind) {
    return kind == ValueKind::Load || kind == ValueKind::GlobalLoad ||
           kind == ValueKind::PointerLoad || kind == ValueKind::IndexedLoad;
}

bool store_kind(ValueKind kind) {
    return kind == ValueKind::Store || kind == ValueKind::GlobalStore ||
           kind == ValueKind::PointerStore;
}

// An address: a base, a constant byte displacement, and at most one scaled
// variable index. Slot and object bases are distinct storage; a value base
// is any other pointer.
struct Address {
    enum class Base : std::uint8_t { Slot, Object, Value };
    Base base{Base::Value};
    std::uint32_t id{};
    std::optional<ValueId> index;
    std::uint64_t scale{};
    std::int64_t offset{};
    friend bool operator==(const Address&, const Address&) = default;
};

// The bytes one load or store accesses.
struct Access {
    Address address;
    std::uint64_t size{};
    friend bool operator==(const Access&, const Access&) = default;
};

class AccessAnalysis {
public:
    AccessAnalysis(const ManagedFunction& function, const hir::Module& module,
                   const TargetInfo& target)
        : function_(function), module_(module), target_(target),
          addressed_(function.slots.size()) {
        for (const auto& slot : function.slots) {
            addressed_[slot.id.value] = slot.address_taken;
        }
        for (const auto& value : function.values) {
            if (value.kind == ValueKind::SlotAddress && value.slot &&
                value.slot->value < addressed_.size()) {
                addressed_[value.slot->value] = true;
            }
        }
    }

    // What an ordinary, non-volatile load or store accesses.
    [[nodiscard]] std::optional<Access> access(const ManagedValue& value) const {
        if (value.is_volatile_access) return std::nullopt;
        switch (value.kind) {
        case ValueKind::Load:
        case ValueKind::Store: {
            const auto& slot = function_.slots[value.slot->value];
            if (slot.is_volatile || slot.physical_location) return std::nullopt;
            return sized({Address::Base::Slot, slot.id.value, std::nullopt, 0, 0},
                         slot.type);
        }
        case ValueKind::GlobalLoad:
        case ValueKind::GlobalStore: {
            const auto& object = module_.object(*value.object);
            if (!distinct(object)) return std::nullopt;
            return sized({Address::Base::Object, object.id.value, std::nullopt, 0, 0},
                         object.type);
        }
        case ValueKind::PointerLoad:
            return sized(decompose(value.operands[0]), value.type);
        case ValueKind::PointerStore:
            return sized(decompose(value.operands[0]),
                         function_.values[value.operands[1].value].type);
        case ValueKind::IndexedLoad: {
            auto address = decompose(value.operands[0]);
            if (!displace(address, value.operands[1],
                          stride(function_.values[value.operands[0].value].type))) {
                address = {Address::Base::Value, value.operands[0].value,
                           value.operands[1],
                           stride(function_.values[value.operands[0].value].type)};
            }
            return sized(address, value.type);
        }
        default: return std::nullopt;
        }
    }

    // Whether two accesses can touch a common byte. Distinct slots and
    // objects never overlap, and no pointer reaches a slot whose address is
    // never taken. Equal bases with equal variable parts compare their
    // constant displacements.
    [[nodiscard]] bool may_alias(const Access& left, const Access& right) const {
        const auto& x = left.address;
        const auto& y = right.address;
        const bool x_identified = x.base != Address::Base::Value;
        const bool y_identified = y.base != Address::Base::Value;
        if (x_identified && y_identified) {
            if (x.base != y.base || x.id != y.id) return false;
        } else if (x_identified || y_identified) {
            const auto& identified = x_identified ? x : y;
            return identified.base != Address::Base::Slot ||
                   addressed_[identified.id];
        } else if (x.id != y.id) {
            return true;
        }
        if (x.index != y.index || x.scale != y.scale) return true;
        const auto x_begin = static_cast<__int128>(x.offset);
        const auto y_begin = static_cast<__int128>(y.offset);
        return x_begin < y_begin + static_cast<__int128>(right.size) &&
               y_begin < x_begin + static_cast<__int128>(left.size);
    }

private:
    // An object that no other declared object shares storage with.
    static bool distinct(const hir::Object& object) {
        return !object.alias_target && !object.weakref_target &&
               !object.fixed_address;
    }

    std::optional<Access> sized(Address address, hir::TypeId type) const {
        const auto size = hir::natural_size(module_, type, target_);
        if (!size || *size == 0) return std::nullopt;
        return Access{address, *size};
    }

    // The address stride of a pointer's pointee, as the targets scale an
    // indexed address.
    std::uint64_t stride(hir::TypeId pointer) const {
        const auto& type = module_.type(pointer);
        if (type.kind != hir::Type::Kind::Pointer || !type.pointee) return 1;
        return std::max<std::uint64_t>(
            1, hir::layout_size(module_, *type.pointee, target_).value_or(1));
    }

    // A constant address index, sign-extended from its width.
    std::optional<std::int64_t> constant_index(ValueId id) const {
        const auto& value = function_.values[id.value];
        if (value.kind != ValueKind::ConstantInteger) return std::nullopt;
        const auto size = hir::natural_size(module_, value.type, target_);
        if (!size || *size == 0 || *size > 8) return std::nullopt;
        const auto bits = static_cast<unsigned>(*size * 8);
        auto integer = value.integer;
        if (bits < 64) {
            integer &= (std::uint64_t{1} << bits) - 1U;
            if ((integer >> (bits - 1U)) & 1U) integer |= ~((std::uint64_t{1} << bits) - 1U);
        }
        return static_cast<std::int64_t>(integer);
    }

    // Adds `index` units of `scale` bytes to the address when the index is
    // constant or the address has no variable part yet.
    bool displace(Address& address, ValueId index, std::uint64_t scale) const {
        if (const auto constant = constant_index(index)) {
            const auto offset = static_cast<__int128>(address.offset) +
                                static_cast<__int128>(*constant) *
                                    static_cast<__int128>(scale);
            if (offset < std::numeric_limits<std::int64_t>::min() ||
                offset > std::numeric_limits<std::int64_t>::max()) {
                return false;
            }
            address.offset = static_cast<std::int64_t>(offset);
            return true;
        }
        if (address.index) return false;
        address.index = index;
        address.scale = scale;
        return true;
    }

    Address decompose(ValueId id) const {
        std::int64_t offset = 0;
        std::optional<ValueId> index;
        std::uint64_t scale = 0;
        for (unsigned depth = 0; depth < 64; ++depth) {
            const auto& value = function_.values[id.value];
            if (value.kind == ValueKind::Cast &&
                value.cast == CastOperation::Reinterpret &&
                value.operands.size() == 1 && pointer_type(module_, value.type) &&
                pointer_type(module_,
                             function_.values[value.operands[0].value].type)) {
                id = value.operands[0];
                continue;
            }
            if (value.kind == ValueKind::IndexedAddress &&
                value.operands.size() == 2) {
                Address partial{Address::Base::Value, 0, index, scale, offset};
                if (!displace(partial, value.operands[1], stride(value.type))) break;
                offset = partial.offset;
                index = partial.index;
                scale = partial.scale;
                id = value.operands[0];
                continue;
            }
            if (value.kind == ValueKind::GlobalAddress && value.object &&
                distinct(module_.object(*value.object))) {
                return {Address::Base::Object, value.object->value, index, scale,
                        offset};
            }
            if (value.kind == ValueKind::SlotAddress && value.slot) {
                return {Address::Base::Slot, value.slot->value, index, scale,
                        offset};
            }
            break;
        }
        return {Address::Base::Value, id.value, index, scale, offset};
    }

    const ManagedFunction& function_;
    const hir::Module& module_;
    const TargetInfo& target_;
    std::vector<bool> addressed_;
};

std::vector<BlockId> reverse_postorder(const ManagedFunction& function) {
    std::vector<BlockId> order;
    std::vector<bool> visited(function.blocks.size());
    std::vector<std::pair<BlockId, std::size_t>> stack{{function.entry, 0}};
    visited[function.entry.value] = true;
    while (!stack.empty()) {
        auto& [block, next] = stack.back();
        const auto& successors = function.blocks[block.value].terminator.successors;
        if (next < successors.size()) {
            const auto successor = successors[next++];
            if (!visited[successor.value]) {
                visited[successor.value] = true;
                stack.push_back({successor, 0});
            }
            continue;
        }
        order.push_back(block);
        stack.pop_back();
    }
    std::reverse(order.begin(), order.end());
    return order;
}

// A location's contents known equal to an SSA value.
struct Available {
    Access access;
    ValueId value;
    friend bool operator==(const Available&, const Available&) = default;
};
using MemoryState = std::vector<Available>;

constexpr std::size_t maximum_available = 64;

// The width of an integer or floating scalar whose bits a scalar of another
// such type may reinterpret.
std::optional<unsigned> reinterpretable_bits(const hir::Module& module,
                                             hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin) return std::nullopt;
    switch (type.builtin) {
    case BuiltinType::I8:
    case BuiltinType::U8: return 8U;
    case BuiltinType::I16:
    case BuiltinType::U16: return 16U;
    case BuiltinType::I32:
    case BuiltinType::U32:
    case BuiltinType::F32: return 32U;
    case BuiltinType::I64:
    case BuiltinType::U64:
    case BuiltinType::F64: return 64U;
    default: return std::nullopt;
    }
}

// A load whose location holds a value of another type of its width.
struct View {
    BlockId block;
    ValueId load;
    ValueId source;
    hir::TypeId type;
    SourceLocation location;
};

} // namespace

bool forward_memory_values(ManagedFunction& function,
                           const hir::Module& hir_module,
                           const TargetInfo& target) {
    const AccessAnalysis analysis(function, hir_module, target);
    std::vector<std::optional<Access>> accesses(function.values.size());
    for (const auto& value : function.values) {
        if (load_kind(value.kind) || store_kind(value.kind)) {
            accesses[value.id.value] = analysis.access(value);
        }
    }
    std::vector<bool> labeled(function.blocks.size());
    for (const auto& label : function.labels) labeled[label.block.value] = true;

    // Loads make their location available with their own result, stores
    // with their source, so the transfer is a monotone gen/kill function.
    // A rewrite then replaces a load whose location is already available.
    ValueReplacements replacements;
    std::unordered_set<std::uint32_t> removed;
    std::vector<View> views;
    const auto transfer = [&](const ManagedBlock& block, MemoryState& state,
                              bool rewrite) {
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            if (!value.effect_input && !value.effect_output) continue;
            const auto& access = accesses[id.value];
            if (load_kind(value.kind) && access) {
                const auto found = std::find_if(
                    state.begin(), state.end(), [&](const Available& entry) {
                        return entry.access == *access &&
                               function.values[entry.value.value].type == value.type;
                    });
                const auto bits = reinterpretable_bits(hir_module, value.type);
                const auto view = !bits ? state.end() : std::find_if(
                    state.begin(), state.end(), [&](const Available& entry) {
                        return entry.access == *access &&
                               reinterpretable_bits(
                                   hir_module,
                                   function.values[entry.value.value].type) == bits;
                    });
                if (rewrite && found != state.end()) {
                    replacements.add(id, replacements.resolve(found->value));
                    removed.insert(id.value);
                } else if (rewrite && view != state.end()) {
                    const ValueId cast{static_cast<std::uint32_t>(
                        function.values.size() + views.size())};
                    views.push_back({block.id, id,
                                     replacements.resolve(view->value),
                                     value.type, value.location});
                    replacements.add(id, cast);
                    removed.insert(id.value);
                }
                state.push_back({*access, id});
            } else if (store_kind(value.kind) && access) {
                std::erase_if(state, [&](const Available& entry) {
                    return analysis.may_alias(entry.access, *access);
                });
                state.push_back({*access, value.kind == ValueKind::PointerStore
                                              ? value.operands[1]
                                              : value.operands[0]});
            } else if ((value.kind == ValueKind::LifetimeStart ||
                        value.kind == ValueKind::LifetimeEnd) && value.slot) {
                std::erase_if(state, [&](const Available& entry) {
                    return entry.access.address.base == Address::Base::Slot &&
                           entry.access.address.id == value.slot->value;
                });
            } else {
                // Calls, atomics, volatile accesses, machine instructions,
                // and other effects may change any memory.
                state.clear();
            }
            if (state.size() > maximum_available) state.erase(state.begin());
        }
    };

    const auto order = reverse_postorder(function);
    std::vector<bool> reachable(function.blocks.size());
    for (const auto block : order) reachable[block.value] = true;
    std::vector<std::optional<MemoryState>> exits(function.blocks.size());
    // A location whose predecessors leave different known values in it holds
    // a phi of those values at a join.
    std::vector<MemoryState> joined(function.blocks.size());
    const auto entry_state = [&](BlockId id) {
        MemoryState state;
        const auto& block = function.blocks[id.value];
        if (id == function.entry || labeled[id.value]) return state;
        bool first = true;
        for (const auto predecessor : block.predecessors) {
            if (!reachable[predecessor.value] || !exits[predecessor.value]) {
                continue;
            }
            const auto& incoming = *exits[predecessor.value];
            if (first) {
                state = incoming;
                first = false;
            } else {
                std::erase_if(state, [&](const Available& entry) {
                    return std::find(incoming.begin(), incoming.end(), entry) ==
                           incoming.end();
                });
            }
        }
        for (const auto& phi : joined[id.value]) {
            std::erase_if(state, [&](const Available& entry) {
                return entry.access == phi.access;
            });
            state.push_back(phi);
        }
        return state;
    };
    const auto solve = [&] {
        for (unsigned sweep = 0; sweep < 16; ++sweep) {
            bool converged = true;
            for (const auto id : order) {
                auto state = entry_state(id);
                transfer(function.blocks[id.value], state, false);
                if (exits[id.value] != state) {
                    exits[id.value] = std::move(state);
                    converged = false;
                }
            }
            if (converged) return true;
        }
        return false;
    };
    if (!solve()) return false;
    // Only locations that some load reads are worth a phi.
    std::vector<Access> read;
    for (const auto& value : function.values) {
        const auto& access = accesses[value.id.value];
        if (load_kind(value.kind) && access &&
            std::find(read.begin(), read.end(), *access) == read.end()) {
            read.push_back(*access);
        }
    }
    bool inserted = false;
    for (unsigned round = 0; round < 4; ++round) {
        bool added = false;
        for (const auto id : order) {
            const auto& block = function.blocks[id.value];
            if (block.predecessors.size() < 2 || labeled[id.value] ||
                std::any_of(block.predecessors.begin(), block.predecessors.end(),
                            [&](BlockId predecessor) {
                                return !reachable[predecessor.value] ||
                                       !exits[predecessor.value];
                            })) {
                continue;
            }
            const auto candidates = *exits[block.predecessors.front().value];
            for (const auto& candidate : candidates) {
                const auto type = function.values[candidate.value.value].type;
                if (std::find(read.begin(), read.end(), candidate.access) ==
                        read.end() ||
                    std::any_of(joined[id.value].begin(), joined[id.value].end(),
                                [&](const Available& phi) {
                                    return phi.access == candidate.access;
                                })) {
                    continue;
                }
                ManagedValue phi;
                phi.kind = ValueKind::Phi;
                phi.type = type;
                phi.location = block.location;
                bool everywhere = true;
                bool differs = false;
                for (const auto predecessor : block.predecessors) {
                    const auto& state = *exits[predecessor.value];
                    const auto found = std::find_if(
                        state.begin(), state.end(), [&](const Available& entry) {
                            return entry.access == candidate.access &&
                                   function.values[entry.value.value].type == type;
                        });
                    if (found == state.end()) {
                        everywhere = false;
                        break;
                    }
                    differs = differs || found->value != candidate.value;
                    phi.incoming.push_back({predecessor, found->value});
                }
                if (!everywhere || !differs) continue;
                phi.id = ValueId{static_cast<std::uint32_t>(function.values.size())};
                const auto phi_id = phi.id;
                function.values.push_back(std::move(phi));
                auto& values = function.blocks[id.value].values;
                values.insert(values.begin(), phi_id);
                joined[id.value].push_back({candidate.access, phi_id});
                added = true;
            }
        }
        if (!added) break;
        inserted = true;
        accesses.resize(function.values.size());
        if (!solve()) return true;
    }
    for (const auto id : order) {
        auto state = entry_state(id);
        transfer(function.blocks[id.value], state, true);
    }
    if (removed.empty()) return inserted;
    for (const auto& view : views) {
        ManagedValue cast;
        cast.id = ValueId{static_cast<std::uint32_t>(function.values.size())};
        cast.location = view.location;
        cast.type = view.type;
        cast.kind = ValueKind::Cast;
        cast.cast = CastOperation::Reinterpret;
        cast.operands = {view.source};
        auto& values = function.blocks[view.block.value].values;
        values.insert(std::find(values.begin(), values.end(), view.load), cast.id);
        function.values.push_back(std::move(cast));
    }
    replacements.apply(function);
    remove_effectful_values(function, removed);
    return true;
}

ConstantObjects::ConstantObjects(const hir::Module& hir_module,
                                 const data::Module& data_module,
                                 const CompilerOptions& options)
    : byte_order_(data_module.byte_order) {
    const auto qualified = [&](auto&& self, hir::TypeId id) -> bool {
        const auto& type = hir_module.type(id);
        return type.is_volatile || type.is_atomic ||
               (type.kind == hir::Type::Kind::Array && type.element &&
                self(self, *type.element));
    };
    for (const auto& object : data_module.objects) {
        const auto& entity = hir_module.object(object.source);
        // A definition that the link or loader can replace stays opaque: a
        // weak or preemptible one, and an alias.
        const bool preemptible =
            entity.linkage == Linkage::Global &&
            entity.visibility == hir::SymbolVisibility::Default &&
            options.position_independent && !options.pie &&
            options.semantic_interposition;
        if (!object.read_only || !entity.definition || entity.weak ||
            entity.alias_target || entity.weakref_target ||
            entity.fixed_address || entity.is_thread_local || preemptible ||
            qualified(qualified, entity.type)) {
            continue;
        }
        Image image;
        image.bytes.assign(object.size, 0);
        switch (object.initializer) {
        case data::InitializerKind::Zero: break;
        case data::InitializerKind::Integer:
        case data::InitializerKind::Floating: {
            const auto& type = hir_module.type(entity.type);
            const auto bytes = std::min<std::size_t>(
                object.size, type.kind == hir::Type::Kind::Builtin &&
                                     type.builtin == BuiltinType::F80
                                 ? 10U : object.size);
            for (std::size_t index = 0; index < bytes && index < 16; ++index) {
                const auto shift = static_cast<unsigned>(
                    8 * (byte_order_ == ByteOrder::Little ? index
                                                          : bytes - 1U - index));
                image.bytes[index] = static_cast<unsigned char>(
                    (shift < 64 ? object.bits.low >> shift
                                : object.bits.high >> (shift - 64U)) & 0xffU);
            }
            break;
        }
        case data::InitializerKind::Bytes:
        case data::InitializerKind::Aggregate:
            image.bytes = object.bytes;
            image.relocations = object.relocations;
            break;
        default: continue;
        }
        // -fzero-init-in-data keeps all-zero objects patchable in the image.
        if (options.zero_init_in_data &&
            std::all_of(image.bytes.begin(), image.bytes.end(),
                        [](unsigned char byte) { return byte == 0; })) {
            continue;
        }
        images_.emplace(object.source.value, std::move(image));
    }
}

std::optional<UInt128> ConstantObjects::read(hir::ObjectId object,
                                             std::int64_t offset,
                                             unsigned bytes) const {
    const auto found = images_.find(object.value);
    if (found == images_.end() || offset < 0 || bytes == 0 || bytes > 16) {
        return std::nullopt;
    }
    const auto& image = found->second;
    const auto begin = static_cast<std::uint64_t>(offset);
    if (begin > image.bytes.size() || bytes > image.bytes.size() - begin ||
        std::any_of(image.relocations.begin(), image.relocations.end(),
                    [&](const data::Relocation& relocation) {
                        return relocation.offset < begin + bytes &&
                               begin < relocation.offset + relocation.size;
                    })) {
        return std::nullopt;
    }
    UInt128 result;
    for (unsigned index = 0; index < bytes; ++index) {
        const auto byte = image.bytes[begin + (byte_order_ == ByteOrder::Little
                                                   ? index
                                                   : bytes - 1U - index)];
        const auto shift = 8 * index;
        if (shift < 64) result.low |= std::uint64_t{byte} << shift;
        else result.high |= std::uint64_t{byte} << (shift - 64U);
    }
    return result;
}

bool fold_constant_object_reads(ManagedFunction& function,
                                const hir::Module& hir_module,
                                const ConstantObjects& objects,
                                const TargetInfo& target) {
    const AccessAnalysis analysis(function, hir_module, target);
    ValueReplacements replacements;
    std::unordered_set<std::uint32_t> removed;
    for (auto& block : function.blocks) {
        std::vector<ValueId> rewritten;
        rewritten.reserve(block.values.size());
        for (const auto id : block.values) {
            const auto& load = function.values[id.value];
            const auto access =
                load_kind(load.kind) ? analysis.access(load) : std::nullopt;
            const auto& type = hir_module.type(load.type);
            const bool floating = type.kind == hir::Type::Kind::Builtin &&
                                  type.builtin >= BuiltinType::F32 &&
                                  type.builtin <= BuiltinType::Fptr;
            const bool scalar =
                type.kind == hir::Type::Kind::Pointer ||
                (type.kind == hir::Type::Kind::Builtin &&
                 type.builtin != BuiltinType::Void &&
                 type.builtin != BuiltinType::Label);
            std::optional<UInt128> bits;
            if (access && scalar && !access->address.index &&
                access->address.base == Address::Base::Object) {
                const auto bytes = type.kind == hir::Type::Kind::Builtin &&
                                           type.builtin == BuiltinType::F80
                                       ? 10U
                                       : static_cast<unsigned>(access->size);
                bits = objects.read(hir::ObjectId{access->address.id},
                                    access->address.offset, bytes);
                if (bits && type.kind == hir::Type::Kind::Builtin &&
                    type.builtin == BuiltinType::Bool && bits->low > 1) {
                    bits.reset();
                }
            }
            if (bits) {
                ManagedValue constant;
                constant.id = ValueId{
                    static_cast<std::uint32_t>(function.values.size())};
                constant.location = load.location;
                constant.inline_site = load.inline_site;
                constant.type = load.type;
                constant.kind = floating ? ValueKind::ConstantFloating
                                         : ValueKind::ConstantInteger;
                constant.integer = bits->low;
                constant.integer_high = bits->high;
                rewritten.push_back(constant.id);
                replacements.add(id, constant.id);
                removed.insert(id.value);
                function.values.push_back(std::move(constant));
            }
            rewritten.push_back(id);
        }
        block.values = std::move(rewritten);
    }
    if (removed.empty()) return false;
    replacements.apply(function);
    remove_effectful_values(function, removed);
    return true;
}

} // namespace cross::mir
