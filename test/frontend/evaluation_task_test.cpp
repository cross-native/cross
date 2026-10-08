// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/evaluation_task.hpp"
#include "common/continuation_query.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using cross::EvaluationTask;
static_assert(std::is_same_v<EvaluationTask<unsigned>, cross::ContinuationTask<unsigned>>);
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::abort(); }
}
struct Live {
    unsigned& count;
    explicit Live(unsigned& value) : count(value) { ++count; }
    ~Live() { --count; }
};
EvaluationTask<unsigned> descend(unsigned depth, unsigned& live, bool fail) {
    Live frame(live);
    if (depth == 0) {
        if (fail) throw std::runtime_error("leaf failure");
        co_return live;
    }
    const auto result = co_await descend(depth - 1, live, fail);
    co_return result;
}
EvaluationTask<unsigned> append(unsigned value, std::vector<unsigned>& order) {
    order.push_back(value);
    co_return value;
}
EvaluationTask<void> descend_void(unsigned depth, unsigned& live, bool fail) {
    Live frame(live);
    if (!depth) {
        if (fail) throw std::runtime_error("void leaf failure");
        co_return;
    }
    co_await descend_void(depth - 1, live, fail);
}
EvaluationTask<unsigned> sequence(std::vector<unsigned>& order) {
    const auto first = co_await append(1, order);
    const auto second = co_await append(2, order);
    unsigned chosen;
    if (first) chosen = co_await append(3, order);
    else chosen = co_await append(99, order);
    co_return first + second + chosen;
}
EvaluationTask<unsigned> read(const unsigned& value) { co_return value; }
EvaluationTask<unsigned> selected_read(bool selected, const unsigned* value) {
    if (selected) co_return co_await read(*value);
    co_return 7U;
}
cross::ContinuationTask<std::unique_ptr<unsigned>> move_only(unsigned depth) {
    if (!depth) co_return std::make_unique<unsigned>(23);
    auto result = co_await move_only(depth - 1);
    co_return result;
}
}

int main() {
    cross::ContinuationQuery<unsigned(unsigned)> immediate = [](unsigned value) { return value + 1; };
    require(immediate(4) == 5 && immediate.async(8).run() == 9,
            "immediate service changed across boundary/continuation calls");
    cross::ContinuationQuery<unsigned(unsigned)> recursive;
    unsigned query_live{};
    bool query_failure{};
    recursive = [&](unsigned depth) -> EvaluationTask<unsigned> {
        Live frame(query_live);
        if (!depth) {
            if (query_failure) throw std::runtime_error("query leaf failure");
            co_return query_live;
        }
        co_return co_await recursive.async(depth - 1);
    };
    require(recursive(50000) == 50001 && query_live == 0,
            "awaitable service restarted native recursion or lost frame cleanup");
    query_failure = true;
    try {
        (void)recursive(50000);
        require(false, "query exception was lost");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "query leaf failure" && query_live == 0,
                "failed service lost its exception or frame cleanup");
    }
    auto owner = std::make_shared<unsigned>(47);
    std::weak_ptr<unsigned> retained = owner;
    cross::ContinuationQuery<unsigned()> replaced;
    replaced = [owner, &replaced]() -> EvaluationTask<unsigned> {
        replaced = {};
        co_return *owner;
    };
    owner.reset();
    require(replaced() == 47 && retained.expired() && !replaced,
            "nested service replacement destroyed a suspended callable or leaked its owner");
    cross::ContinuationQuery<void()> no_result = []() -> EvaluationTask<void> { co_return; };
    no_result.async().run();
    unsigned live{};
    require(descend(50000, live, false).run() == 50001 && live == 0,
            "deep continuation used native recursion or lost frame cleanup");
    try {
        (void)descend(50000, live, true).run();
        require(false, "exception was lost");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "leaf failure" && live == 0,
                "failed continuation lost its exception or frame cleanup");
    }
    std::vector<unsigned> order;
    require(sequence(order).run() == 6 && order == std::vector<unsigned>{1, 2, 3},
            "continuation changed evaluation order or ran an unselected arm");
    const unsigned selected = 11;
    require(selected_read(false, nullptr).run() == 7 && selected_read(true, &selected).run() == 11,
            "conditional continuation ran an unselected child");
    require(*move_only(50000).run() == 23,
            "common continuation lost a move-only parser/lowering result");
    descend_void(50000, live, false).run();
    require(live == 0, "void continuation lost cleanup");
    try {
        descend_void(50000, live, true).run();
        require(false, "void exception was lost");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "void leaf failure" && live == 0,
                "failed void continuation lost its exception or frame cleanup");
    }
}
