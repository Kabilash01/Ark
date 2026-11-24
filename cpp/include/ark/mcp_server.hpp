#pragma once

#include "ark/message.hpp"

#include <functional>
#include <iosfwd>

namespace ark {

using McpToolHandler = std::function<Json(std::string_view name, const Json& arguments)>;

Json mcp_tool_catalog();
void serve_mcp_stdio(std::istream& input, std::ostream& output, const McpToolHandler& handler);

} // namespace ark
