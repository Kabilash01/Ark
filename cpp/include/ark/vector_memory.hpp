#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace ark {

class WorkingMemory;

class Embeddings {
public:
    Embeddings(Transport transport, std::string_view local_url, std::string model);
    std::vector<std::vector<float>> encode(const std::vector<std::string>& texts) const;
    const std::string& model() const { return model_; }

private:
    Transport transport_;
    std::string url_;
    std::string model_;
};

struct SemanticRecord {
    std::string id;
    std::string text;
    Json metadata = Json::object();
};

class SemanticMemory {
public:
    SemanticMemory(Transport transport, std::string_view local_url, Embeddings encoder);
    void store(const std::vector<SemanticRecord>& records);
    Json retrieve(std::string_view query, int limit = 5);
    Json by_metadata(const Json& filter, int limit = 50);
    int count();
    void remove(std::string_view id);

private:
    bool exists() const;
    Transport transport_;
    std::string collection_;
    Embeddings encoder_;
};

// SQLite stores records and normalized vectors in one transaction. An exact
// cosine scan preserves the previous candidate ranking for small libraries.
class ProceduralMemory {
public:
    ProceduralMemory(const std::filesystem::path& path, Embeddings encoder);
    ~ProceduralMemory();
    ProceduralMemory(const ProceduralMemory&) = delete;
    ProceduralMemory& operator=(const ProceduralMemory&) = delete;

    std::string add(std::string_view intent, std::string_view description, const Json& steps,
                    std::string_view source = "manual");
    Json retrieve(std::string_view query, std::size_t top_k = 5, std::size_t candidate_pool = 20);
    Json record_use(std::string_view id, bool success);
    Json get(std::string_view id);
    Json all();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Embeddings encoder_;
};

Json memory_context(std::string_view task_id, std::string_view query,
                    WorkingMemory* working, SemanticMemory& semantic, ProceduralMemory* procedural);

} // namespace ark
