#pragma once

#include "ark/citation_graph.hpp"
#include "ark/vector_memory.hpp"
#include "ark/versions.hpp"

namespace ark {

AgentProfile hypothesis_profile(std::string model);
Json rank_hypotheses(const Json& candidates, const std::vector<std::vector<float>>& paper_embeddings);

class HypothesisEngine {
public:
    HypothesisEngine(Transport transport, std::string local_url, AgentProfile profile,
                     SemanticMemory& papers, CitationGraph& graph, const Embeddings& encoder);
    Json run(Json message) const;

private:
    Transport transport_;
    std::string local_url_;
    AgentProfile profile_;
    SemanticMemory& papers_;
    CitationGraph& graph_;
    const Embeddings& encoder_;
};

} // namespace ark
