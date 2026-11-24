#include "ark/capability_discovery.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void answers_ten_questions(const char* questions_path) {
    std::ifstream file(questions_path);
    const auto suite = Json::parse(file);
    Json registry = Json::array();
    for (const auto& item : suite.at("questions")) {
        const auto cap = item.at("capability").get<std::string>();
        registry.push_back({{"name", "agent_" + cap}, {"capabilities", Json::array({cap})},
            {"avg_quality_score", 0.8}, {"load_count", 3}, {"version", "1.0.0"}});
    }
    Json histories = Json::object();
    for (const auto& item : suite.at("questions")) {
        const auto cap = item.at("capability").get<std::string>();
        histories["agent_" + cap] = Json::array({{{"score", 0.9}, {"samples", 10}}});
    }
    const auto map = ark::build_capability_map(registry, Json::array(), histories);
    for (const auto& item : suite.at("questions")) {
        const auto answer = ark::answer_capability_question(map, item.at("question").get<std::string>());
        check(answer.at("can_do") == true, "expected capability was not detected");
        check(answer.at("capability") == item.at("capability"), "question mapped to the wrong capability");
        check(answer.at("confidence").get<double>() >= 0.8 && answer.at("confidence").get<double>() <= 0.99,
              "confidence ignored strong registry and benchmark evidence");
    }
    check(suite.at("questions").size() == 10, "verification suite must have ten questions");
    const auto unknown = ark::answer_capability_question(map, "Can ARK OS cook dinner?");
    check(unknown.at("can_do") == false && unknown.at("confidence") == 0.0,
          "unknown capabilities must remain unsupported with zero confidence");
}

void reflects_failures_in_confidence() {
    const Json registry = Json::array({{{"name", "research_agent"}, {"capabilities", {"RESEARCH"}},
        {"avg_quality_score", 0.8}}});
    const Json failure = Json::array({{{"agent_name", "research_agent"}, {"quality_score", 0.2},
        {"metadata", {{"reflection", {{"flag_for_self_improvement", true}}}}}}});
    const auto with_failure = ark::build_capability_map(registry, failure, Json::object());
    const auto clean = ark::build_capability_map(registry, Json::array(), Json::object());
    const auto a = with_failure.at("capabilities").at("RESEARCH");
    const auto b = clean.at("capabilities").at("RESEARCH");
    check(a.at("reflection_failures") == 1 && a.at("confidence") < b.at("confidence"),
          "reflection failure was not included in confidence");
}
}

int main(int argc, char** argv) {
    try { if (argc != 2) throw std::runtime_error("missing question suite"); answers_ten_questions(argv[1]); reflects_failures_in_confidence(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
