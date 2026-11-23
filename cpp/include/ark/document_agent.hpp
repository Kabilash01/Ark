#pragma once

#include "ark/message.hpp"

namespace ark {

class DocumentAgent {
public:
    Json run(Json message) const;
};

} // namespace ark
