#pragma once

#include "ark/storage.hpp"

#include <map>

namespace ark {

struct AgentProfile {
    std::string version;
    std::string model;
    std::map<std::string, std::string, std::less<>> prompts;
};

class AgentVersioner {
public:
    explicit AgentVersioner(Redis& redis) : redis_(redis) {}

    bool initialized(std::string_view agent);
    AgentProfile ensure(std::string_view agent, const Json& capabilities, const AgentProfile& defaults);
    Json bump(std::string_view agent, std::string_view change_type, const Json& change);
    Json history(std::string_view agent);
    Json history_with_rollback_availability(std::string_view agent);
    Json rollback(std::string_view agent, std::string_view version);
    Json compare(std::string_view agent, std::string_view from, std::string_view to);

private:
    Redis& redis_;
    Json revision(std::string_view agent, std::string_view version);
};

} // namespace ark
