#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

namespace ark {

class GitHubAgent {
public:
    GitHubAgent(Transport transport, std::string token = {});
    Json run(Json message) const;

private:
    Json github_request(const Json& request, std::string_view action) const;
    Transport transport_;
    std::string token_;
};

} // namespace ark
