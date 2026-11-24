#include "ark/mcp_server.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace ark {
namespace {

Json property(std::string_view type, std::string_view description) {
    return {{"type", type}, {"description", description}};
}

Json tool(std::string_view name, std::string_view description, Json properties, Json required) {
    return {{"name", name}, {"description", description},
        {"inputSchema", {{"type", "object"}, {"additionalProperties", false},
            {"properties", std::move(properties)}, {"required", std::move(required)}}}};
}

Json rpc_error(const Json& id, int code, std::string message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

Json rpc_result(const Json& id, Json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

std::string text_content(const Json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump(2);
}

} // namespace

Json mcp_tool_catalog() {
    const Json privacy = {{"type", "boolean"}, {"description", "Enable the stricter privacy policy configured for ARK OS."}};
    Json security_properties = Json::object();
    security_properties["files"] = {{"type", "object"}, {"description", "Map of relative source filenames to source text."},
        {"additionalProperties", {{"type", "string"}}}};
    security_properties["language"] = {{"type", "string"}, {"enum", Json::array({"python", "java"})}};
    Json sage_properties = Json::object();
    sage_properties["loop_id"] = property("string", "Stable identifier used to resume this workflow.");
    sage_properties["topic"] = property("string", "Research topic.");
    sage_properties["material_ids"] = {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 2}, {"maxItems", 100}};
    sage_properties["figure_paths"] = {{"type", "array"}, {"items", {{"type", "string"}}}};
    return Json::array({
        tool("ark_research", "Research a topic using ARK OS local research workers.",
            {{"topic", property("string", "Scientific or technical topic to research.")}, {"privacy", privacy}}, {"topic"}),
        tool("ark_code", "Write and test Python code using ARK OS Coding Agent.",
            {{"instruction", property("string", "Implementation task and acceptance requirements.")},
             {"language", {{"type", "string"}, {"enum", {"python"}}, {"default", "python"},
                 {"description", "The current Coding Agent acceptance-test workspace supports Python."}}}}, {"instruction"}),
        tool("ark_security", "Scan supplied source code with ARK OS defensive security checks in Docker.",
            std::move(security_properties), {"files", "language"}),
        tool("ark_sage", "Run the checkpointed ARK SAGE materials screening workflow.",
            std::move(sage_properties),
            {"loop_id", "topic", "material_ids"}),
        tool("ark_task", "Submit a general goal through the ARK OS supervisor and planner.",
            {{"goal", property("string", "Goal for ARK OS to execute.")}, {"privacy", privacy}}, {"goal"})
    });
}

void serve_mcp_stdio(std::istream& input, std::ostream& output, const McpToolHandler& handler) {
    if (!handler) throw std::invalid_argument("MCP server requires a tool handler");
    bool initialized = false;
    bool initialize_received = false;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        if (line.size() > 4 * 1024 * 1024) {
            output << rpc_error(nullptr, -32600, "MCP message exceeds 4 MiB").dump() << '\n' << std::flush;
            continue;
        }
        Json request = Json::parse(line, nullptr, false);
        if (request.is_discarded()) {
            output << rpc_error(nullptr, -32700, "Parse error").dump() << '\n' << std::flush;
            continue;
        }
        if (!request.is_object() || !request.contains("jsonrpc") || !request.at("jsonrpc").is_string()
            || request.at("jsonrpc") != "2.0"
            || !request.contains("method") || !request.at("method").is_string()) {
            output << rpc_error(request.is_object() ? request.value("id", Json(nullptr)) : Json(nullptr),
                                -32600, "Invalid Request").dump() << '\n' << std::flush;
            continue;
        }
        const auto method = request.at("method").get<std::string>();
        const Json id = request.value("id", Json(nullptr));
        const bool notification = !request.contains("id");
        if (!notification && !(id.is_string() || id.is_number())) {
            output << rpc_error(nullptr, -32600, "Request ID must be a string or number").dump() << '\n' << std::flush;
            continue;
        }
        if (notification) {
            if (method == "notifications/initialized" && initialize_received) initialized = true;
            continue;
        }
        Json response;
        try {
            if (method == "initialize") {
                const auto params = request.value("params", Json::object());
                if (!params.is_object() || !params.contains("protocolVersion") || !params.at("protocolVersion").is_string()) {
                    response = rpc_error(id, -32602, "initialize requires a string protocolVersion");
                } else {
                    const auto requested = params.at("protocolVersion").get<std::string>();
                    static constexpr std::array supported{"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
                    std::string selected = "2025-11-25";
                    for (const auto version : supported) if (requested == version) { selected = version; break; }
                    initialized = false;
                    initialize_received = true;
                    response = rpc_result(id, {{"protocolVersion", selected}, {"capabilities", {{"tools", Json::object()}}},
                        {"serverInfo", {{"name", "ark-os"}, {"version", "0.1.0"}}},
                        {"instructions", "ARK OS local C++ agents. Tool inputs and results are processed by the configured ARK runtime."}});
                }
            } else if (method == "ping") {
                response = rpc_result(id, Json::object());
            } else if (!initialized) {
                response = rpc_error(id, -32002, "Server not initialized");
            } else if (method == "tools/list") {
                const auto params = request.value("params", Json::object());
                if (!params.is_object()) response = rpc_error(id, -32602, "tools/list params must be an object");
                else response = rpc_result(id, {{"tools", mcp_tool_catalog()}});
            } else if (method == "tools/call") {
                const auto params = request.value("params", Json::object());
                if (!params.is_object()) {
                    response = rpc_error(id, -32602, "tools/call params must be an object");
                } else if (!params.contains("name") || !params.at("name").is_string()) {
                    response = rpc_error(id, -32602, "tools/call requires a string tool name");
                } else {
                    const auto name = params.at("name").get<std::string>();
                    const auto arguments = params.value("arguments", Json::object());
                    if (!arguments.is_object()) {
                        response = rpc_error(id, -32602, "Tool arguments must be an object");
                    } else {
                        const auto catalog = mcp_tool_catalog();
                        const auto known = std::find_if(catalog.begin(), catalog.end(), [&](const Json& item) { return item.at("name") == name; });
                        if (known == catalog.end()) response = rpc_error(id, -32602, "Unknown tool: " + name);
                        else {
                            try {
                                const auto result = handler(name, arguments);
                                const bool failed = result.is_object() && result.contains("status")
                                    && result.at("status").is_string() && result.at("status") != "DONE";
                                response = rpc_result(id, {{"content", Json::array({{{"type", "text"}, {"text", text_content(result)}}})},
                                    {"structuredContent", result}, {"isError", failed}});
                            } catch (const std::exception& error) {
                                response = rpc_result(id, {{"content", Json::array({{{"type", "text"}, {"text", error.what()}}})},
                                    {"isError", true}});
                            }
                        }
                    }
                }
            } else {
                response = rpc_error(id, -32601, "Method not found: " + method);
            }
        } catch (const std::exception& error) {
            response = rpc_error(id, -32603, error.what());
        }
        output << response.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n' << std::flush;
    }
}

} // namespace ark
