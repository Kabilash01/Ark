#include "ark/conflict_resolver.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace ark {
namespace {
std::mutex observer_mutex;
ResourceConflictObserver resource_observer;
using Clock = std::chrono::steady_clock;
} // namespace

void set_resource_conflict_observer(ResourceConflictObserver observer) {
    const std::lock_guard lock(observer_mutex);
    resource_observer = std::move(observer);
}

ToolSemaphore::Permit::Permit(Permit&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}

ToolSemaphore::Permit::~Permit() {
    if (owner_) owner_->release();
}

ToolSemaphore::ToolSemaphore(std::size_t capacity, std::string_view name) : available_(capacity), name_(name) {
    if (capacity == 0) throw std::invalid_argument("Tool capacity must be positive");
}

ToolSemaphore::Permit ToolSemaphore::acquire() {
    std::unique_lock lock(mutex_);
    const auto ticket = next_++;
    const bool waited = ticket != serving_ || available_ == 0;
    const auto start = Clock::now();
    ready_.wait(lock, [&] { return available_ > 0 && ticket == serving_; });
    --available_;
    ++serving_;
    lock.unlock();
    ready_.notify_all();
    Permit permit(*this);
    if (waited && !name_.empty()) {
        try {
            ResourceConflictObserver observer;
            {
                const std::lock_guard guard(observer_mutex);
                observer = resource_observer;
            }
            if (observer) observer(name_, std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start));
        } catch (const std::exception&) {
            // Telemetry must not prevent the acquired tool from running.
        }
    }
    return permit;
}

std::size_t ToolSemaphore::queued() const {
    const std::lock_guard lock(mutex_);
    return next_ - serving_;
}

void ToolSemaphore::release() {
    {
        const std::lock_guard lock(mutex_);
        ++available_;
    }
    ready_.notify_all();
}

ToolSemaphore& tool_semaphore(Tool tool) {
    static ToolSemaphore browser(1, "browser");
    static ToolSemaphore terminal(2, "terminal");
    static ToolSemaphore arxiv(3, "arxiv");
    switch (tool) {
    case Tool::browser: return browser;
    case Tool::terminal: return terminal;
    case Tool::arxiv: return arxiv;
    }
    throw std::invalid_argument("Unknown limited tool");
}

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
    const Json elapsed = winner.enqueued == Clock::time_point{} ? Json(nullptr)
        : Json(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - winner.enqueued).count());
    return {{"conflict_type", "priority"}, {"status", "SELECTED"}, {"elapsed_ms", elapsed},
            {"task_id", winner.id}, {"priority", winner.priority},
            {"queued_at", winner.timestamp}, {"domain", winner.domain}, {"waiting", waiting}};
}

Json summarize_conflicts(Json events) {
    std::map<std::string, Json> latest;
    for (const auto& event : events) latest[event.at("case_id").get<std::string>()] = event;
    Json groups = Json::object();
    for (const auto name : {"resource", "output", "priority", "memory"}) {
        groups[name] = {{"cases", 0}, {"outcomes", Json::object()}, {"timed_cases", 0}, {"total_elapsed_ms", 0.0}};
    }
    for (const auto& [id, event] : latest) {
        auto& group = groups.at(event.at("conflict_type").get<std::string>());
        group["cases"] = group.at("cases").get<int>() + 1;
        const auto outcome = event.at("status").get<std::string>();
        auto& outcomes = group.at("outcomes");
        outcomes[outcome] = outcomes.value(outcome, 0) + 1;
        const auto elapsed = event.value("elapsed_ms", Json(nullptr));
        if (elapsed.is_number()) {
            group["timed_cases"] = group.at("timed_cases").get<int>() + 1;
            group["total_elapsed_ms"] = group.at("total_elapsed_ms").get<double>() + elapsed.get<double>();
        }
    }
    for (auto& group : groups) {
        const auto n = group.at("timed_cases").get<int>();
        group["mean_elapsed_ms"] = n ? Json(group.at("total_elapsed_ms").get<double>() / n) : Json(nullptr);
        group.erase("total_elapsed_ms");
    }
    return {{"window_days", 30}, {"generated_at", utc_timestamp()}, {"distinct_cases", latest.size()},
            {"by_type", std::move(groups)}, {"events", std::move(events)}};
}

} // namespace ark
