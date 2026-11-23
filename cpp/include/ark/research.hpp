#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"
#include "ark/versions.hpp"

#include <filesystem>

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

Json read_json(const std::filesystem::path& path);

} // namespace ark
