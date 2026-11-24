#pragma once

#include "ark/inference.hpp"
#include "ark/versions.hpp"

namespace ark {

AgentProfile experiment_designer_profile(std::string model);

class ExperimentDesigner {
public:
    ExperimentDesigner(Transport transport, std::string url, AgentProfile profile,
                       InferenceProvider provider = InferenceProvider::ollama);
    Json run(Json message) const;

private:
    LocalInference inference_;
    AgentProfile profile_;
};

} // namespace ark
