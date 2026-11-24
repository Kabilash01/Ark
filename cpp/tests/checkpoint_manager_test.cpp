#include "ark/checkpoint_manager.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

void saves_resumes_lists_and_clears() {
    const auto root = std::filesystem::temp_directory_path() / ("ark-sage-checkpoints-" + ark::random_uuid());
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
    const ark::CheckpointManager manager(root);
    const std::vector<std::string> workflow{"literature", "hypothesis", "experiment", "analysis", "report",
        "experiment_second", "analysis_second", "report_second"};
    check(!manager.load("loop-1", "literature") && manager.get_resume_point("loop-1").empty(), "missing checkpoint should be empty");
    manager.save("loop-1", "literature", {{"papers", 20}, {"summary", "retrieved"}});
    manager.save("loop-1", "hypothesis", {{"count", 5}});
    check(manager.load("loop-1", "literature")->at("papers") == 20, "checkpoint data was not persisted");
    check(manager.get_resume_point("loop-1") == "hypothesis" && manager.next_step("loop-1", workflow) == "experiment",
        "resume status did not point to the last completed and next missing step");
    check(manager.list_checkpoints("loop-1") == std::vector<std::string>({"hypothesis", "literature"}), "checkpoint listing was incorrect");
    check(manager.list_loops().size() == 1, "loop listing omitted saved status");
    int calls = 0;
    bool interrupted = false;
    try {
        (void)manager.run_workflow("loop-1", workflow, [&](std::string_view step, const Json&) -> Json {
            ++calls;
            if (step == "experiment") { interrupted = true; throw std::runtime_error("simulated interruption"); }
            return {{"step", step}};
        });
    } catch (const std::runtime_error&) {}
    check(interrupted && calls == 1, "workflow did not stop at the interrupted first missing step");
    calls = 0;
    const auto resumed = manager.run_workflow("loop-1", workflow, [&](std::string_view step, const Json&) {
        ++calls; return Json{{"step", step}};
    });
    check(calls == 6 && resumed.size() == 8 && manager.get_resume_point("loop-1") == "report_second"
        && resumed.at("report_second").at("step") == "report_second",
        "workflow failed to skip saved steps and automatically checkpoint the second hypothesis run");
    manager.clear("loop-1");
    check(manager.list_checkpoints("loop-1").empty() && manager.next_step("loop-1", workflow) == "literature",
        "clear did not remove loop checkpoints");
}

void rejects_path_traversal() {
    const auto root = std::filesystem::temp_directory_path() / ("ark-sage-checkpoints-" + ark::random_uuid());
    const ark::CheckpointManager manager(root);
    bool rejected = false;
    try { manager.save("../outside", "step", {{"x", 1}}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && !std::filesystem::exists(root), "unsafe loop ID escaped or created checkpoint storage");
}
}

int main() {
    try { saves_resumes_lists_and_clears(); rejects_path_traversal(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
