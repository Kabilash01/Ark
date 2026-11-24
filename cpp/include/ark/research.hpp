#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"
#include "ark/versions.hpp"

#include <filesystem>
#include <array>
#include <condition_variable>
#include <mutex>

namespace ark {

AgentProfile research_profile(std::string model);

struct ResearchConfig {
    std::string model;
    std::string embedding_model = "bge-large:latest";
    std::string ollama_url = "http://127.0.0.1:11434";
    std::string qdrant_url = "http://127.0.0.1:6333";
    std::filesystem::path semantic_db;
    std::chrono::seconds arxiv_interval{3};
    std::string scholar_key;
    Json privacy = Json::object();
};

class ResearchAgent {
public:
    explicit ResearchAgent(Transport transport, ResearchConfig config);
    ResearchAgent(Transport transport, ResearchConfig config, AgentProfile profile);
    Json run(Json message);

private:
    Transport transport_;
    ResearchConfig config_;
    AgentProfile profile_;

    Json run_research(const Json& payload);
    Json chat(std::string_view prompt, const std::string& content);
    Json read_arxiv(const std::string& params);
    Json search_scholar(const std::string& query);
    void persist(const Json& sources);
};

class ResearchPool {
public:
    Json run(const std::function<Json(std::string_view instance)>& execute);
    void record_score(std::string_view instance, double score);
    Json snapshot();

private:
    struct Replica {
        std::string_view name;
        bool busy = false;
        std::size_t load_count = 0;
        std::size_t scored = 0;
        double score_sum = 0;
        bool preferred() const { return scored == 0 || score_sum / scored >= 0.65; }
    };
    std::array<Replica, 2> replicas_{{{"research_agent#1"}, {"research_agent#2"}}};
    std::mutex mutex_;
    std::condition_variable ready_;
};

Json read_json(const std::filesystem::path& path);

} // namespace ark
