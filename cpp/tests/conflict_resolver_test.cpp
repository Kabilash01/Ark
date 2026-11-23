#include "ark/conflict_resolver.hpp"

#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void higher_priority_then_earlier_timestamp() {
    const std::array<ark::PendingTask, 3> pending{{
        {"low", 2, "2026-01-01T00:00:01+00:00", "code"},
        {"new", 7, "2026-01-01T00:00:03+00:00", "research"},
        {"old", 7, "2026-01-01T00:00:02+00:00", "code"},
    }};
    ark::ConflictResolver resolver;
    check(resolver.detect_priority_conflict(pending), "competing tasks must be detected");
    const auto winner = resolver.resolve_priority(pending);
    check(winner == 2, "expected the earliest of the highest-priority tasks");
    const auto log = resolver.log_conflict(pending, winner);
    check(log.at("task_id") == "old" && log.at("waiting").size() == 2, "incorrect conflict report");
    check(!resolver.detect_priority_conflict(std::span(pending).first(1)), "a single task is uncontested");
}

void ties_rotate_domains_and_preserve_fifo() {
    std::vector<ark::PendingTask> pending{
        {"code-1", 5, "2026-01-01T00:00:01+00:00", "code"},
        {"code-2", 5, "2026-01-01T00:00:01+00:00", "code"},
        {"research-1", 5, "2026-01-01T00:00:01+00:00", "research"},
        {"research-2", 5, "2026-01-01T00:00:01+00:00", "research"},
    };
    ark::ConflictResolver resolver;
    for (const auto* expected : {"code-1", "research-1", "code-2", "research-2"}) {
        const auto winner = resolver.resolve_priority(pending);
        check(pending[winner].id == expected, "domain rotation or same-domain FIFO order changed");
        pending.erase(pending.begin() + winner);
    }
}

void resource_capacity_and_fifo() {
    using namespace std::chrono_literals;
    for (const auto& [tool, capacity] : std::array{
            std::pair{ark::Tool::browser, 1}, std::pair{ark::Tool::terminal, 2}, std::pair{ark::Tool::arxiv, 3}}) {
        auto& semaphore = ark::tool_semaphore(tool);
        std::vector<ark::ToolSemaphore::Permit> permits;
        std::vector<std::size_t> order;
        std::mutex order_mutex;
        std::vector<std::future<void>> workers;
        auto enter = [&](std::size_t id) {
            workers.push_back(std::async(std::launch::async, [&, id] {
                const auto permit = semaphore.acquire();
                const std::lock_guard lock(order_mutex);
                order.push_back(id);
            }));
            const auto deadline = std::chrono::steady_clock::now() + 1s;
            while (semaphore.queued() < id && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
            return semaphore.queued() == id;
        };
        for (int i = 0; i < capacity; ++i) permits.push_back(semaphore.acquire());
        const bool first_waited = enter(1);
        const bool second_waited = enter(2);
        permits.pop_back();
        for (auto& worker : workers) worker.get();
        check(first_waited && second_waited && order == std::vector<std::size_t>{1, 2},
              "saturated tool did not resume its users in FIFO order");
    }

    ark::ToolSemaphore semaphore(1);
    try {
        const auto permit = semaphore.acquire();
        throw std::runtime_error("simulated tool failure");
    } catch (const std::runtime_error&) {}
    const auto permit = semaphore.acquire();
    check(semaphore.queued() == 0, "exception path lost its permit");
}

} // namespace

int main() {
    try {
        higher_priority_then_earlier_timestamp();
        ties_rotate_domains_and_preserve_fifo();
        resource_capacity_and_fifo();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
