#pragma once

#include "ark/http.hpp"
#include "ark/versions.hpp"

namespace ark {

AgentProfile web_search_profile(std::string model);

class WebSearchAgent {
public:
    WebSearchAgent(Transport transport, std::string local_url, AgentProfile profile, std::string api_key,
                   Json privacy);
    Json run(Json message) const;

private:
    Transport transport_;
    std::string url_;
    AgentProfile profile_;
    std::string key_;
    Json privacy_;
};

} // namespace ark
