#include "ark/mcp_server.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void serves_handshake_catalog_and_tools() {
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"fixture","version":"1"}}})" "\n"
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})" "\n"
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ark_task","arguments":{"goal":"inspect fixture"}}})" "\n"
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"ark_research","arguments":{"topic":"topic"}}})" "\n"
        R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"missing","arguments":{}}})" "\n"
        "invalid-json\n"
        R"({"jsonrpc":"2.0","id":6,"method":"unknown/method"})" "\n");
    std::ostringstream output;
    int calls = 0;
    ark::serve_mcp_stdio(input, output, [&](std::string_view name, const Json& args) {
        if (name != "ark_task") throw std::runtime_error("fixture execution failure");
        ++calls;
        check(args.at("goal") == "inspect fixture", "MCP dispatched wrong tool arguments");
        return Json{{"status", "DONE"}, {"task_id", "fixture-task"}};
    });
    std::istringstream lines(output.str());
    Json responses[7];
    std::string line;
    std::size_t count = 0;
    while (std::getline(lines, line)) { if (count < 7) responses[count] = Json::parse(line); ++count; }
    check(count == 7 && calls == 1, "MCP did not reply once per request while suppressing notifications");
    check(responses[0].at("id") == 1 && responses[0].at("result").at("protocolVersion") == "2025-11-25"
        && responses[0].at("result").at("capabilities").contains("tools"), "MCP handshake was malformed");
    check(responses[1].at("id") == 2 && responses[1].at("result").at("tools").size() == 5,
          "MCP tool catalog did not advertise the five ARK tools");
    check(responses[2].at("id") == 3 && responses[2].at("result").at("isError") == false
        && responses[2].at("result").at("structuredContent").at("task_id") == "fixture-task",
        "MCP tool result did not preserve structured ARK output");
    check(responses[3].at("id") == 4 && responses[3].at("result").at("isError") == true,
          "MCP execution errors were not returned as tool errors");
    check(responses[4].at("id") == 5 && responses[4].at("error").at("code") == -32602,
          "unknown MCP tool did not return an invalid-params error");
    check(responses[5].at("error").at("code") == -32700 && responses[6].at("error").at("code") == -32601,
          "MCP parse or method errors were malformed");
}

void requires_initialization_and_valid_handler() {
    std::istringstream input(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})" "\n");
    std::ostringstream output;
    ark::serve_mcp_stdio(input, output, [](auto, const auto&) { return Json::object(); });
    const auto response = Json::parse(output.str());
    check(response.at("error").at("code") == -32002, "MCP accepted requests before initialization");
    bool rejected = false;
    try { std::istringstream empty; std::ostringstream sink; ark::serve_mcp_stdio(empty, sink, {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "MCP server accepted a missing tool handler");
}
}

int main() {
    try { serves_handshake_catalog_and_tools(); requires_initialization_and_valid_handler(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
