#pragma once

#include "ark/browser.hpp"
#include "ark/coding.hpp"

namespace ark {

Json run_tool_case(const Json& task, const Transport& transport, std::string_view model_url,
                   const AgentProfile& browser_profile, BrowserOptions browser_options,
                   const DockerSandbox& sandbox);

} // namespace ark
