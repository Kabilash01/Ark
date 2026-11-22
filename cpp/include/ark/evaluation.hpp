#pragma once

#include "ark/kernel.hpp"

namespace ark {

AgentProfile evaluation_profile(std::string model);

class EvaluationAgent {
public:
    EvaluationAgent(Transport transport, std::string url, std::string model);
    EvaluationAgent(Transport transport, std::string url, AgentProfile profile);
    Json run(Json message) const;

private:
    Transport transport_;
    std::string url_;
    AgentProfile profile_;
};

} // namespace ark
