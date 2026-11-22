#include "ark/privacy.hpp"

#include <algorithm>

namespace ark {

TaskPrivacy TaskPrivacy::resolve(const Json& config, const Json& payload) {
    const bool global_airgap = config.value("airgap_mode", false);
    const bool global_privacy = config.value("privacy_mode", false);
    const bool task_airgap = payload.value("airgap_mode", false);
    const bool task_privacy = payload.value("privacy", false);
    const bool airgap = global_airgap || task_airgap;
    return {airgap || global_privacy || task_privacy, airgap};
}

bool TaskPrivacy::allows_external(const Json& config, std::string_view host) const {
    if (airgap) {
        return false;
    }
    if (!enabled) {
        return true;
    }
    const auto allowed = config.find("allowed_external");
    return allowed != config.end() && allowed->is_array()
        && std::find(allowed->begin(), allowed->end(), host) != allowed->end();
}

} // namespace ark
