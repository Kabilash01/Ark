#include "ark/vector_memory.hpp"
#include "ark/storage.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ark {
namespace {

Response send(const Transport& transport, std::string url, std::string method, const Json& payload,
              std::chrono::seconds timeout = std::chrono::seconds(30)) {
    return transport({std::move(url), std::move(method), payload.dump(), {"Content-Type: application/json"}, timeout});
}

Json payload_result(const Response& response, std::string_view action) {
    response.require_success(action);
    return Json::parse(response.body).at("result");
}

Json flatten(const Json& points) {
    auto records = Json::array();
    for (const auto& point : points) {
        auto metadata = point.at("payload");
        auto text = metadata.at("text");
        metadata.erase("text");
        records.push_back({{"id", point.at("id")}, {"text", text},
                           {"metadata", metadata}, {"score", point.value("score", Json(nullptr))}});
    }
    return records;
}

void validate_limit(int limit) {
    if (limit <= 0 || limit > 1000) {
        throw std::invalid_argument("Memory query limit must be between 1 and 1000");
    }
}

} // namespace

Embeddings::Embeddings(Transport transport, std::string_view local_url, std::string model)
    : transport_(std::move(transport)), url_(local_base_url(local_url)), model_(std::move(model)) {
    if (model_.empty()) {
        throw std::invalid_argument("Embedding model must not be empty");
    }
}

std::vector<std::vector<float>> Embeddings::encode(const std::vector<std::string>& texts) const {
    if (texts.empty()) {
        return {};
    }
    const auto response = send(transport_, url_ + "/api/embed", "POST", {
        {"model", model_}, {"input", texts},
    }, std::chrono::seconds(120));
    response.require_success("Local embedding");
    const auto vectors = Json::parse(response.body).at("embeddings").get<std::vector<std::vector<float>>>();
    if (vectors.size() != texts.size() || vectors.front().empty()) {
        throw std::runtime_error("Embedding response must contain one nonempty vector per input");
    }
    const auto dimension = vectors.front().size();
    for (const auto& vector : vectors) {
        if (vector.size() != dimension
            || !std::all_of(vector.begin(), vector.end(), [](float value) { return std::isfinite(value); })) {
            throw std::runtime_error("Embedding response contained an invalid vector");
        }
    }
    return vectors;
}

SemanticMemory::SemanticMemory(Transport transport, std::string_view local_url, Embeddings encoder)
    : transport_(std::move(transport)),
      collection_(local_base_url(local_url) + "/collections/ark_cpp_" + point_id(encoder.model())),
      encoder_(std::move(encoder)) {}

bool SemanticMemory::exists() const {
    const auto response = transport_({collection_});
    if (response.status == 404) {
        return false;
    }
    response.require_success("Qdrant collection lookup");
    return true;
}

void SemanticMemory::store(const std::vector<SemanticRecord>& records) {
    if (records.empty()) {
        return;
    }
    std::vector<std::string> texts;
    for (const auto& record : records) {
        if (record.id.empty() || !record.metadata.is_object()) {
            throw std::invalid_argument("Semantic record requires an ID and a metadata object");
        }
        texts.push_back(record.text);
    }
    const auto vectors = encoder_.encode(texts);
    auto points = Json::array();
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        auto metadata = record.metadata;
        metadata["text"] = record.text;
        points.push_back({{"id", record.id}, {"payload", metadata}, {"vector", vectors[i]}});
    }
    if (!exists()) {
        const auto created = send(transport_, collection_, "PUT", {
            {"vectors", {{"size", vectors.front().size()}, {"distance", "Cosine"}}},
        });
        if (created.status != 409) {
            created.require_success("Qdrant collection creation");
        }
    }
    send(transport_, collection_ + "/points?wait=true", "PUT", {{"points", points}})
        .require_success("Qdrant upsert");
}

Json SemanticMemory::retrieve(std::string_view query, int limit) {
    validate_limit(limit);
    if (!exists()) {
        return Json::array();
    }
    const auto response = send(transport_, collection_ + "/points/query", "POST", {
        {"query", encoder_.encode({std::string(query)}).at(0)}, {"with_payload", true}, {"limit", limit},
    });
    return flatten(payload_result(response, "Qdrant semantic search").at("points"));
}

Json SemanticMemory::by_metadata(const Json& filter, int limit) {
    validate_limit(limit);
    if (!filter.is_object()) {
        throw std::invalid_argument("Metadata filter must be an object");
    }
    if (!exists()) {
        return Json::array();
    }
    auto fields = Json::array();
    for (const auto& [key, value] : filter.items()) {
        fields.push_back({{"key", key}, {"match", {{"value", value}}}});
    }
    Json request{{"with_payload", true}, {"limit", limit}};
    if (!fields.empty()) {
        request["filter"] = {{"must", fields}};
    }
    const auto response = send(transport_, collection_ + "/points/scroll", "POST", request);
    return flatten(payload_result(response, "Qdrant metadata search").at("points"));
}

int SemanticMemory::count() {
    if (!exists()) {
        return 0;
    }
    const auto response = send(transport_, collection_ + "/points/count", "POST", {{"exact", true}});
    return payload_result(response, "Qdrant count").at("count").get<int>();
}

void SemanticMemory::remove(std::string_view id) {
    if (exists()) {
        send(transport_, collection_ + "/points/delete?wait=true", "POST", {{"points", {id}}})
            .require_success("Qdrant deletion");
    }
}

Json memory_context(std::string_view task_id, std::string_view query,
                    WorkingMemory* working, SemanticMemory& semantic, ProceduralMemory* procedural) {
    auto context = Json::array();
    const auto append = [&context](std::string_view tier, Json data) {
        if (!data.empty()) {
            context.push_back({{"tier", tier}, {"data", std::move(data)}});
        }
    };
    if (working && !task_id.empty()) {
        append("working", working->all(task_id));
    }
    append("semantic", semantic.retrieve(query));
    if (procedural) {
        append("procedural", procedural->retrieve(query));
    }
    return context;
}

} // namespace ark
