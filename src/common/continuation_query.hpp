// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/continuation_task.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace cross {

// A typed compiler-host service with synchronous boundary compatibility.
// Connected continuation clients await async(); only outer/legacy callers
// pump operator(). Retain the selected callable across nested service overrides.
// Referenced arguments must remain owned by the awaiting caller until completion.
template<class Signature>
class ContinuationQuery;

template<class Result, class... Args>
class ContinuationQuery<Result(Args...)> {
public:
    ContinuationQuery() = default;
    ContinuationQuery(std::nullptr_t) {}
    template<class Function>
        requires (!std::is_same_v<std::remove_cvref_t<Function>, ContinuationQuery> &&
                  std::is_invocable_v<Function&, Args...>)
    ContinuationQuery(Function function) {
        if constexpr (std::is_same_v<std::invoke_result_t<Function&, Args...>,
                                     ContinuationTask<Result>>)
            suspended_ = std::move(function);
        else
            immediate_ = std::move(function);
    }

    explicit operator bool() const noexcept { return immediate_ || suspended_; }

    ContinuationTask<Result> async(Args... args) const {
        if (suspended_) {
            const auto query = suspended_;
            co_return co_await query(std::forward<Args>(args)...);
        }
        const auto query = immediate_;
        co_return query(std::forward<Args>(args)...);
    }

    Result operator()(Args... args) const {
        return async(std::forward<Args>(args)...).run();
    }

private:
    std::function<Result(Args...)> immediate_;
    std::function<ContinuationTask<Result>(Args...)> suspended_;
};

} // namespace cross
