#pragma once

#include "ark/message.hpp"

#include <condition_variable>
#include <mutex>
#include <span>

namespace ark {

enum class Tool { browser, terminal, arxiv };

class ToolSemaphore {
public:
    class Permit {
    public:
        Permit(Permit&& other) noexcept;
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        ~Permit();

    private:
        friend class ToolSemaphore;
        explicit Permit(ToolSemaphore& owner) : owner_(&owner) {}
        ToolSemaphore* owner_;
    };

    explicit ToolSemaphore(std::size_t capacity);
    [[nodiscard]] Permit acquire();
    std::size_t queued() const;

private:
    void release();
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::size_t available_;
    std::size_t next_ = 0;
    std::size_t serving_ = 0;
};

ToolSemaphore& tool_semaphore(Tool tool);

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
