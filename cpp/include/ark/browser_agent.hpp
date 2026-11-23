#pragma once

#include "ark/browser.hpp"
#include "ark/http.hpp"
#include "ark/versions.hpp"

#include <functional>
#include <optional>

namespace ark {

using BrowserActivity = std::function<void(std::string_view action, std::string_view status)>;

AgentProfile browser_profile(std::string model);
std::optional<std::string> browser_start_url(std::string_view instruction);

class BrowserAgent {
public:
    BrowserAgent(Transport transport, std::string local_url, AgentProfile profile, BrowserOptions options = {});
    Json run(Json message, const BrowserActivity& activity = {}) const;

private:
    Transport transport_;
    std::string url_;
    AgentProfile profile_;
    BrowserOptions options_;
};

} // namespace ark
