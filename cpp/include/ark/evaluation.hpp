#pragma once

#include "ark/kernel.hpp"

namespace ark {

class EvaluationAgent {
public:
    EvaluationAgent(Transport transport, std::string url, std::string model);
    Json run(Json message) const;

private:
    Transport transport_;
    std::string url_;
    std::string model_;
};

} // namespace ark
