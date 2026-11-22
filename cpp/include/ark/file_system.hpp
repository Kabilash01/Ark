#pragma once

#include "ark/message.hpp"

namespace ark {

class EpisodicMemory;

class FileSystemAgent {
public:
    explicit FileSystemAgent(Json allowed_directories);
    Json run(Json message, EpisodicMemory& episodes) const;

private:
    Json directories_;
};

} // namespace ark
