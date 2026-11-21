#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ark {

struct Task {
    std::string id;
    std::string agent;
    std::string intent;
    std::string instruction;
    std::vector<std::string> dependencies;
};

using TaskLayer = std::vector<Task>;
using TaskHandler = std::function<Json(const Task&, const Json& dependency_results)>;
using AgentAvailable = std::function<bool(std::string_view)>;

// Copies only the task fields needed by the router. Parallelism is derived
// from dependencies instead of trusting the model's parallel_with hints.
std::vector<TaskLayer> order_tasks(const Json& dag);
Json dispatch_plan(const Json& dag, const TaskHandler& execute,
                   const AgentAvailable& available, std::size_t max_workers = 4);

std::string chat_local(const Transport& transport, std::string_view url, std::string_view model,
                       std::string_view prompt, std::string_view content, bool json_format);

class Planner {
public:
    Planner(Transport transport, std::string local_url, std::string model);

    std::string classify(std::string_view goal) const;
    Json plan(std::string_view goal, const std::vector<std::string>& agents) const;

private:
    Transport transport_;
    std::string local_url_;
    std::string model_;
};

} // namespace ark
