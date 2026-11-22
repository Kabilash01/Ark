#pragma once

#include "ark/message.hpp"

namespace ark {

struct TaskPrivacy {
    bool enabled;
    bool airgap;

    static TaskPrivacy resolve(const Json& config, const Json& payload);
    bool allows_external(const Json& config, std::string_view host) const;
};

} // namespace ark
