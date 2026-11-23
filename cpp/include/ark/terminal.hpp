#pragma once

#include "ark/sandbox.hpp"

namespace ark {

class TerminalAgent {
public:
    explicit TerminalAgent(DockerSandbox sandbox);
    Json run(Json message) const;

private:
    DockerSandbox sandbox_;
};

} // namespace ark
