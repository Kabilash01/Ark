#pragma once

#include "ark/inference.hpp"
#include "ark/versions.hpp"

namespace ark {

class SelfImprovementAgent {
public:
    using Evaluator = std::function<double(const AgentProfile&, const Json&)>;
    using Commit = std::function<Json(const AgentProfile&, double)>;

    SelfImprovementAgent(Transport transport, std::string url, std::string model,
                         InferenceProvider provider = InferenceProvider::ollama);

    Json propose(std::string_view target_agent, const AgentProfile& current,
                 const Json& reflection_logs) const;
    Json compare(const AgentProfile& current, std::string_view candidate_prompt,
                 const Json& benchmark_cases, const Evaluator& evaluate) const;
    Json promote_if_better(std::string_view target_agent, const AgentProfile& current,
                           std::string_view candidate_prompt, const Json& comparison,
                           const Commit& commit) const;

private:
    LocalInference inference_;
    std::string model_;
};

} // namespace ark
