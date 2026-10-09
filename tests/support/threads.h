// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <latch>
#include <thread>
#include <vector>

// Threads that start their work together, so that the work of each overlaps that of the others even when it is short.

namespace openxisf::test {

/// Runs body(t) on count threads, t from 0 to count - 1, and returns once every thread is done. Every thread waits
/// until all have started before it calls body.
template <typename Body> void run_threads(std::size_t count, const Body& body)
{
    std::latch started(static_cast<std::ptrdiff_t>(count));
    std::vector<std::thread> threads;
    threads.reserve(count);
    for (std::size_t t = 0; t < count; ++t) {
        threads.emplace_back([&started, &body, t] {
            started.arrive_and_wait();
            body(t);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
}

} // namespace openxisf::test
