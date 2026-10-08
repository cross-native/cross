// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <memory>
#include <utility>

namespace cross::detail {

class OwnerRelease;

// Private host bookkeeping, never source identity, target storage or IR data.
// Copy/move payload operations must not copy a pending owner's queue state.
class OwnerReleaseLink {
public:
    OwnerReleaseLink() = default;
    OwnerReleaseLink(const OwnerReleaseLink&) noexcept {}
    OwnerReleaseLink(OwnerReleaseLink&&) noexcept {}
    OwnerReleaseLink& operator=(const OwnerReleaseLink&) noexcept { return *this; }
    OwnerReleaseLink& operator=(OwnerReleaseLink&&) noexcept { return *this; }
private:
    friend class OwnerRelease;
    OwnerReleaseLink* next_{};
    std::shared_ptr<const void> shared_;
    const void* unique_{};
    void (*delete_unique_)(const void*) noexcept{};
};

// One allocation-free drain for connected host ownership graphs. Destructors
// expose their outgoing edges only after actual destruction has begun. Pending
// shared values therefore retain their full public graph if a weak observer
// acquires another owner before their normal control-block release.
class OwnerRelease {
public:
    template<class Detach>
    static void run(Detach&& detach) noexcept {
        if (active_) {
            detach(*active_);
            return;
        }
        OwnerRelease scope;
        active_ = &scope;
        detach(scope);
        scope.drain();
        active_ = nullptr;
    }

    template<class Value>
    void take(std::shared_ptr<Value>& owner) noexcept {
        if (!owner || owner.use_count() != 1) {
            owner.reset();
            return;
        }
        auto& link = owner->teardown_;
        // Distinct aliasing control blocks may name the same retained child.
        // Keep its existing queue entry instead of replacing or reinserting it.
        if (link.shared_ || link.unique_) {
            owner.reset();
            return;
        }
        link.shared_ = std::move(owner);
        enqueue(link);
    }

    template<class Value>
    void take(std::unique_ptr<Value>& owner) noexcept {
        if (auto* value = owner.release()) {
            auto& link = value->teardown_;
            assert(!link.shared_ && !link.unique_);
            link.unique_ = value;
            link.delete_unique_ = [](const void* object) noexcept {
                delete static_cast<const Value*>(object);
            };
            enqueue(link);
        }
    }
private:
    inline static thread_local OwnerRelease* active_{};
    OwnerReleaseLink* pending_{};

    void enqueue(OwnerReleaseLink& link) noexcept {
        link.next_ = pending_;
        pending_ = &link;
    }
    void drain() noexcept {
        while (pending_) {
            auto* link = pending_;
            pending_ = link->next_;
            link->next_ = nullptr;
            auto shared = std::move(link->shared_);
            const auto* unique = std::exchange(link->unique_, nullptr);
            const auto delete_unique = std::exchange(link->delete_unique_, nullptr);
            // Clear all intrusive state before real destruction: an aliasing
            // owner may destroy its parent, which then releases this child.
            if (shared) shared.reset();
            else if (delete_unique) delete_unique(unique);
        }
    }
};

} // namespace cross::detail
