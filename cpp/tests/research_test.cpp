#include "ark/research.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

namespace {

using ark::Json;
using ark::Request;
using ark::Response;

constexpr auto paper_id = "1706.03762v7";
constexpr auto second_id = "1810.13409v1";
constexpr auto expected_point = "6569a4a5-db01-53b0-bd78-8e26856d2a08";

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

std::string entry(const std::string& id) {
    return "<entry><id>http://arxiv.org/abs/" + id + "</id>"
           "<title>Attention Is\n All You Need</title><summary>Self-attention replaces recurrence.</summary>"
           "<author><name>A. Author</name></author><published>2017-06-12</published></entry>";
}

std::string feed(const std::string& entries) {
    return "<feed xmlns=\"http://www.w3.org/2005/Atom\">" + entries + "</feed>";
}

Response json_response(const Json& response) { return {200, response.dump()}; }

Response completion(const Json& output) {
    return json_response({{"message", {{"content", output.dump()}}}});
}

struct Services {
    enum class Discovery { Arxiv, Scholar, Malformed, Empty };
    Discovery discovery = Discovery::Arxiv;
    bool bad_citation = false;
    bool collection_exists = false;
    std::map<std::string, Json> memory;
    std::vector<std::string> calls;

    Response operator()(const Request& request) {
        if (request.url.find("search_query=") != std::string::npos) {
            calls.push_back("search");
            check(request.url.find("ti%3Aattention") != std::string::npos, "query must be URL encoded");
            if (discovery == Discovery::Malformed) {
                return {200, "<!DOCTYPE feed [<!ENTITY test SYSTEM 'file:///tmp/ark-test'>]>" + feed("")};
            }
            return {200, feed(discovery == Discovery::Arxiv ? entry(paper_id) + entry(second_id) : "")};
        }
        if (request.url.find("semanticscholar.org") != std::string::npos) {
            calls.push_back("scholar");
            check(request.headers == std::vector<std::string>{"x-api-key: fixture-key"}, "missing Scholar key");
            return json_response({{"data", {{{"externalIds", {{"ArXiv", "1706.03762"}}}},
                                            {{"externalIds", nullptr}}}}});
        }
        if (request.url.find("id_list=") != std::string::npos) {
            calls.push_back("fetch");
            if (discovery == Discovery::Arxiv) {
                check(request.url.find("1706.03762v7%2C1810.13409v1") != std::string::npos, "papers should be fetched in one batch");
                return {200, feed(entry(paper_id) + entry(second_id) + entry("9999.99999"))};
            }
            check(request.url.ends_with("1706.03762"), "Scholar DOI-free fallback should use the arXiv ID");
            return {200, feed(entry(paper_id))};
        }
        if (request.url.ends_with("/api/embed")) {
            calls.push_back("embed");
            const auto inputs = Json::parse(request.body).at("input");
            check(inputs.size() == (discovery == Discovery::Arxiv ? 2U : 1U), "only requested papers can be stored");
            auto vectors = Json::array();
            for (const auto& item : inputs) {
                check(item == "Self-attention replaces recurrence.", "must embed the source abstract");
                vectors.push_back({0.5, 0.6});
            }
            return json_response({{"embeddings", vectors}});
        }
        if (request.url.ends_with("/api/chat")) {
            const auto body = Json::parse(request.body);
            const auto input = body.at("messages").at(1).at("content").get<std::string>();
            if (input == "The Transformer") {
                calls.push_back("query");
                return completion({{"query", "ti:attention"}});
            }
            calls.push_back("synthesis");
            check(body.at("model") == "fixture-model", "must use configured local model");
            check(body.at("format") == "json", "synthesis should request JSON");
            check(!memory.empty(), "persist abstracts before model synthesis");
            const auto papers = Json::parse(input).at("papers");
            check(papers.at(0).at("arxiv_id") == paper_id, "must supply provider paper IDs");
            return completion({{"summary", "A transformer uses attention."},
                               {"findings", {{{"text", "It uses attention."},
                                              {"arxiv_ids", {bad_citation ? "9999.99999" : paper_id}}}}}});
        }
        if (request.url.ends_with("/points?wait=true")) {
            calls.push_back("upsert");
            check(request.method == "PUT", "upsert uses PUT");
            for (const auto& point : Json::parse(request.body).at("points").get<std::vector<Json>>()) {
                memory[point.at("id").get<std::string>()] = point.at("payload");
            }
            return json_response({{"status", "ok"}});
        }
        if (request.url.find("/collections/") != std::string::npos) {
            if (request.method == "GET") {
                return {collection_exists ? 200 : 404, "{}"};
            }
            const auto body = Json::parse(request.body);
            check(body.at("vectors").at("size") == 2, "collection must use embedding dimension");
            collection_exists = true;
            return json_response({{"result", true}});
        }
        throw std::runtime_error("unexpected HTTP request");
    }
};

ark::ResearchConfig config() {
    ark::ResearchConfig config;
    config.model = "fixture-model";
    config.arxiv_interval = std::chrono::seconds(0);
    config.privacy = {{"allowed_external", {"export.arxiv.org"}}};
    config.scholar_key = "fixture-key";
    return config;
}

Json request(Json payload) { return ark::make_message("user", "research_agent", "RESEARCH", std::move(payload)); }

void native_workflow() {
    Services services;
    ark::ResearchAgent agent(std::ref(services), config());
    const auto task = request({{"instruction", "The Transformer"}, {"privacy", true}});
    const auto result = agent.run(task);
    check(result.at("status") == "DONE", result.dump());
    check(result.at("agent_version") == "research_agent@1.0.0", "agent version is part of existing message");
    check(result.at("task_id") == task.at("task_id") && result.size() == task.size(), "frozen message contract changed");
    check(result.at("output").at("citations").at(0).at("arxiv_id") == paper_id, "missing valid citation");
    check(services.calls == std::vector<std::string>{"query", "search", "fetch", "embed", "upsert", "synthesis"},
          "expected query, batched data requests, persistence, then synthesis");
    check(services.memory.at(expected_point).at("source") == "arxiv", "point ID must match Python uuid5");
    const auto again = agent.run(task);
    check(again.at("status") == "DONE" && services.memory.size() == 2, "retry duplicated paper records");
}

void fallback() {
    Services services;
    services.discovery = Services::Discovery::Scholar;
    ark::ResearchAgent agent(std::ref(services), config());
    const auto result = agent.run(request({{"query", "ti:attention"}}));
    check(result.at("status") == "DONE", result.dump());
    check(services.calls.at(1) == "scholar" && services.memory.size() == 1, "Scholar fallback was not used");
}

void explicit_paper_bypasses_search() {
    Services services;
    services.discovery = Services::Discovery::Scholar;
    ark::ResearchAgent agent(std::ref(services), config());
    const auto result = agent.run(request({{"instruction", "Summarize arXiv:1706.03762"}}));
    check(result.at("status") == "DONE", result.dump());
    check(services.calls == std::vector<std::string>{"fetch", "embed", "upsert", "synthesis"},
          "explicit paper ID should skip discovery requests");
}

void blocks_disallowed_network() {
    auto settings = config();
    settings.privacy["airgap_mode"] = true;
    auto transport = [](const Request&) -> Response {
        throw std::runtime_error("task made an unauthorized HTTP request");
    };
    ark::ResearchAgent agent(transport, settings);
    auto task = request({{"query", "ti:attention"}, {"airgap_mode", false}});
    auto result = agent.run(task);
    check(result.at("status") == "FAILED", "airgap must fail");
    check(result.at("output").at("error_message").get<std::string>().starts_with("Airgap"),
          "global airgap must block network even if payload disables it");

    settings.privacy = {{"allowed_external", Json::array()}};
    ark::ResearchAgent private_agent(transport, settings);
    task["payload"] = {{"query", "ti:attention"}, {"privacy", true}};
    result = private_agent.run(task);
    check(result.at("output").at("error_message").get<std::string>().starts_with("Privacy"),
          "privacy must block unlisted arXiv");

    Services services;
    services.discovery = Services::Discovery::Empty;
    ark::ResearchAgent restricted_agent(std::ref(services), config());
    result = restricted_agent.run(task);
    check(result.at("status") == "FAILED" && services.calls == std::vector<std::string>{"search"},
          "privacy must also block unlisted Scholar fallback");
}

void rejects_untrusted_responses() {
    Services services;
    services.bad_citation = true;
    ark::ResearchAgent agent(std::ref(services), config());
    const auto task = request({{"query", "ti:attention"}});
    auto result = agent.run(task);
    check(result.at("status") == "FAILED", "must reject invented citations");
    check(result.at("output").at("error_message").get<std::string>().find("outside the retrieved sources") != std::string::npos,
          "wrong citation failure");

    services.discovery = Services::Discovery::Malformed;
    result = agent.run(task);
    check(result.at("status") == "FAILED" && result.at("output").at("error_message").get<std::string>().find("DTD") != std::string::npos,
          "must reject arXiv feeds containing DTD declarations");
}

} // namespace

int main() {
    try {
        native_workflow();
        fallback();
        explicit_paper_bypasses_search();
        blocks_disallowed_network();
        rejects_untrusted_responses();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
