#include "ark/hypothesis_engine.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

void ranks_five_candidates_against_twenty_papers() {
    std::vector<std::vector<float>> papers(20, {1.0F, 0.0F});
    Json candidates = Json::array();
    const std::array<std::vector<float>, 5> vectors{{{1.0F, 0.0F}, {-1.0F, 0.0F}, {0.0F, 1.0F}, {0.5F, 0.5F}, {0.8F, 0.6F}}};
    for (std::size_t i = 0; i < vectors.size(); ++i) {
        candidates.push_back({{"hypothesis", "Testable materials hypothesis " + std::to_string(i)},
            {"rationale", "Supported by a measurable change in material properties."},
            {"feasibility", 0.5 + 0.1 * static_cast<double>(i)}, {"paper_ids", {"paper_1"}}, {"embedding", vectors[i]}});
    }
    const auto ranked = ark::rank_hypotheses(candidates, papers, {"paper_1"});
    check(ranked.size() == 5 && ranked.at(0).at("rank") == 1, "hypothesis rank positions are missing");
    check(ranked.at(0).at("hypothesis") == "Testable materials hypothesis 1"
        && std::abs(ranked.at(0).at("novelty").get<double>() - 2.0) < 1e-6,
        "novelty or feasibility score did not rank the most distant feasible candidate first");
    check(!ranked.at(0).contains("embedding"), "transient embeddings leaked into hypothesis output");
    check(ranked.at(0).at("paper_ids") == Json::array({"paper_1"}), "evidence references were not retained");
}

void rejects_dimension_and_feasibility_errors() {
    const Json candidates = Json::array({{{"hypothesis", "A sufficiently long hypothesis"}, {"rationale", "A sufficiently long rationale"},
        {"feasibility", 2.0}, {"paper_ids", {"paper_1"}}, {"embedding", Json::array({1.0})}}});
    bool rejected = false;
    try { (void)ark::rank_hypotheses(candidates, {{1.0F}}, {"paper_1"}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "out-of-range feasibility was accepted");

    auto hallucinated = candidates;
    hallucinated[0]["feasibility"] = 0.5;
    hallucinated[0]["paper_ids"] = Json::array({"2401.99999"});
    rejected = false;
    try { (void)ark::rank_hypotheses(hallucinated, {{1.0F}}, {"2401.00001"}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "unsupported or hallucinated citation was accepted");
}

void prompts_for_specific_and_evidence_grounded_hypotheses() {
    const auto profile = ark::hypothesis_profile("fixture-chat");
    const auto& prompt = profile.prompts.at("system");
    check(prompt.find("Never invent, alter, or infer an arXiv ID") != std::string::npos
        && prompt.find("comparator or control") != std::string::npos
        && prompt.find("bounded condition or composition") != std::string::npos,
        "hypothesis prompt does not constrain citations and experimental specificity");
}

void orchestrates_qdrant_neo4j_and_local_model() {
    const auto transport = [](const ark::Request& request) -> ark::Response {
        if (request.method == "GET" && request.url.find("/collections/") != std::string::npos) return {200, "{}"};
        if (request.url.ends_with("/points/query")) {
            auto points = Json::array();
            for (int i = 0; i < 20; ++i) {
                const auto suffix = std::string(i < 10 ? "0000" : "000") + std::to_string(i + 1);
                points.push_back({{"id", i + 1}, {"payload", {{"text", "Materials science abstract " + std::to_string(i)},
                    {"title", "Paper " + std::to_string(i)}, {"arxiv_id", "2401." + suffix},
                    {"url", "https://arxiv.org/abs/2401." + suffix}}}});
            }
            return {200, Json{{"result", {{"points", std::move(points)}}}}.dump()};
        }
        if (request.url.ends_with("/api/embed")) {
            const auto body = Json::parse(request.body);
            const auto& inputs = body.at("input");
            Json vectors = Json::array();
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                const bool candidates = inputs.size() == 5;
                vectors.push_back(candidates && i == 4 ? Json::array({-1.0, 0.0}) : Json::array({1.0, 0.0}));
            }
            return {200, Json{{"embeddings", std::move(vectors)}}.dump()};
        }
        if (request.url.ends_with("/query/v2")) return {200, R"({"data":{"fields":["first","second","papers"],"values":[["alloy A","ceramic B",12]]},"errors":[]})"};
        if (request.url.ends_with("/api/chat")) {
            Json candidates = Json::array();
            for (int i = 0; i < 5; ++i) candidates.push_back({{"hypothesis", "A testable materials mechanism hypothesis number " + std::to_string(i)},
                {"rationale", "A measurable experiment can test this materials mechanism."}, {"feasibility", 0.6 + i * 0.05},
                {"paper_ids", {"2401.00001"}}});
            return {200, Json{{"message", {{"content", Json{{"hypotheses", std::move(candidates)}}.dump()}}}, {"done_reason", "stop"}}.dump()};
        }
        throw std::runtime_error("unexpected hypothesis fixture request: " + request.url);
    };
    ark::Embeddings encoder(transport, "http://127.0.0.1:11434", "fixture-embed");
    ark::SemanticMemory papers(transport, "http://127.0.0.1:6333", encoder);
    ark::CitationGraph graph(transport, "http://127.0.0.1:7474");
    ark::HypothesisEngine engine(transport, "http://127.0.0.1:11434", ark::hypothesis_profile("fixture-chat"), papers, graph, encoder);
    auto message = ark::make_message("test", "hypothesis_engine", "HYPOTHESIZE", {{"domain", "materials science"}});
    const auto result = engine.run(std::move(message));
    check(result.at("status") == "DONE" && result.at("output").at("reference_papers") == 20
        && result.at("output").at("citation_gaps") == 1 && result.at("output").at("hypotheses").size() == 5
        && result.at("output").at("references").size() == 20
        && result.at("output").at("hypotheses").at(0).at("paper_ids") == Json::array({"2401.00001"}),
        "Hypothesis Engine did not combine paper memory, citation gaps, and generated hypotheses");
}
}

int main() {
    try { ranks_five_candidates_against_twenty_papers(); rejects_dimension_and_feasibility_errors();
        prompts_for_specific_and_evidence_grounded_hypotheses(); orchestrates_qdrant_neo4j_and_local_model(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
