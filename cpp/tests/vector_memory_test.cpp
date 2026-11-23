#include "ark/vector_memory.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool value, std::string_view reason) {
    if (!value) {
        throw std::runtime_error(std::string(reason));
    }
}

ark::Response response(const Json& body) { return {200, body.dump()}; }

ark::Response fake_embeddings(const ark::Request& request) {
    check(request.url.ends_with("/api/embed"), "wrong embedding endpoint");
    const auto input = Json::parse(request.body);
    check(input.at("model") == "fixture-encoder", "wrong embedding model");
    auto vectors = Json::array();
    for (const auto& text : input.at("input")) {
        vectors.push_back(text == "similar" ? Json::array({0.9, 0.4})
                          : text == "different" ? Json::array({0.0, 1.0}) : Json::array({1.0, 0.0}));
    }
    return response({{"embeddings", vectors}});
}

void semantic_round_trip() {
    bool exists = false;
    auto points = Json::array();
    int calls = 0;
    auto service = [&](const ark::Request& request) -> ark::Response {
        if (request.url.ends_with("/api/embed")) {
            ++calls;
            return fake_embeddings(request);
        }
        if (request.method == "GET") {
            return {exists ? 200 : 404, "{}"};
        }
        const auto body = Json::parse(request.body);
        if (request.url.ends_with("/points?wait=true")) {
            points = body.at("points");
            check(points.at(0).at("payload").at("text") == "similar", "text was not stored");
        } else if (request.url.ends_with("/points/count")) {
            return response({{"result", {{"count", points.size()}}}});
        } else if (request.url.ends_with("/points/query")) {
            check(body.at("query") == Json::array({1.0, 0.0}) && body.at("limit") == 5, "query vector not passed");
            auto match = points.at(0);
            match["score"] = 0.9;
            return response({{"result", {{"points", {match}}}}});
        } else if (request.url.ends_with("/points/scroll")) {
            if (body.contains("filter")) {
                check(body.at("filter").at("must").at(0).at("match").at("value") == "arxiv", "filter not passed");
            } else {
                check(body.at("limit") == 1, "unfiltered listing ignored its limit");
            }
            return response({{"result", {{"points", points}}}});
        } else if (request.url.ends_with("/points/delete?wait=true")) {
            check(body.at("points").at(0) == "paper", "unexpected delete request");
            points.clear();
        } else {
            check(body.at("vectors").at("size") == 2, "collection dimension not inferred from vectors");
            exists = true;
        }
        return response({{"result", true}});
    };

    ark::Embeddings embeddings(service, "http://127.0.0.1:11434", "fixture-encoder");
    ark::SemanticMemory memory(service, "http://127.0.0.1:6333", embeddings);
    check(memory.retrieve("query").empty() && !calls, "empty memory should not call embeddings");
    memory.store({{"paper", "similar", {{"source", "arxiv"}}}});
    check(memory.count() == 1, "point count wrong");
    const auto result = ark::memory_context("", "query", nullptr, memory, nullptr);
    const auto match = result.at(0).at("data").at(0);
    check(result.at(0).at("tier") == "semantic" && match.at("text") == "similar" && match.at("score") == 0.9,
          result.dump());
    check(memory.by_metadata({{"source", "arxiv"}}).at(0).at("metadata") == Json{{"source", "arxiv"}},
          "metadata shape changed");
    const auto previous_calls = calls;
    check(memory.by_metadata(Json::object(), 1).at(0).at("text") == "similar" && calls == previous_calls,
          "unfiltered listing should work without embeddings");
    memory.remove("paper");
    check(memory.count() == 0, "forget should remove the point");
}

void skills_persist_and_rank() {
    const auto path = std::filesystem::temp_directory_path() / (ark::random_uuid() + ".db");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    ark::Embeddings encoder(fake_embeddings, "http://127.0.0.1:11434", "fixture-encoder");
    std::string best;
    std::string similar;
    {
        ark::ProceduralMemory skills(path, encoder);
        check(skills.retrieve("query").empty(), "empty skill retrieval failed");
        best = skills.add("RESEARCH", "query", {"step one"});
        similar = skills.add("RESEARCH", "similar", {"step two"}, "manual");
        skills.add("RESEARCH", "different", {"step three"});
        check(skills.retrieve("query", 1).at(0).at("skill_id") == best, "cosine ranking wrong");
        skills.record_use(best, false);
        skills.record_use(similar, true);
    }
    ark::ProceduralMemory reopened(path, encoder);
    check(reopened.all().size() == 3, "skills didn't survive restart");
    check(reopened.all(1).size() == 1, "procedural listing ignored its limit");
    check(reopened.get(similar).at("success_rate") == 1.0, "skill outcome didn't persist");
    check(reopened.retrieve("query", 1, 2).at(0).at("skill_id") == similar,
          "reranking did not weight observed success");
    check(reopened.retrieve("query", 1, 1).at(0).at("skill_id") == best,
          "candidate pool should be selected by cosine first");
}

void conflicting_revisions_survive_and_rank() {
    const auto path = std::filesystem::temp_directory_path() / (ark::random_uuid() + ".db");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    const ark::Embeddings encoder(fake_embeddings, "http://127.0.0.1:11434", "fixture-encoder");
    ark::ProceduralMemory writer(path, encoder);
    const auto old = writer.add("RESEARCH", "query", {"Choose the first candidate"}, "research_agent");
    writer.record_use(old, false);
    ark::ProceduralMemory second(path, encoder);
    const auto latest = second.add("RESEARCH", " Query ", {"Validate evidence first"}, "reflection_agent");
    second.record_use(latest, true);
    ark::ProceduralMemory reopened(path, encoder);
    const auto queue = reopened.pending_conflicts();
    const auto selected = reopened.retrieve("query");
    check(queue.size() == 1 && reopened.all().size() == 2 && selected.size() == 1, "lost versions or review item");
    const auto& conflict = queue.at(0);
    check(conflict.at("previous").at("skill_id") == old && conflict.at("proposed").at("source") == "reflection_agent"
          && conflict.at("proposed").at("success_rate") == 1.0 && selected.at(0).at("skill_id") == latest,
          "revision provenance or success ranking changed");
    check(reopened.review_conflict(conflict.at("conflict_id").get<std::int64_t>())
          && writer.pending_conflicts().empty() && writer.all().size() == 2, "review discarded an original skill");
}

} // namespace

int main() {
    try {
        semantic_round_trip();
        skills_persist_and_rank();
        conflicting_revisions_survive_and_rank();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
