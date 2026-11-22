#include "ark/benchmark.hpp"
#include "ark/research.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

Json fixture_result(const Json& task, double score = 0.86) {
    const bool coding = task.at("kind") == "coding";
    const std::string agent = coding ? "coding_agent" : "research_agent";
    auto output = Json::object();
    if (coding) {
        output = {{"test_result", {{"exit_code", 0}, {"timed_out", false}}},
                  {"files", {{"solution.py", "generated fixture"}}}};
    } else {
        output["citations"] = Json::array();
        for (const auto& paper : task.at("required_arxiv_ids")) {
            output["citations"].push_back({{"arxiv_id", paper.get<std::string>() + "v2"}});
        }
    }
    return {{"task_id", "runtime-id"}, {"status", score < 0.65 ? "FAILED" : "DONE"},
            {"agent_versions", {{agent, "1.3.0"}, {"evaluation_agent", "1.4.0"}}},
            {"results", {{"produce", {{"status", "DONE"}, {"agent_version", agent + "@1.3.0"}, {"output", output}}},
                         {"evaluate", {{"quality_score", score}, {"output", {{"scores", {{"fixture", score}}},
                            {"evaluated_task", "produce"}, {"summary", "Fixture evaluation"}}}}}}}};
}

void records_all_ten_cases(const Json& suite) {
    const auto& tasks = suite.at("tasks");
    check(suite.at("version") == "0.1" && tasks.size() == 10, "unexpected dataset size or version");
    const auto coding = std::count_if(tasks.begin(), tasks.end(), [](const auto& task) { return task.at("kind") == "coding"; });
    check(coding == 5, "expected five coding tasks and five research tasks");
    std::size_t current = 0;
    int holdouts = 0;
    int snapshots = 0;
    const auto report = ark::run_benchmark(suite, [&](const Json& dag) {
        const auto& task = tasks.at(current++);
        check(dag.at("goal") == task.at("goal"), "benchmark reordered a task");
        check(dag.at("tasks").at(1).at("depends_on") == Json::array({"produce"}), "evaluator has the wrong dependency");
        if (task.at("kind") == "coding") {
            check(dag.at("coding_test_files") == task.at("test_files") && !dag.contains("acceptance_tests"),
                  "holdout tests were sent to the agent");
        } else {
            const auto instruction = dag.at("tasks").at(0).at("instruction").get<std::string>();
            for (const auto& paper : task.at("required_arxiv_ids")) {
                check(instruction.find("arXiv:" + paper.get<std::string>()) != std::string::npos,
                      "a required source was omitted from the prompt");
            }
        }
        return fixture_result(task);
    }, [&](const ark::SandboxWorkspace& workspace) {
        ++holdouts;
        check(workspace.read_file("solution.py") == "generated fixture", "wrong solution passed to holdout");
        check(workspace.files().contains("test_acceptance.py") && !workspace.files().contains("test_smoke.py"),
              "holdout workspace must contain the separate acceptance suite");
        return ark::SandboxResult{0, false, "", ""};
    }, [&](const Json& partial) {
        check(partial.at("results").size() == static_cast<std::size_t>(++snapshots), "progress reported incomplete rows");
    });
    check(holdouts == 5 && snapshots == 10 && report.at("summary").at("scored") == 10, report.dump());
    check(std::abs(report.at("summary").at("mean_score").get<double>() - 0.86) < 1e-9, "incorrect aggregate score");
    for (const auto& row : report.at("results")) {
        const auto agent = row.at("kind") == "coding" ? "coding_agent" : "research_agent";
        check(row.at("agent_version") == std::string(agent) + "@1.3.0" && row.at("score") == 0.86
              && row.at("agent_versions").at("evaluation_agent") == "1.4.0"
              && row.at("duration").get<double>() >= 0.0 && row.at("notes") == "Fixture evaluation", row.dump());
    }
}

void distinguishes_measured_failures_from_unavailable_scores(const Json& suite) {
    Json partial{{"version", "fixture"}, {"tasks", Json::array({suite.at("tasks").at(2), suite.at("tasks").at(5),
        suite.at("tasks").at(6), suite.at("tasks").at(7), suite.at("tasks").at(8)})}};
    int current = 0;
    int holdouts = 0;
    const auto report = ark::run_benchmark(partial, [&](const Json&) {
        const auto index = current++;
        auto result = fixture_result(partial.at("tasks").at(index), index == 4 ? 0.50 : 0.86);
        auto& outcomes = result.at("results");
        if (index == 0) outcomes["produce"]["output"]["citations"].erase(1);
        if (index == 1) outcomes["produce"]["output"]["test_result"]["exit_code"] = 1;
        if (index == 3) outcomes["evaluate"]["output"] = {{"error_message", "Model unavailable"}};
        return result;
    }, [&](const ark::SandboxWorkspace&) { return ark::SandboxResult{holdouts++ == 0 ? 1 : 0, false, "", ""}; });
    const auto& rows = report.at("results");
    for (int index = 0; index < 3; ++index) {
        check(rows.at(index).at("score") == 0.0 && rows.at(index).at("status") == "FAILED", rows.at(index).dump());
    }
    check(rows.at(3).at("score").is_null() && rows.at(3).at("notes") == "Model unavailable", rows.at(3).dump());
    check(rows.at(4).at("score") == 0.50 && rows.at(4).at("status") == "FAILED", rows.at(4).dump());
    check(report.at("summary").at("scored") == 4 && holdouts == 3, report.dump());
}

void validates_before_running(const Json& suite) {
    auto duplicate = suite;
    duplicate.at("tasks").push_back(suite.at("tasks").at(0));
    bool called = false;
    bool rejected = false;
    try {
        (void)ark::run_benchmark(duplicate, [&](const Json&) { called = true; return nullptr; },
            [](const ark::SandboxWorkspace&) { return ark::SandboxResult{0, false, "", ""}; });
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected && !called, "invalid dataset started running or was accepted");
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected the benchmark dataset path");
        const auto suite = ark::read_json(std::filesystem::path(argv[1]));
        records_all_ten_cases(suite);
        distinguishes_measured_failures_from_unavailable_scores(suite);
        validates_before_running(suite);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
