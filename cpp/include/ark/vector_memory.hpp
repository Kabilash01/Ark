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

class SemanticIndex {
public:
    SemanticIndex(const std::filesystem::path& path, std::string model);
    ~SemanticIndex();
    void store(const Json& points);
    void used(const Json& rows);
    std::vector<std::string> unused(std::int64_t cutoff);
    void remove(const std::vector<std::string>& ids);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string model_;
};

class SemanticMemory {
public:
    SemanticMemory(Transport transport, std::string_view local_url, Embeddings encoder,
                   const std::filesystem::path& index_path = {});
    void store(const std::vector<SemanticRecord>& records);
    Json retrieve(std::string_view query, int limit = 5);
    Json by_metadata(const Json& filter, int limit = 50);
    int count();
    void remove(std::string_view id);
    std::size_t evict_unused(std::chrono::hours age = std::chrono::hours(30 * 24));

private:
    bool exists() const;
    Transport transport_;
    std::string collection_;
    Embeddings encoder_;
    std::unique_ptr<SemanticIndex> index_;
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
    Json all(int limit = -1);
    Json pending_conflicts(int limit = 20);
    bool review_conflict(std::int64_t id);
    std::size_t evict_low_success();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Embeddings encoder_;
};

Json memory_context(std::string_view task_id, std::string_view query,
                    WorkingMemory* working, SemanticMemory& semantic, ProceduralMemory* procedural);

} // namespace ark
