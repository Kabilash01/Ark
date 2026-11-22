#include "ark/web_search.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void searches_and_limits_citations_to_returned_sources() {
    int calls = 0;
    int cite = 1;
    ark::WebSearchAgent agent([&](const ark::Request& request) {
        ++calls;
        const auto body = Json::parse(request.body);
        if (request.url == "https://api.tavily.com/search") {
            check(request.method == "POST" && body.at("query") == "A public query", "wrong search request");
            check(std::find(request.headers.begin(), request.headers.end(), "Authorization: Bearer test-key")
                != request.headers.end(), "missing provider authorization");
            return ark::Response{200, R"({"results":[
              {"url":"https://example.org/guide","title":"Guide","content":"Actual excerpt"},
              {"url":"https://example.org/guide","title":"Duplicate","content":"Duplicate"},
              {"url":"file:///secret","title":"Invalid URL","content":"Ignored"}]})"};
        }
        check(request.url == "http://localhost:11434/api/chat", "synthesis was not local");
        check(body.at("model") == "fixture-general", "wrong model for search synthesis");
        const auto input = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
        check(input.at("results").size() == 1 && input.at("results").at(0).at("id") == 1, "invalid search evidence");
        const Json answer{{"summary", "Fixture summary"}, {"source_ids", Json::array({cite})}};
        return ark::Response{200, Json{{"message", {{"content", answer.dump()}}}}.dump()};
    }, "http://localhost:11434", ark::web_search_profile("fixture-general"), "test-key", Json::object());

    auto task = ark::make_message("test", "web_search_agent", "BROWSE", {{"query", "A public query"}});
    const auto result = agent.run(task);
    check(result.at("status") == "DONE" && calls == 2, result.dump());
    check(result.at("output").at("citations").at(0).at("url") == "https://example.org/guide", "wrong citation URL");
    check(result.at("agent_version") == "web_search_agent@1.0.0", "missing search agent version");
    cite = 2;
    check(agent.run(task).at("status") == "FAILED", "invented citation was accepted");
    task["payload"]["privacy"] = true;
    const auto private_result = agent.run(task);
    check(private_result.at("status") == "FAILED" && calls == 4, "private query reached an external provider");
}

void airgap_overrides_the_provider_allowlist() {
    bool called = false;
    ark::WebSearchAgent agent([&](const ark::Request&) {
        called = true;
        return ark::Response{200, "{}"};
    }, "http://localhost:11434", ark::web_search_profile("fixture-general"), "test-key",
       {{"airgap_mode", true}, {"allowed_external", Json::array({"api.tavily.com"})}});
    const auto result = agent.run(ark::make_message("test", "web_search_agent", "BROWSE", {{"query", "A query"}}));
    check(result.at("status") == "FAILED" && !called, "airgap let a search request through");
}

} // namespace

int main() {
    try {
        searches_and_limits_citations_to_returned_sources();
        airgap_overrides_the_provider_allowlist();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
