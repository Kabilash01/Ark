#pragma once

#include "ark/http.hpp"
#include "ark/versions.hpp"

namespace ark {

class ProceduralMemory;

AgentProfile reflection_profile(std::string model);

class ReflectionAgent {
public:
    ReflectionAgent(Transport transport, std::string local_url, AgentProfile profile);
    Json run(const Json& task, EpisodicMemory& episodes, ProceduralMemory& skills) const;

private:
    Transport transport_;
    std::string url_;
    AgentProfile profile_;
};

} // namespace ark
