#include "ark/evaluation.hpp"
#include "ark/versions.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Fixture {
    ark::Redis redis;
    std::string name = "version-test-" + ark::random_uuid();
    ark::AgentVersioner versioner{redis};
    ark::AgentRegistry registry{redis};
    const Json capabilities = Json::array({"EVALUATE"});
    const ark::AgentProfile defaults{"1.0.0", "original-model", {{"system", "abc"}}};

    explicit Fixture(int port) : redis(port, 14) {}

    ~Fixture() {
        try {
            redis.command({"DEL", "registry:" + name, "registry:version_history:" + name,
                           "ark:agent_revisions:" + name, "ark:agent_version_refs:" + name});
        } catch (...) {
        }
    }

    ark::AgentProfile active() { return versioner.ensure(name, capabilities, defaults); }
};

void check_model_request(const ark::AgentProfile& profile, std::string_view expected_model,
                         std::string_view expected_prompt, std::string_view expected_version) {
    bool called = false;
    ark::EvaluationAgent evaluator([&](const ark::Request& request) {
        called = true;
        const auto body = Json::parse(request.body);
        check(body.at("model") == expected_model, "wrong model sent to Ollama");
        check(body.at("messages").at(0).at("content") == expected_prompt, "wrong prompt sent to Ollama");
        const Json judgment{{"scores", {{"correctness", 1.0}, {"completeness", 1.0}, {"clarity", 1.0}}},
                            {"feedback", "Fixture"}};
        return ark::Response{200, Json{{"message", {{"content", judgment.dump()}}}}.dump()};
    }, "http://localhost:11434", profile);
    const auto result = evaluator.run(ark::make_message("tester", "evaluation_agent", "EVALUATE",
                                      {{"output_type", "general"}, {"artifact", "Fixture"}}));
    check(called && result.at("status") == "DONE", result.dump());
    check(result.at("agent_version") == "evaluation_agent@" + std::string(expected_version),
          "wrong version recorded in ARKMessage");
}

void versions_and_rollback(int port) {
    Fixture fixture(port);
    auto& versioner = fixture.versioner;
    const auto& name = fixture.name;
    check(!versioner.initialized(name), "new agent already has a revision");
    check_model_request(fixture.active(), "original-model", "abc", "1.0.0");
    fixture.active();
    const auto baseline = versioner.history(name);
    check(baseline.size() == 1, "initialization duplicated the baseline");
    check(baseline.at(0).at("prompt_hash") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "baseline must hash the actual system prompt");
    check(!baseline.at(0).contains("prompts"), "public history exposed prompt contents");

    const auto patch = versioner.bump(name, "patch", {{"model", "new-model"},
        {"prompts", {{"system", "new instructions"}}}, {"benchmark_score", 0.62}, {"change_summary", "New instructions"}});
    check(patch.at("version") == "1.0.1" && patch.at("prompt_hash") != baseline.at(0).at("prompt_hash"),
          "patch must save a new prompt hash and version");
    check_model_request(fixture.active(), "new-model", "new instructions", "1.0.1");
    const auto minor = versioner.bump(name, "minor", {{"benchmark_score", 0.82}, {"change_summary", "Updated benchmark"}});
    const auto major = versioner.bump(name, "major", {{"change_summary", "Major version"}});
    check(minor.at("version") == "1.1.0" && major.at("version") == "2.0.0", "incorrect semantic version bump");
    const auto delta = versioner.compare(name, "1.0.1", "1.1.0").at("benchmark_score_delta").get<double>();
    check(std::abs(delta - 0.2) < 1e-12, "incorrect benchmark delta");

    versioner.rollback(name, "1.0.0");
    const auto record = fixture.registry.get(name);
    check(record.at("version") == "1.0.0" && record.at("model") == "original-model", "registry did not roll back");
    const auto events = fixture.redis.command({"LRANGE", "ark:audit_log", "-10", "-1"});
    bool logged = false;
    for (const auto& item : events) {
        const auto& serialized = item.get_ref<const std::string&>();
        const auto event = Json::parse(serialized);
        if (event.value("agent", "") == name && event.value("event", "") == "agent_version_rollback") {
            logged = event.at("previous_version") == "2.0.0" && event.at("version") == "1.0.0";
            fixture.redis.command({"LREM", "ark:audit_log", "1", serialized});
            break;
        }
    }
    check(logged, "rollback event missing from audit log");

    ark::Redis reopened(port, 14);
    ark::AgentVersioner restarted(reopened);
    check_model_request(restarted.ensure(name, fixture.capabilities, fixture.defaults), "original-model", "abc", "1.0.0");
    check(versioner.history(name).size() == 4, "rollback changed revision history");
    const auto after_rollback = versioner.bump(name, "patch", {{"change_summary", "Patch the active prompt"}});
    check(after_rollback.at("version") == "2.0.1" && after_rollback.at("previous_version") == "1.0.0",
          "bump after rollback reused a version or inherited the wrong revision");
    check_model_request(fixture.active(), "original-model", "abc", "2.0.1");
}

void preserves_legacy_history(int port) {
    Fixture fixture(port);
    fixture.registry.add(fixture.name, fixture.capabilities, "legacy-model", "1.0.0");
    check(fixture.registry.version(fixture.name, "1.2.0", 0.5, "Legacy change"), "legacy fixture failed");
    check(fixture.registry.version(fixture.name, "1.0.0", 0.4, "Legacy rollback"), "legacy rollback fixture failed");
    check(fixture.active().version == "1.2.1", "migration reused a legacy version");
    check(fixture.versioner.history(fixture.name).size() == 3, "migration discarded legacy history");
    try {
        fixture.versioner.rollback(fixture.name, "1.2.0");
    } catch (const std::invalid_argument&) {
        check(fixture.active().version == "1.2.1", "legacy rollback changed active version");
        return;
    }
    throw std::runtime_error("legacy version without a snapshot was activated");
}

} // namespace

int main() {
    try {
        if (const auto* port = std::getenv("ARK_TEST_REDIS_PORT")) {
            versions_and_rollback(std::stoi(port));
            preserves_legacy_history(std::stoi(port));
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
