#pragma once

#include "ark/message.hpp"

#include <filesystem>
#include <optional>
#include <functional>
#include <vector>

namespace ark {

class CheckpointManager {
public:
    using StepRunner = std::function<Json(std::string_view step, const Json& prior_results)>;
    explicit CheckpointManager(std::filesystem::path root = "data/sage/checkpoints");
    void save(std::string_view loop_id, std::string_view step_name, const Json& data) const;
    std::optional<Json> load(std::string_view loop_id, std::string_view step_name) const;
    std::string get_resume_point(std::string_view loop_id) const;
    std::string next_step(std::string_view loop_id, const std::vector<std::string>& workflow) const;
    Json run_workflow(std::string_view loop_id, const std::vector<std::string>& workflow, const StepRunner& runner) const;
    std::vector<std::string> list_checkpoints(std::string_view loop_id) const;
    Json list_loops() const;
    void clear(std::string_view loop_id) const;

private:
    std::filesystem::path root_;
    std::filesystem::path loop_path(std::string_view loop_id) const;
    std::filesystem::path step_path(std::string_view loop_id, std::string_view step_name) const;
};

} // namespace ark
