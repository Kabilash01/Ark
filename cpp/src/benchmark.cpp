#include "ark/benchmark.hpp"
#include "ark/research.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ark {
namespace {

constexpr auto producer_id = "produce";
constexpr auto judge_id = "evaluate";

void validate_tests(const Json& files, std::string_view id, std::string_view field) {
    static const std::regex name(R"(test_[a-zA-Z0-9_]+\.py)");
    if (!files.is_object() || files.empty()) {
        throw std::invalid_argument(std::string(id) + " requires nonempty " + std::string(field));
    }
    for (const auto& [filename, content] : files.items()) {
        if (!std::regex_match(filename, name) || !content.is_string()
            || content.get_ref<const std::string&>().find_first_not_of(" \t\r\n") == std::string::npos) {
            throw std::invalid_argument(std::string(id) + " has invalid test file " + filename);
        }
    }
}

void validate_suite(const Json& suite) {
    static const std::regex arxiv(R"((?:[0-9]{4}\.[0-9]{4,5}|[a-zA-Z.-]+/[0-9]{7}))");
    const auto& tasks = suite.at("tasks");
    if (suite.at("version").get<std::string>().empty() || !tasks.is_array() || tasks.empty()) {
        throw std::invalid_argument("Benchmark requires a version and a nonempty tasks list");
    }
    std::set<std::string> ids;
    for (const auto& task : tasks) {
        const auto id = task.at("task_id").get<std::string>();
        const auto kind = task.at("kind").get<std::string>();
        if (id.find_first_not_of(" \t\r\n") == std::string::npos || !ids.insert(id).second) {
            throw std::invalid_argument("Benchmark task IDs must be nonblank and unique");
        }
        const auto& goal = task.at("goal").get_ref<const std::string&>();
        if (goal.find_first_not_of(" \t\r\n") == std::string::npos || goal.size() > 8192) {
            throw std::invalid_argument(id + " requires a goal of 1 to 8192 bytes");
        }
        if (kind == "coding") {
            const auto& smoke = task.at("test_files");
            const auto& holdout = task.at("acceptance_tests");
            validate_tests(smoke, id, "test_files");
            validate_tests(holdout, id, "acceptance_tests");
            for (const auto& [name, content] : holdout.items()) {
                if (smoke.contains(name)) {
                    throw std::invalid_argument(id + " must use separate smoke and acceptance filenames");
                }
            }
        } else if (kind == "research") {
            const auto papers = task.at("required_arxiv_ids").get<std::vector<std::string>>();
            std::set<std::string> unique;
            if (papers.empty() || papers.size() > 5) {
                throw std::invalid_argument(id + " requires between one and five sources");
            }
            for (const auto& paper : papers) {
                if (!std::regex_match(paper, arxiv) || !unique.insert(paper).second) {
                    throw std::invalid_argument(id + " requires unique base arXiv IDs");
                }
            }
        } else {
            throw std::invalid_argument(id + " must be a research or coding task");
        }
    }
}

Json plan(const Json& task) {
    const bool coding = task.at("kind") == "coding";
    const auto& goal = task.at("goal").get_ref<const std::string&>();
    auto instruction = goal;
    if (!coding) {
        instruction += "\nUse these paper abstracts as sources and cite each in your findings.";
        for (const auto& paper : task.at("required_arxiv_ids")) {
            instruction += " arXiv:" + paper.get<std::string>();
        }
    }
    Json dag{{"goal", goal}, {"tasks", Json::array({
        {{"task_id", producer_id}, {"agent", coding ? "coding_agent" : "research_agent"},
         {"intent", coding ? "CODE" : "RESEARCH"}, {"instruction", instruction},
         {"depends_on", Json::array()}, {"parallel_with", Json::array()}},
        {{"task_id", judge_id}, {"agent", "evaluation_agent"}, {"intent", "EVALUATE"},
         {"instruction", coding ? goal : instruction}, {"depends_on", Json::array({producer_id})},
         {"parallel_with", Json::array()}},
    })}};
    if (coding) {
        dag["coding_test_files"] = task.at("test_files");
    }
    return dag;
}

bool passes(const Json& test) {
    return test.at("exit_code") == 0 && test.at("timed_out") == false;
}

bool meets_coding_contract(const Json& task, const Json& output, Json& evidence, const TestRunner& run_tests) {
    evidence["smoke_tests_passed"] = passes(output.at("test_result"));
    if (evidence.at("smoke_tests_passed") != true) {
        return false;
    }
    SandboxWorkspace workspace;
    workspace.write_file("solution.py", output.at("files").at("solution.py").get<std::string>());
    for (const auto& [name, content] : task.at("acceptance_tests").items()) {
        workspace.write_file(name, content.get<std::string>());
    }
    const auto result = run_tests(workspace);
    evidence["acceptance_tests"] = {{"exit_code", result.exit_code}, {"timed_out", result.timed_out}};
    return passes(evidence.at("acceptance_tests"));
}

bool meets_research_contract(const Json& task, const Json& output, Json& evidence) {
    auto citations = Json::array();
    std::set<std::string> cited_bases;
    for (const auto& citation : output.at("citations")) {
        const auto id = citation.at("arxiv_id").get<std::string>();
        citations.push_back(id);
        cited_bases.insert(id.substr(0, id.find('v', id.find_last_of("./") + 1)));
    }
    auto missing = Json::array();
    for (const auto& paper : task.at("required_arxiv_ids")) {
        if (!cited_bases.contains(paper.get<std::string>())) {
            missing.push_back(paper);
        }
    }
    evidence["citations"] = std::move(citations);
    evidence["missing_arxiv_ids"] = std::move(missing);
    return evidence.at("missing_arxiv_ids").empty();
}

void score_case(const Json& task, const Json& run, Json& row, const TestRunner& run_tests) {
    row["run_task_id"] = run.value("task_id", Json(nullptr));
    row["agent_versions"] = run.value("agent_versions", Json::object());
    const auto& results = run.at("results");
    const auto producer = results.value(producer_id, Json::object());
    row["agent_version"] = producer.value("agent_version", Json(nullptr));
    const auto output = producer.value("output", Json::object());
    if (!row.at("agent_version").is_string() || row.at("agent_version").get_ref<const std::string&>().empty()) {
        row["notes"] = "Producer did not return an agent version";
        return;
    }
    auto& evidence = row["evidence"];
    const bool coding = task.at("kind") == "coding";
    if (coding && output.contains("test_result")) {
        if (!meets_coding_contract(task, output, evidence, run_tests)) {
            row["score"] = 0.0;
            row["notes"] = evidence.at("smoke_tests_passed") == true
                ? "Solution failed the separate Docker acceptance tests" : "Solution failed the Docker smoke tests";
            return;
        }
    } else if (!coding && producer.value("status", "") == "DONE" && output.contains("citations")) {
        if (!meets_research_contract(task, output, evidence)) {
            row["score"] = 0.0;
            row["notes"] = "Research findings omitted one or more required arXiv citations";
            return;
        }
    } else {
        row["notes"] = output.value("error_message", "Producer did not return benchmark evidence");
        return;
    }
    const auto judge = results.value(judge_id, Json::object());
    const auto assessment = judge.value("output", Json::object());
    const auto score = judge.value("quality_score", Json(nullptr));
    if (!assessment.contains("scores") || !assessment.at("scores").is_object()
        || assessment.value("evaluated_task", "") != producer_id || !score.is_number()
        || !std::isfinite(score.get<double>()) || score.get<double>() < 0 || score.get<double>() > 1) {
        row["notes"] = assessment.value("error_message", "Evaluation did not produce a measured score");
        return;
    }
    row["score"] = score;
    row["status"] = run.at("status");
    row["notes"] = assessment.value("summary", "Evaluation completed");
}

Json summary(const Json& rows, std::size_t total) {
    std::size_t scored = 0;
    std::size_t completed = 0;
    double sum = 0;
    for (const auto& row : rows) {
        completed += row.at("status") == "DONE";
        if (row.at("score").is_number()) {
            ++scored;
            sum += row.at("score").get<double>();
        }
    }
    return {{"total", total}, {"scored", scored}, {"completed", completed},
            {"mean_score", scored ? Json(sum / static_cast<double>(scored)) : Json(nullptr)}};
}

} // namespace

Json run_benchmark(const Json& suite, const BenchmarkDispatch& dispatch, const TestRunner& run_tests,
                   const BenchmarkProgress& progress) {
    validate_suite(suite);
    if (!dispatch || !run_tests) {
        throw std::invalid_argument("Benchmark requires a dispatcher and an acceptance test runner");
    }
    const auto& tasks = suite.at("tasks");
    Json report{{"benchmark", "ARKBench"}, {"dataset_version", suite.at("version")},
                {"duration_unit", "seconds"}, {"started_at", utc_timestamp()}, {"results", Json::array()}};
    auto& rows = report.at("results");
    for (const auto& task : tasks) {
        const auto start = std::chrono::steady_clock::now();
        Json row{{"task_id", task.at("task_id")}, {"kind", task.at("kind")}, {"status", "FAILED"},
                 {"score", nullptr}, {"agent_version", nullptr}, {"agent_versions", Json::object()}, {"evidence", Json::object()}};
        try {
            score_case(task, dispatch(plan(task)), row, run_tests);
        } catch (const std::exception& error) {
            row["notes"] = error.what();
        }
        row["duration"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        rows.push_back(std::move(row));
        report["summary"] = summary(rows, tasks.size());
        if (progress) {
            progress(report);
        }
    }
    report["finished_at"] = utc_timestamp();
    return report;
}

Json benchmark_history(const std::filesystem::path& directory, std::string_view agent) {
    auto history = Json::array();
    if (!std::filesystem::is_directory(directory)) return history;
    const auto prefix = std::string(agent) + "@";
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (!file.is_regular_file() || file.path().extension() != ".json") continue;
        try {
            const auto report = read_json(file.path());
            if (report.value("benchmark", "") != "ARKBench" || !report.contains("finished_at")) continue;
            std::map<std::string, std::pair<double, int>> groups;
            for (const auto& row : report.at("results")) {
                const auto version = row.value("agent_version", Json(nullptr));
                const auto score = row.value("score", Json(nullptr));
                if (!version.is_string() || !version.get_ref<const std::string&>().starts_with(prefix) || !score.is_number()) continue;
                const auto value = score.get<double>();
                if (!std::isfinite(value) || value < 0 || value > 1) continue;
                auto& [sum, n] = groups[version.get_ref<const std::string&>().substr(prefix.size())];
                sum += value;
                ++n;
            }
            for (const auto& [version, totals] : groups) {
                history.push_back({{"version", version}, {"score", totals.first / totals.second}, {"samples", totals.second},
                    {"time", report.at("finished_at")}, {"dataset", report.at("dataset_version")}});
            }
        } catch (const std::exception&) {
            // Another producer may still be writing this report.
        }
    }
    std::sort(history.begin(), history.end(), [](const Json& left, const Json& right) {
        return std::tie(left.at("time"), left.at("dataset"), left.at("version"))
             < std::tie(right.at("time"), right.at("dataset"), right.at("version"));
    });
    return history;
}

} // namespace ark
