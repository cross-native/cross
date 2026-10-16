// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <new>
#include <optional>
#include <utility>

namespace cross {

// Continuation frames come and go in nearly LIFO order, and evaluator frames
// are tens of kilobytes. A per-thread cache of freed frames by size, up to
// `capacity` bytes, keeps the general allocator from trimming and refaulting
// its heap at every evaluation step. Frames cached when a thread ends stay
// allocated.
namespace continuation_frames {
inline constexpr std::size_t granule = 64;
inline constexpr std::size_t classes = 1024;
inline constexpr std::size_t capacity = std::size_t{16} << 20;
struct Cache {
    void* free[classes];
    std::size_t bytes;
};
inline thread_local Cache cache{};

inline void* allocate(std::size_t size) {
    const auto index = (size + granule - 1) / granule;
    if (index >= classes) return ::operator new(size);
    if (void* frame = cache.free[index]) {
        cache.free[index] = *static_cast<void**>(frame);
        cache.bytes -= index * granule;
        return frame;
    }
    return ::operator new(index * granule);
}

inline void release(void* frame, std::size_t size) noexcept {
    const auto index = (size + granule - 1) / granule;
    if (index >= classes || cache.bytes + index * granule > capacity) {
        ::operator delete(frame);
        return;
    }
    *static_cast<void**>(frame) = cache.free[index];
    cache.free[index] = frame;
    cache.bytes += index * granule;
}
} // namespace continuation_frames

// A compiler-host continuation, not a concurrent or emitted runtime task.
// Resumption always goes through the outer pump: neither awaiting a child nor
// finishing it recursively resumes another native frame. Callers retain their
// language/resource controls, evaluation order, typed operands and target policy.
// Keep a co_await that `&&`, `||`, or `?:` may skip in an explicit if/else
// statement unless that operator is the whole condition of an if statement or
// the whole initializer of a declaration, or is itself an operand of such a
// `&&` or `?:` or the right operand of such a `||`. Elsewhere, such as in a
// loop condition, a co_return operand, or a call argument, GCC 16.1 runs the
// skipped task anyway and drops its result and exception. Likewise, a
// structured binding whose object needs destruction must not stay in scope
// across co_await: GCC 16.1 keeps its cleanup guard outside the frame and skips
// the destructor. These are host implementation constraints, not Cross
// evaluation rules.
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

        static void* operator new(std::size_t size) {
            return continuation_frames::allocate(size);
        }
        static void operator delete(void* frame, std::size_t size) noexcept {
            continuation_frames::release(frame, size);
        }

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
