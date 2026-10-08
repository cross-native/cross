// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace cross {

// A compiler-host continuation, not a concurrent or emitted runtime task.
// Resumption always goes through the outer pump: neither awaiting a child nor
// finishing it recursively resumes another native frame. Callers retain their
// language/resource controls, evaluation order, typed operands and target policy.
// Keep conditional awaits in explicit if/else statements: the GCC 16.1 Windows
// host was observed to resume an unselected `condition ? co_await task : value`
// arm. Likewise, a structured binding whose object needs destruction must not
// stay in scope across co_await: GCC 16.1 keeps its cleanup guard outside the
// frame and skips the destructor. These are host implementation constraints,
// not Cross evaluation rules.
struct ContinuationSchedule {
    std::coroutine_handle<> next;
};

template<class T>
struct ContinuationResult {
    std::optional<T> result;
    void return_value(T value) { result.emplace(std::move(value)); }
    T take_result() { return std::move(*result); }
};

template<>
struct ContinuationResult<void> {
    void return_void() const noexcept {}
    void take_result() const noexcept {}
};

template<class T>
class ContinuationTask {
public:
    struct promise_type : ContinuationResult<T> {
        ContinuationSchedule* schedule{};
        std::coroutine_handle<> continuation;
        std::exception_ptr failure;

        ContinuationTask get_return_object() noexcept {
            return ContinuationTask(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        struct Finish {
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> frame) const noexcept {
                auto& promise = frame.promise();
                promise.schedule->next = promise.continuation;
            }
            void await_resume() const noexcept {}
        };
        Finish final_suspend() const noexcept { return {}; }
        void unhandled_exception() noexcept { failure = std::current_exception(); }
    };

    ContinuationTask(ContinuationTask&& other) noexcept : frame_(std::exchange(other.frame_, {})) {}
    ContinuationTask(const ContinuationTask&) = delete;
    ContinuationTask& operator=(const ContinuationTask&) = delete;
    ~ContinuationTask() { if (frame_) frame_.destroy(); }

    bool await_ready() const noexcept { return false; }
    template<class Parent>
    void await_suspend(std::coroutine_handle<Parent> parent) noexcept {
        auto& promise = frame_.promise();
        promise.schedule = parent.promise().schedule;
        promise.continuation = parent;
        promise.schedule->next = frame_;
    }
    T await_resume() {
        auto& promise = frame_.promise();
        if (promise.failure) std::rethrow_exception(promise.failure);
        return promise.take_result();
    }

    T run() && {
        ContinuationSchedule schedule{frame_};
        frame_.promise().schedule = &schedule;
        while (schedule.next) {
            const auto next = std::exchange(schedule.next, {});
            next.resume();
        }
        return await_resume();
    }

private:
    explicit ContinuationTask(std::coroutine_handle<promise_type> frame) noexcept : frame_(frame) {}
    std::coroutine_handle<promise_type> frame_;
};

} // namespace cross
