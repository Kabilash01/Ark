#pragma once

#include "ark/http.hpp"
#include "ark/inference.hpp"
#include "ark/versions.hpp"

namespace ark {

AgentProfile vision_profile(std::string model);

class VisionAgent {
public:
    VisionAgent(Transport transport, std::string url, AgentProfile profile,
                InferenceProvider provider = InferenceProvider::ollama);
    Json run(Json message) const;

private:
    LocalInference inference_;
    InferenceProvider provider_;
    AgentProfile profile_;
};

} // namespace ark
