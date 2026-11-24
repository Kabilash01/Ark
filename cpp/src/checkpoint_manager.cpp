#include "ark/checkpoint_manager.hpp"
#include "ark/runtime_files.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace ark {
namespace {

constexpr std::size_t max_checkpoint_bytes = 16 * 1024 * 1024;
const std::regex safe_component(R"([A-Za-z0-9][A-Za-z0-9_-]{0,63})");

void validate_component(std::string_view value, std::string_view kind) {
    if (!std::regex_match(value.begin(), value.end(), safe_component)) throw std::invalid_argument(std::string("Invalid checkpoint ") + std::string(kind));
}

Json read_checkpoint_json(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > max_checkpoint_bytes) throw std::runtime_error("Checkpoint file is missing or exceeds 16 MiB");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open checkpoint file");
    std::string text(static_cast<std::size_t>(size), '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file || static_cast<std::size_t>(file.gcount()) != text.size()) throw std::runtime_error("Could not read complete checkpoint file");
    return Json::parse(text);
}

} // namespace

CheckpointManager::CheckpointManager(std::filesystem::path root) : root_(std::filesystem::absolute(std::move(root)).lexically_normal()) {}

std::filesystem::path CheckpointManager::loop_path(std::string_view loop_id) const {
    validate_component(loop_id, "loop ID");
    return root_ / std::string(loop_id);
}

std::filesystem::path CheckpointManager::step_path(std::string_view loop_id, std::string_view step_name) const {
    validate_component(step_name, "step name");
    return loop_path(loop_id) / (std::string(step_name) + ".json");
}

void CheckpointManager::save(std::string_view loop_id, std::string_view step_name, const Json& data) const {
    if (!data.is_object() || data.dump().size() > max_checkpoint_bytes) throw std::invalid_argument("Checkpoint data must be an object under 16 MiB");
    const auto directory = loop_path(loop_id);
    std::filesystem::create_directories(directory);
    const auto file = step_path(loop_id, step_name);
    write_json(file, data);

    Json completed = Json::array();
    const auto status_path = directory / "status.json";
    if (std::filesystem::exists(status_path)) {
        try { completed = read_checkpoint_json(status_path).value("completed_steps", Json::array()); }
        catch (const std::exception&) { completed = Json::array(); }
    }
    if (!completed.is_array()) completed = Json::array();
    const std::string step(step_name);
    completed.erase(std::remove(completed.begin(), completed.end(), Json(step)), completed.end());
    completed.push_back(step);
    write_json(status_path, {{"loop_id", loop_id}, {"step", step}, {"status", "completed"},
        {"timestamp", utc_timestamp()}, {"completed_steps", std::move(completed)}});
}

std::optional<Json> CheckpointManager::load(std::string_view loop_id, std::string_view step_name) const {
    const auto file = step_path(loop_id, step_name);
    if (!std::filesystem::exists(file)) return std::nullopt;
    return read_checkpoint_json(file);
}

std::string CheckpointManager::get_resume_point(std::string_view loop_id) const {
    const auto status = loop_path(loop_id) / "status.json";
    if (!std::filesystem::exists(status)) return {};
    const auto saved = read_checkpoint_json(status);
    return saved.value("step", "");
}

std::string CheckpointManager::next_step(std::string_view loop_id, const std::vector<std::string>& workflow) const {
    if (workflow.size() > 100) throw std::invalid_argument("Checkpoint workflow exceeds 100 steps");
    std::vector<std::string> seen;
    for (const auto& step : workflow) {
        validate_component(step, "workflow step");
        if (std::find(seen.begin(), seen.end(), step) != seen.end()) throw std::invalid_argument("Checkpoint workflow steps must be unique");
        seen.push_back(step);
        if (!load(loop_id, step)) return step;
    }
    return {};
}

Json CheckpointManager::run_workflow(std::string_view loop_id, const std::vector<std::string>& workflow, const StepRunner& runner) const {
    if (!runner || workflow.empty() || workflow.size() > 100) throw std::invalid_argument("Checkpoint workflow requires 1–100 steps and a runner");
    std::vector<std::string> seen;
    Json results = Json::object();
    for (const auto& step : workflow) {
        validate_component(step, "workflow step");
        if (std::find(seen.begin(), seen.end(), step) != seen.end()) throw std::invalid_argument("Checkpoint workflow steps must be unique");
        seen.push_back(step);
        if (const auto saved = load(loop_id, step)) results[step] = *saved;
        else {
            const auto output = runner(step, results);
            if (!output.is_object() || output.dump().size() > max_checkpoint_bytes) throw std::runtime_error("Workflow step must return an object under 16 MiB");
            save(loop_id, step, output);
            results[step] = output;
        }
    }
    return results;
}

std::vector<std::string> CheckpointManager::list_checkpoints(std::string_view loop_id) const {
    const auto directory = loop_path(loop_id);
    if (!std::filesystem::exists(directory)) return {};
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json" || entry.path().filename() == "status.json") continue;
        const auto name = entry.path().stem().string();
        validate_component(name, "step file name");
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

Json CheckpointManager::list_loops() const {
    auto loops = Json::array();
    if (!std::filesystem::exists(root_)) return loops;
    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        if (!entry.is_directory()) continue;
        const auto id = entry.path().filename().string();
        if (!std::regex_match(id, safe_component)) continue;
        const auto status = entry.path() / "status.json";
        if (std::filesystem::exists(status)) loops.push_back(read_checkpoint_json(status));
    }
    return loops;
}

void CheckpointManager::clear(std::string_view loop_id) const {
    std::error_code error;
    std::filesystem::remove_all(loop_path(loop_id), error);
    if (error) throw std::filesystem::filesystem_error("Could not clear SAGE checkpoints", error);
}

} // namespace ark
