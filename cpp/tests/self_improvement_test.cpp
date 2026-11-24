#include "ark/self_improvement.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

Json logs() {
    return Json::array({
        {{"quality_score", 0.42}, {"intent", "RESEARCH"}, {"summary", "search missed key evidence"},
            {"metadata", {{"reflection", {{"flag_for_self_improvement", true},
                {"agent_version_recommendation", {{"agent_name", "research_agent"}, {"reason", "Missed cited paper"}, {"version", "1.0.0"}}},
                {"what_failed", "The summary omitted relevant sources."}, {"pattern_learned", "Query was too broad."}}}}}},
        {{"quality_score", 0.65}, {"metadata", {{"reflection", {{"flag_for_self_improvement", true},
            {"agent_version_recommendation", {{"agent_name", "research_agent"}}}}}}}},
        {{"quality_score", 0.20}, {"metadata", {{"reflection", {{"flag_for_self_improvement", true},
            {"agent_version_recommendation", {{"agent_name", "coding_agent"}}}}}}}}
    });
}

void proposes_from_target_failures_only() {
    const std::string candidate(150, 'x');
    bool called = false;
    const auto transport = [&](const ark::Request& request) {
        called = true;
        const auto body = Json::parse(request.body);
        const auto user = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
        check(user.at("reflection_failures").size() == 1
            && user.at("reflection_failures").at(0).at("reason") == "Missed cited paper",
            "proposal included unrelated or non-failing reflection logs");
        return ark::Response{200, Json{{"message", {{"content", Json{{"system_prompt", candidate},
            {"rationale", "Narrow search terms and check retrieved citations."}}.dump()}}}, {"done_reason", "stop"}}.dump()};
    };
    const ark::SelfImprovementAgent agent(transport, "http://127.0.0.1:11434", "fixture-model");
    const ark::AgentProfile current{"1.0.0", "research-model", {{"system", "Existing research prompt."}}};
    const auto result = agent.propose("research_agent", current, logs());
    check(called && result.at("status") == "CANDIDATE_READY" && result.at("failure_runs") == 1
        && result.at("required_benchmark_cases") == 10 && result.at("candidate_system_prompt") == candidate,
        "self-improvement proposal was not generated from filtered failures");
}

void compares_ten_cases_and_promotes_only_above_margin() {
    const ark::SelfImprovementAgent agent([](const ark::Request&) -> ark::Response { throw std::runtime_error("proposal not used"); },
        "http://127.0.0.1:11434", "fixture-model");
    const ark::AgentProfile baseline{"1.0.0", "model", {{"system", "baseline prompt"}}};
    const Json tasks = Json::array({1,2,3,4,5,6,7,8,9,10});
    int calls = 0;
    const auto comparison = agent.compare(baseline, "candidate prompt", tasks, [&](const auto& profile, const auto&) {
        ++calls; return profile.prompts.at("system") == "baseline prompt" ? 0.60 : 0.70;
    });
    check(calls == 20 && comparison.at("case_ids").size() == 10 && comparison.at("promotion_eligible") == true
        && std::abs(comparison.at("score_gain").get<double>() - 0.1) < 1e-9,
        "A/B comparison did not score ten cases for both prompts");

    bool committed = false;
    const auto promoted = agent.promote_if_better("research_agent", baseline, "candidate prompt", comparison,
        [&](const auto& profile, double score) {
            committed = true;
            check(profile.prompts.at("system") == "candidate prompt" && std::abs(score - 0.70) < 1e-9,
                "version commit received the wrong prompt or score");
            return Json{{"version", "1.0.1"}};
        });
    check(committed && promoted.at("status") == "PROMOTED" && promoted.at("version").at("version") == "1.0.1",
        "qualifying prompt was not promoted");

    auto weak = comparison; weak["candidate_mean"] = 0.65;
    committed = false;
    const auto rejected = agent.promote_if_better("research_agent", baseline, "candidate prompt", weak,
        [&](const auto&, double) { committed = true; return Json::object(); });
    check(!committed && rejected.at("status") == "REJECTED", "candidate at the 0.05 margin was incorrectly promoted");
}
}

int main() {
    try { proposes_from_target_failures_only(); compares_ten_cases_and_promotes_only_above_margin(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
