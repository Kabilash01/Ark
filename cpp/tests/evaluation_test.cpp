#include "ark/evaluation.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

Json evaluation_message(std::string_view type) {
    return ark::make_message("user", "evaluation_agent", "EVALUATE",
                             {{"output_type", type}, {"artifact", "Fixture artifact"}});
}

ark::Response judgment(const Json& scores) {
    return {200, Json{{"message", {{"content", Json{{"scores", scores}, {"feedback", "Fixture"}}.dump()}}}}.dump()};
}

Json request_input(const ark::Request& request) {
    return Json::parse(Json::parse(request.body).at("messages").at(1).at("content").get<std::string>());
}

void applies_each_rubric() {
    const std::vector<std::tuple<std::string, std::string, double>> fixtures{
        {"research", "citation_accuracy", 0.4}, {"code", "tests_pass", 0.5},
        {"security", "cve_recall", 0.5}, {"hypothesis", "novelty", 0.5},
        {"data", "statistical_validity", 0.5}, {"general", "correctness", 0.5},
    };
    for (const auto& [type, dimension, expected] : fixtures) {
        ark::EvaluationAgent evaluator([&](const ark::Request& request) {
            const auto input = request_input(request);
            check(input.at("type") == type, "incorrect rubric selected");
            auto scores = Json::object();
            for (const auto& criterion : input.at("criteria")) {
                const auto name = criterion.at("name").get<std::string>();
                scores[name] = name == dimension ? 1.0 : 0.0;
            }
            return judgment(scores);
        }, "http://localhost:11434", "test-judge");
        const auto result = evaluator.run(evaluation_message(type));
        check(result.at("quality_score") == expected && result.at("status") == "RETRY", result.dump());
    }
}

void decisions_and_invalid_scores() {
    double grade = 0.65;
    ark::EvaluationAgent evaluator([&](const ark::Request& request) {
        auto scores = Json::object();
        for (const auto& criterion : request_input(request).at("criteria").get<std::vector<Json>>()) {
            scores[criterion.at("name").get<std::string>()] = grade;
        }
        return judgment(scores);
    }, "http://localhost:11434", "test-judge");
    auto result = evaluator.run(evaluation_message("general"));
    check(result.at("output").at("decision") == "ACCEPT_WITH_WARNING", result.dump());
    grade = 0.8;
    result = evaluator.run(evaluation_message("general"));
    check(result.at("output").at("decision") == "ACCEPT", result.dump());
    grade = 1.2;
    result = evaluator.run(evaluation_message("general"));
    check(result.at("status") == "FAILED", "out-of-range judge score was accepted");
}

void trusts_docker_results_for_code() {
    ark::EvaluationAgent evaluator([](const ark::Request& request) {
        const auto input = request_input(request);
        check(input.at("criteria").size() == 2, "judge should only score style and security after real tests");
        return judgment({{"tests_pass", 0.0}, {"syntax", 0.0}, {"style", 0.5}, {"security", 0.5}});
    }, "http://localhost:11434", "test-judge");
    auto message = ark::make_message("router", "evaluation_agent", "EVALUATE", {{"instruction", "Score code"}});
    const Json coding{{"status", "DONE"}, {"intent", "CODE"}, {"output", {
        {"passed", true}, {"test_result", {{"exit_code", 0}, {"timed_out", false}}},
    }}};
    message["memory_context"] = {{{"tier", "dependencies"}, {"data", {{"t1", coding}}}}};
    const auto result = evaluator.run(message);
    check(result.at("quality_score") == 0.85 && result.at("output").at("scores").at("tests_pass") == 1.0, result.dump());
}

} // namespace

int main() {
    try {
        applies_each_rubric();
        decisions_and_invalid_scores();
        trusts_docker_results_for_code();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
