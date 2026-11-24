#include "ark/conflict_resolver.hpp"
#include "ark/storage.hpp"

#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
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

void conflict_journal_summarizes_sanitized_cases() {
    const auto path = std::filesystem::temp_directory_path() / (ark::random_uuid() + ".db");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            std::filesystem::remove(path.string() + "-wal", ignored);
            std::filesystem::remove(path.string() + "-shm", ignored);
        }
    } cleanup{path};
    ark::ConflictJournal journal(path);
    for (const auto type : {"resource", "output", "priority", "memory"}) {
        journal.record(type, {{"conflict_type", type}, {"status", "OBSERVED"}, {"elapsed_ms", 12}});
    }
    journal.record("output", {{"conflict_type", "output"}, {"status", "RESOLVED"}, {"elapsed_ms", 22}});
    const auto rows = journal.recent();
    const auto report = ark::summarize_conflicts(rows);
    check(rows.size() == 5 && report.at("distinct_cases") == 4, "journal did not deduplicate case outcomes");
    check(report.at("by_type").at("output").at("cases") == 1
          && report.at("by_type").at("output").at("outcomes").at("RESOLVED") == 1
          && report.at("by_type").at("output").at("mean_elapsed_ms") == 22,
          "latest resolution or elapsed time was not summarized");
    check(rows.at(0).size() == 5 && !rows.at(0).contains("resource"), "journal retained raw conflict details");
}

void resource_waits_are_observed_after_acquisition() {
    using namespace std::chrono_literals;
    ark::ToolSemaphore semaphore(1, "fixture");
    std::mutex observed_mutex;
    std::string observed_resource;
    std::chrono::milliseconds observed_wait{};
    ark::set_resource_conflict_observer([&](std::string_view resource, std::chrono::milliseconds waited) {
        const std::lock_guard lock(observed_mutex);
        observed_resource = resource;
        observed_wait = waited;
    });
    struct Reset { ~Reset() { ark::set_resource_conflict_observer({}); } } reset;
    std::optional<ark::ToolSemaphore::Permit> held{semaphore.acquire()};
    auto waiting = std::async(std::launch::async, [&] { const auto permit = semaphore.acquire(); });
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (semaphore.queued() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    check(semaphore.queued() == 1, "fixture waiter was not queued");
    held.reset();
    waiting.get();
    const std::lock_guard lock(observed_mutex);
    check(observed_resource == "fixture" && observed_wait >= 0ms, "acquired resource wait was not observed");
}

} // namespace

int main() {
    try {
        higher_priority_then_earlier_timestamp();
        ties_rotate_domains_and_preserve_fifo();
        resource_capacity_and_fifo();
        conflict_journal_summarizes_sanitized_cases();
        resource_waits_are_observed_after_acquisition();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
