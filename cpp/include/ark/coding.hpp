#pragma once

#include "ark/kernel.hpp"
#include "ark/sandbox.hpp"

namespace ark {

using TestRunner = std::function<SandboxResult(const SandboxWorkspace&)>;
AgentProfile coding_profile(std::string model);

class CodingAgent {
public:
    CodingAgent(Transport transport, std::string url, std::string model, TestRunner test_runner);
    CodingAgent(Transport transport, std::string url, AgentProfile profile, TestRunner test_runner);
    Json run(Json message) const;

private:
    Transport transport_;
    std::string url_;
    AgentProfile profile_;
    TestRunner test_;

    Json solve(const Json& payload, const Json& memory, int repairs) const;
};

} // namespace ark
