#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"
#include "ark/versions.hpp"

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
using AttemptHandler = std::function<Json(const Task&, const Json& dependencies, int attempt, const Json& feedback)>;
using OutputConflictReview = std::function<Json(const Json& research, const Json& code)>;

struct SupervisedRun {
    Json results;
    Json escalations;
    Json conflicts = Json::array();
};

inline constexpr int supervisor_attempt_limit = 3;

// Copies only the task fields needed by the router. Parallelism is derived
// from dependencies instead of trusting the model's parallel_with hints.
std::vector<TaskLayer> order_tasks(const Json& dag);
Json dispatch_plan(const Json& dag, const TaskHandler& execute,
                   const AgentAvailable& available, std::size_t max_workers = 4);
SupervisedRun supervise_plan(const Json& dag, const AttemptHandler& execute,
                             const AgentAvailable& available, std::size_t max_workers = 4,
                             const OutputConflictReview& review_conflict = {});

Json review_output_conflict(const Transport& transport, std::string_view url, std::string_view model,
                            const Json& research, const Json& code);

std::string chat_local(const Transport& transport, std::string_view url, std::string_view model,
                       std::string_view prompt, std::string_view content, const Json& format = "json");

AgentProfile supervisor_profile(std::string model);
AgentProfile planner_profile(std::string model);

class Planner {
public:
    Planner(Transport transport, std::string local_url, std::string model);
    Planner(Transport transport, std::string local_url, AgentProfile supervisor, AgentProfile planner);

    std::string classify(std::string_view goal) const;
    Json plan(std::string_view goal, const std::vector<std::string>& agents,
              const Json& context = Json::array()) const;

private:
    Transport transport_;
    std::string local_url_;
    AgentProfile supervisor_;
    AgentProfile planner_;
};

} // namespace ark
