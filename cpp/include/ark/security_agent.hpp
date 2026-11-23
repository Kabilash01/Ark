#pragma once

#include "ark/sandbox.hpp"

namespace ark {

class SecurityAgent {
public:
    explicit SecurityAgent(DockerSandbox sandbox);
    Json run(Json message) const;

private:
    DockerSandbox sandbox_;
};

} // namespace ark
