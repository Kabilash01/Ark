#include "ark/reflection_study.hpp"
#include "ark/storage.hpp"

#include <set>

namespace ark {
namespace {

void add_files(SandboxWorkspace& workspace, const Json& files) {
    for (const auto& [name, source] : files.items()) {
        workspace.write_file(name, source.get_ref<const std::string&>());
    }
}

Json trial(const Json& item, bool assisted, const std::string& guidance, const CodingAgent& agent,
           const DockerSandbox& sandbox) {
    Json request{{"instruction", item.at("goal")}, {"test_files", item.at("smoke")}};
    if (assisted) request["review_feedback"] = guidance;
    auto input = make_message("study", "coding_agent", "CODE", std::move(request));
    input["max_retries"] = 0;
    const auto answer = agent.run(std::move(input));
    const auto& output = answer.at("output");
    Json row{{"agent_version", answer.at("agent_version")}, {"smoke_passed", answer.at("status") == "DONE"},
             {"accepted", false}};
    if (answer.at("status") == "DONE") {
        SandboxWorkspace holdout;
        add_files(holdout, output.at("files"));
        add_files(holdout, item.at("holdout"));
        const auto test = sandbox.run_tests(holdout);
        row["accepted"] = test.exit_code == 0 && !test.timed_out;
        row["holdout_exit_code"] = test.exit_code;
        row["holdout_timed_out"] = test.timed_out;
    }
    if (output.contains("error_message")) row["error"] = output.at("error_message");
    return row;
}

} // namespace

Json run_reflection_study(const Json& suite, const CodingAgent& coder, const ReflectionAgent& reflector,
                          const DockerSandbox& sandbox, Embeddings encoder,
                          const std::function<void(const Json&)>& snapshot) {
    const auto root = std::filesystem::temp_directory_path() / ("ark-reflection-study-" + random_uuid());
    if (!std::filesystem::create_directory(root)) throw std::runtime_error("Could not create reflection study directory");
    struct Cleanup {
        const std::filesystem::path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};

    EpisodicMemory episodes(root / "episodes.db");
    ProceduralMemory skills(root / "skills.db", std::move(encoder));
    auto guidance = Json::object();
    for (const auto& training : suite.at("training")) {
        const auto family = training.at("family").get<std::string>();
        if (guidance.contains(family)) throw std::invalid_argument("Duplicate reflection training family");
        SandboxWorkspace workspace;
        workspace.write_file("solution.py", training.at("broken_source").get<std::string>());
        add_files(workspace, training.at("tests"));
        const auto failure = sandbox.run_tests(workspace);
        if (failure.timed_out || failure.exit_code == 0) throw std::runtime_error("Training seed must fail its contract");
        const auto report = reflector.run({{"task_id", "study-train-" + family}, {"goal", training.at("goal")},
            {"intent", "CODE"}, {"status", "FAILED"}, {"quality_score", nullptr},
            {"agent_versions", {{"coding_agent", "1.0.0"}}},
            {"attempts", Json::array({{{"agent", "coding_agent"}, {"status", "FAILED"}, {"test_result", failure.json()}}})}},
            episodes, skills);
        if (report.at("status") != "DONE" || report.at("pattern_learned").get<std::string>().empty()) {
            throw std::runtime_error("Reflection training failed for " + family + ": " + report.dump());
        }
        guidance[family] = {{"agent_version", report.at("agent_version")}, {"pattern", report.at("pattern_learned")}};
    }

    const auto& tasks = suite.at("tasks");
    if (!tasks.is_array() || tasks.empty()) throw std::invalid_argument("Expected held-out reflection study tasks");
    Json report{{"study", suite.at("version")}, {"guidance", guidance}, {"pairs", Json::array()}, {"started_at", utc_timestamp()}};
    std::set<std::string> ids;
    int baseline = 0, assisted = 0, improved = 0, harmed = 0;
    for (const auto& item : tasks) {
        const auto id = item.at("id").get<std::string>();
        const auto family = item.at("family").get<std::string>();
        if (!ids.insert(id).second) throw std::invalid_argument("Duplicate held-out task ID");
        const auto& pattern = guidance.at(family).at("pattern").get_ref<const std::string&>();
        const bool reflected_first = ids.size() % 2 == 0;
        const auto first = trial(item, reflected_first, pattern, coder, sandbox);
        const auto second = trial(item, !reflected_first, pattern, coder, sandbox);
        const auto& control = reflected_first ? second : first;
        const auto& treatment = reflected_first ? first : second;
        const bool base_ok = control.at("accepted"), assisted_ok = treatment.at("accepted");
        baseline += base_ok;
        assisted += assisted_ok;
        improved += !base_ok && assisted_ok;
        harmed += base_ok && !assisted_ok;
        report["pairs"].push_back({{"id", id}, {"family", family}, {"reflected_first", reflected_first},
                                    {"baseline", control}, {"with_reflection", treatment}});
        report["summary"] = {{"pairs", ids.size()}, {"coding_runs", 2 * ids.size()}, {"baseline_passed", baseline},
                              {"with_reflection_passed", assisted}, {"improved", improved}, {"harmed", harmed}};
        if (snapshot) snapshot(report);
    }
    report["finished_at"] = utc_timestamp();
    return report;
}

} // namespace ark
