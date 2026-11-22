#include "ark/conflict_resolver.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace ark {

bool ConflictResolver::detect_priority_conflict(std::span<const PendingTask> pending) {
    return pending.size() > 1;
}

std::size_t ConflictResolver::resolve_priority(std::span<const PendingTask> pending) {
    if (pending.empty()) {
        throw std::invalid_argument("No pending tasks to schedule");
    }
    const auto best = std::min_element(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
        return left.priority != right.priority ? left.priority > right.priority : left.timestamp < right.timestamp;
    });
    std::map<std::string_view, std::size_t> domains;
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto& candidate = pending[index];
        if (candidate.priority == best->priority && candidate.timestamp == best->timestamp) {
            domains.try_emplace(candidate.domain, index);
        }
    }
    const auto next = domains.upper_bound(last_domain_);
    const auto selected = next == domains.end() ? domains.begin() : next;
    last_domain_ = selected->first;
    return selected->second;
}

Json ConflictResolver::log_conflict(std::span<const PendingTask> pending, std::size_t selected) {
    const auto& winner = pending[selected];
    auto waiting = Json::array();
    for (std::size_t index = 0; index < pending.size(); ++index) {
        if (index != selected) {
            const auto& task = pending[index];
            waiting.push_back({{"task_id", task.id}, {"priority", task.priority},
                               {"timestamp", task.timestamp}, {"domain", task.domain}});
        }
    }
    return {{"conflict_type", "priority"}, {"task_id", winner.id}, {"priority", winner.priority},
            {"queued_at", winner.timestamp}, {"domain", winner.domain}, {"waiting", waiting}};
}

} // namespace ark
