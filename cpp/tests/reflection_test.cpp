#include "ark/reflection.hpp"
#include "ark/vector_memory.hpp"

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

void persists_reflection_skills_and_flags() {
    const auto path = std::filesystem::temp_directory_path() / ark::random_uuid();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{path};
    ark::EpisodicMemory episodes(path / "episodes.db");
    ark::Embeddings embeddings([](const ark::Request& request) {
        check(request.url.ends_with("/api/embed"), "invalid embedding endpoint");
        return ark::Response{200, R"({"embeddings":[[0.3,0.4]]})"};
    }, "http://localhost:11434", "embedding-fixture");
    ark::ProceduralMemory skills(path / "skills.db", std::move(embeddings));
    Json reply{{"what_worked", "Fixed tests verified the artifact"}, {"what_failed", "First attempt timed out"},
               {"pattern_learned", "Run fixed tests"}, {"skill_update", {{"description", "Run the contract suite"},
                   {"steps", Json::array({"Preserve the test suite", "Run the tests"})}}},
               {"flag_for_self_improvement", true}, {"agent_version_recommendation", {
                   {"agent_name", "coding_agent"}, {"reason", "Timed out on first attempt"}, {"version", "invented"}}}};
    bool received_conflict = false;
    bool received_unscored = false;
    ark::ReflectionAgent agent([&](const ark::Request& request) {
        const auto body = Json::parse(request.body);
        check(body.at("model") == "local-reflection", "incorrect reflection model");
        check(body.at("format").at("required").size() == 6
                  && body.at("format").at("properties").contains("what_worked"),
              "reflection output schema was not sent to the model");
        const auto evidence = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
        check(evidence.at("attempts").at(0).at("status") == "RETRY"
                  || evidence.at("attempts").at(0).at("status") == "DONE",
              "attempt omitted from reflection evidence");
        if (evidence.at("status") == "DONE" && evidence.at("quality_score").is_null()) received_unscored = true;
        received_conflict = evidence.contains("memory_conflicts_pending_human_review");
        return ark::Response{200, Json{{"message", {{"content", reply.dump()}}}}.dump()};
    }, "http://localhost:11434", ark::reflection_profile("local-reflection"));
    Json task{{"task_id", "source-task"}, {"intent", "CODE"}, {"status", "DONE"}, {"quality_score", 0.9},
              {"attempts", Json::array({{{"status", "RETRY"}}})}, {"agent_versions", {{"coding_agent", "1.2.0"}}}};
    const auto report = agent.run(task, episodes, skills);
    check(report.at("status") == "DONE", report.dump());
    const auto skill = skills.get(report.at("skill_id").get<std::string>());
    check(skill.at("source") == "reflection_agent" && skill.at("intent") == "CODE", skill.dump());
    ark::EpisodicMemory reopened(path / "episodes.db");
    const auto pending = reopened.pending_improvements();
    check(pending.size() == 1 && pending.at(0).at("task_id") == "source-task", pending.dump());
    const auto queued = pending.at(0).at("metadata").at("reflection");
    check(queued.at("agent_version_recommendation").at("version") == "1.2.0", queued.dump());

    reply["flag_for_self_improvement"] = false;
    task["status"] = "FAILED";
    task["quality_score"] = nullptr;
    check(!agent.run(task, episodes, skills).contains("skill_id") && skills.all().size() == 1,
          "failed task incorrectly added a skill");
    reply.erase("skill_update");
    reply.erase("flag_for_self_improvement");
    reply.erase("agent_version_recommendation");
    const auto sparse = agent.run(task, episodes, skills);
    check(sparse.at("status") == "DONE" && sparse.at("skill_update").is_null()
              && sparse.at("flag_for_self_improvement") == false,
          "missing optional reflection fields should mean no proposal");
    reply["flag_for_self_improvement"] = true;
    reply["agent_version_recommendation"] = nullptr;
    const auto incomplete = agent.run(task, episodes, skills);
    check(incomplete.at("status") == "DONE" && incomplete.at("flag_for_self_improvement") == false
              && incomplete.at("agent_version_recommendation").is_null(),
          "incomplete model recommendation failed the whole reflection");
    reply.erase("flag_for_self_improvement");
    reply.erase("agent_version_recommendation");
    task["escalations"] = Json::array({{{"agent", "research_agent"}, {"detail", "Ollama returned HTTP 400"}}});
    const auto failed = agent.run(task, episodes, skills);
    check(failed.at("status") == "DONE" && failed.at("what_failed") == "research_agent: Ollama returned HTTP 400"
              && failed.at("what_worked") == "No completed worker step recorded.",
          "failed task reflection omitted the recorded worker error");
    task.erase("escalations");
    task["status"] = "DONE";
    task["quality_score"] = -1.0;
    task["attempts"] = Json::array({{{"status", "DONE"}}});
    const auto unscored = agent.run(task, episodes, skills);
    check(received_unscored && unscored.at("status") == "DONE" && !unscored.contains("skill_id")
              && unscored.at("what_failed") == "No failure recorded.",
          "unscored task was treated as a failed or accepted scored task");
    task["status"] = "FAILED";
    task["quality_score"] = nullptr;
    skills.add("CODE", "Run the contract suite", {"Run a sampled test"}, "coding_agent");
    reply["what_worked"] = 5;
    const auto invalid = agent.run(task, episodes, skills);
    check(invalid.at("status") == "FAILED" && reopened.pending_improvements().size() == 1,
          "invalid output should be logged as a failed reflection");
    check(received_conflict && invalid.at("memory_conflicts_pending_human_review").size() == 1,
          "Reflection did not receive the durable memory conflict");
    check(reopened.by_agent("reflection_agent").size() == 7, "missing reflection episodes");
}

} // namespace

int main() {
    try {
        persists_reflection_skills_and_flags();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
