#include "ark/conflict_resolver.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

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

} // namespace

int main() {
    try {
        higher_priority_then_earlier_timestamp();
        ties_rotate_domains_and_preserve_fifo();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
