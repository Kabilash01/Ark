#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

#include <filesystem>

namespace ark {

class DevOpsAgent {
public:
    explicit DevOpsAgent(Transport transport, std::filesystem::path socket = "/var/run/docker.sock");
    Json run(Json message) const;

private:
    Transport transport_;
    std::filesystem::path socket_;
};

} // namespace ark
