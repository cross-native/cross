// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <thread>

extern "C" std::uint32_t compound_counter_next();
extern "C" std::uint32_t compound_counter_get();

int main() {
    constexpr std::size_t workers = 4, iterations = 5000;
    std::array<std::array<std::uint32_t, iterations>, workers> results{};
    std::array<std::thread, workers> threads;
    for (std::size_t worker = 0; worker != workers; ++worker) {
        threads[worker] = std::thread([&, worker] {
            for (auto& result : results[worker]) result = compound_counter_next();
        });
    }
    for (auto& thread : threads) thread.join();
    if (compound_counter_get() != workers * iterations) return 2;
    std::array<bool, workers * iterations + 1> observed{};
    for (const auto& batch : results) {
        for (const auto result : batch) {
            if (result == 0 || result >= observed.size() || observed[result]) return 3;
            observed[result] = true;
        }
    }
    return 1;
}
