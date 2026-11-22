#pragma once

#include "ark/message.hpp"

#include <span>

namespace ark {

struct PendingTask {
    std::string id;
    int priority;
    std::string timestamp;
    std::string domain;
};

class ConflictResolver {
public:
    static bool detect_priority_conflict(std::span<const PendingTask> pending);
    std::size_t resolve_priority(std::span<const PendingTask> pending);
    static Json log_conflict(std::span<const PendingTask> pending, std::size_t selected);

private:
    std::string last_domain_;
};

} // namespace ark
