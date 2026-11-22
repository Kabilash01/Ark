#include "ark/done_condition.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void evaluates_objective_conditions() {
    const Json metrics{{"quality_score", 0.84}, {"all_tests_pass", true}, {"citation_count", 2}};
    check(ark::DoneCondition("all_tests_pass AND quality_score >= .84").evaluate(metrics), "rejected measured success");
    check(ark::DoneCondition("citation_count == 3 OR (quality_score > 0.8 AND NOT false)").evaluate(metrics),
          "operator precedence or parentheses changed");
    check(!ark::DoneCondition("all_tests_pass AND coverage >= 80").evaluate(metrics), "missing coverage satisfied goal");
    check(!ark::DoneCondition("NOT (security_issues != 0)").evaluate(metrics), "missing security evidence was inverted");
    check(ark::DoneCondition("quality_score > .8 OR coverage > 90").evaluate(metrics), "measured alternative was ignored");
    for (const auto* invalid : {"", "unknown > 0", "load('file')", "quality_score > 1 OR false trailing", "(true"}) {
        bool rejected = false;
        try { (void)ark::DoneCondition(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "invalid condition was accepted");
    }
}

void uses_verified_worker_evidence() {
    const Json result{{"quality_score", 0.83}, {"results", {
        {"code", {{"output", {{"passed", true}, {"repairs", 1}, {"test_result", {{"exit_code", 0}}}}}}},
        {"research", {{"output", {{"citations", Json::array({{{"url", "https://example.org"}}, {{"url", "https://example.org"}}})}}}}},
    }}};
    const auto metrics = ark::measured_metrics(result);
    check(metrics.at("all_tests_pass") == true && metrics.at("citation_count") == 1
          && metrics.at("self_repair_rate") == 1.0 && !metrics.contains("coverage"), metrics.dump());
}

} // namespace

int main() {
    try {
        evaluates_objective_conditions();
        uses_verified_worker_evidence();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
