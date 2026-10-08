// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace cross {

// Dominance over a control-flow graph whose blocks are numbered densely. The
// immediate dominators come from Cooper, Harvey, and Kennedy's "A Simple, Fast
// Dominance Algorithm"; queries compare dominator-tree interval numbers.
// Blocks unreachable from the entry dominate nothing and are dominated by
// nothing.
class Dominance {
public:
    // successors(block) and predecessors(block) return ranges of IDs with a
    // `value` member.
    template<class Successors, class Predecessors>
    Dominance(std::size_t count, std::uint32_t entry, const Successors& successors,
              const Predecessors& predecessors)
        : reachable_(count), idom_(count, none), children_(count),
          enter_(count), exit_(count) {
        if (entry >= count) return;
        std::vector<std::uint32_t> postorder;
        std::vector<std::uint32_t> number(count, none);
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{entry, 0}};
        reachable_[entry] = true;
        while (!stack.empty()) {
            const auto [block, next] = stack.back();
            const auto& edges = successors(block);
            if (next < edges.size()) {
                ++stack.back().second;
                const auto successor = edges[next].value;
                if (successor < count && !reachable_[successor]) {
                    reachable_[successor] = true;
                    stack.push_back({successor, 0});
                }
                continue;
            }
            number[block] = static_cast<std::uint32_t>(postorder.size());
            postorder.push_back(block);
            stack.pop_back();
        }

        idom_[entry] = entry;
        const auto intersect = [&](std::uint32_t left, std::uint32_t right) {
            while (left != right) {
                while (number[left] < number[right]) left = idom_[left];
                while (number[right] < number[left]) right = idom_[right];
            }
            return left;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (auto item = postorder.rbegin(); item != postorder.rend(); ++item) {
                if (*item == entry) continue;
                auto selected = none;
                for (const auto predecessor : predecessors(*item)) {
                    if (predecessor.value >= count || idom_[predecessor.value] == none) continue;
                    selected = selected == none ? predecessor.value
                                                : intersect(predecessor.value, selected);
                }
                if (selected != none && idom_[*item] != selected) {
                    idom_[*item] = selected;
                    changed = true;
                }
            }
        }

        idom_[entry] = none;
        for (std::uint32_t block = 0; block < count; ++block)
            if (idom_[block] != none) children_[idom_[block]].push_back(block);
        std::uint32_t clock = 0;
        std::vector<std::pair<std::uint32_t, std::size_t>> walk{{entry, 0}};
        enter_[entry] = clock++;
        while (!walk.empty()) {
            const auto [block, next] = walk.back();
            if (next < children_[block].size()) {
                ++walk.back().second;
                const auto child = children_[block][next];
                enter_[child] = clock++;
                walk.push_back({child, 0});
                continue;
            }
            exit_[block] = clock++;
            walk.pop_back();
        }
    }

    [[nodiscard]] bool reachable(std::uint32_t block) const {
        return block < reachable_.size() && reachable_[block];
    }
    [[nodiscard]] bool dominates(std::uint32_t dominator, std::uint32_t block) const {
        return reachable(dominator) && reachable(block) &&
               enter_[dominator] <= enter_[block] && exit_[block] <= exit_[dominator];
    }
    [[nodiscard]] std::optional<std::uint32_t> immediate_dominator(std::uint32_t block) const {
        if (block >= idom_.size() || idom_[block] == none) return std::nullopt;
        return idom_[block];
    }
    // Dominator-tree children in increasing block order.
    [[nodiscard]] const std::vector<std::uint32_t>& children(std::uint32_t block) const {
        return children_[block];
    }

private:
    static constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    std::vector<bool> reachable_;
    std::vector<std::uint32_t> idom_;
    std::vector<std::vector<std::uint32_t>> children_;
    std::vector<std::uint32_t> enter_;
    std::vector<std::uint32_t> exit_;
};

// Strongly connected component number of each block (Tarjan's algorithm);
// blocks on a common cycle share a number. successors(block) returns a range
// of IDs with a `value` member.
template<class Successors>
std::vector<std::uint32_t> strongly_connected_components(std::size_t count,
                                                         const Successors& successors) {
    constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> component(count, none), index(count, none), low(count);
    std::vector<std::uint32_t> stack;
    std::vector<bool> on_stack(count);
    std::vector<std::pair<std::uint32_t, std::size_t>> frames;
    std::uint32_t next_index = 0, next_component = 0;
    const auto open = [&](std::uint32_t block) {
        index[block] = low[block] = next_index++;
        stack.push_back(block);
        on_stack[block] = true;
        frames.push_back({block, 0});
    };
    for (std::uint32_t root = 0; root < count; ++root) {
        if (index[root] != none) continue;
        open(root);
        while (!frames.empty()) {
            const auto [block, next] = frames.back();
            const auto& edges = successors(block);
            if (next < edges.size()) {
                ++frames.back().second;
                const auto successor = edges[next].value;
                if (successor >= count) continue;
                if (index[successor] == none) open(successor);
                else if (on_stack[successor]) low[block] = std::min(low[block], index[successor]);
                continue;
            }
            frames.pop_back();
            if (!frames.empty())
                low[frames.back().first] = std::min(low[frames.back().first], low[block]);
            if (low[block] != index[block]) continue;
            for (auto member = none; member != block;) {
                member = stack.back();
                stack.pop_back();
                on_stack[member] = false;
                component[member] = next_component;
            }
            ++next_component;
        }
    }
    return component;
}

} // namespace cross
