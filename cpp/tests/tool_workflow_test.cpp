#include "ark/tool_benchmark.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        const ark::Json fixture{{"task_id", "mixed-fixture"}, {"kind", "mixed"},
            {"fixture", {{"max_workers", 2}, {"stages", ark::Json::array({
                {{"task_id", "audit"}, {"kind", "security"}, {"depends_on", ark::Json::array()},
                 {"fixture", {{"dependencies", {{"requests", "2.31.0"}}},
                     {"advisories", ark::Json::array({{{"id", "CVE-FIXTURE"}, {"package", "requests"},
                         {"affected_versions", ark::Json::array({"2.31.0"})}, {"source", "local fixture"}}})}}}},
                {{"task_id", "report"}, {"kind", "document"}, {"depends_on", ark::Json::array({"audit"})},
                 {"fixture", {{"format", "docx"}, {"content", {{"paragraphs", ark::Json::array({"Audit result"})}}}}},
                 {"bindings", ark::Json::array({{{"from", "audit"}, {"source", "/output/matched_advisories/0"},
                     {"target", "/content/paragraphs/0"}}})}}
            })}}}};
        const ark::Transport no_network = [](const ark::Request&) -> ark::Response {
            throw std::runtime_error("fixture workflow must not make network requests");
        };
        const ark::DockerSandbox sandbox(no_network, "/missing/docker.sock", "not-used-by-offline-security-stage");
        const ark::AgentProfile profile{"1.0.0", "fixture", {}};
        const auto result = ark::run_tool_case(fixture, no_network, "http://127.0.0.1:1", profile,
                                                {.executable = "chromium"}, sandbox);
        if (result.at("status") != "DONE"
            || result.at("output").at("stage_status").at("audit") != "DONE"
            || result.at("output").at("stage_status").at("report") != "DONE"
            || result.at("output").at("stage_outputs").at("report").at("paragraphs").at(0)
                != "Audit result | CVE-FIXTURE"
            || result.at("agent_versions").at("security_agent") != "1.0.0"
            || result.at("agent_versions").at("document_agent") != "1.0.0") {
            throw std::runtime_error(result.dump(2));
        }
        std::cout << "native workflow DAG and safe data binding passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
