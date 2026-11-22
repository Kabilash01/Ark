#include "ark/privacy.hpp"

#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void resolves_strictest_policy() {
    const Json config{{"allowed_external", Json::array({"export.arxiv.org"})}, {"privacy_mode", true}};
    const auto global = ark::TaskPrivacy::resolve(config, {{"privacy", false}, {"airgap_mode", false}});
    check(global.enabled && !global.airgap, "a request cannot weaken global privacy");
    check(global.allows_external(config, "export.arxiv.org"), "configured literature provider was denied");
    for (const auto host : {"api.groq.com", "api.anthropic.com", "export.arxiv.org.example"}) {
        check(!global.allows_external(config, host), "host outside the exact allowlist was permitted");
    }

    const auto airgap = ark::TaskPrivacy::resolve(config, {{"airgap_mode", true}});
    check(airgap.enabled && airgap.airgap && !airgap.allows_external(config, "export.arxiv.org"),
          "airgap must override the allowlist and enable privacy");
    const auto local_private = ark::TaskPrivacy::resolve(Json::object(), {{"privacy", true}});
    check(!local_private.allows_external(Json::object(), "export.arxiv.org"), "missing allowlist must deny outside access");
    const auto disabled = ark::TaskPrivacy::resolve(Json::object(), Json::object());
    check(!disabled.enabled && disabled.allows_external(Json::object(), "export.arxiv.org"), "default behavior changed");
}

} // namespace

int main() {
    try {
        resolves_strictest_policy();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
