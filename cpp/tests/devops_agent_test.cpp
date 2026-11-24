#include "ark/devops_agent.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

Json request(Json files = {{"Dockerfile", "FROM scratch\nCOPY app /app\nENTRYPOINT [\"/app\"]\n"}, {"app", "native binary"}}) {
    return ark::make_message("test", "devops_agent", "DEVOPS", {{"files", std::move(files)}, {"command", Json::array({"/app"})}});
}

std::string frame(std::string_view value) {
    std::string result(8, '\0');
    result[0] = 1;
    auto size = static_cast<std::uint32_t>(value.size());
    for (int i = 0; i < 4; ++i) result[7 - i] = static_cast<char>((size >> (i * 8)) & 0xff);
    result.append(value);
    return result;
}

void builds_runs_and_cleans_up() {
    std::vector<std::string> routes;
    const auto transport = [&](const ark::Request& call) -> ark::Response {
        check(call.unix_socket == "/tmp/docker.sock", "Docker request did not use configured socket");
        routes.push_back(call.url);
        if (call.url.find("/build?") != std::string::npos) {
            check(call.headers.at(0) == "Content-Type: application/x-tar", "build context content type is wrong");
            check(call.body.size() >= 1024 && call.body.substr(257, 5) == "ustar", "build context is not a tar archive");
            check(call.url.find("networkmode=none") != std::string::npos, "image build networking was not disabled");
            return {200, "{\"stream\":\"Successfully built\\n\"}\n"};
        }
        if (call.url.find("/containers/create?name=") != std::string::npos) {
            const auto body = Json::parse(call.body);
            check(body.at("HostConfig").at("NetworkMode") == "none" && body.at("HostConfig").at("ReadonlyRootfs") == true,
                "runtime container restrictions are missing");
            check(body.at("Cmd") == Json::array({"/app"}), "runtime command argv was altered");
            return {201, R"({"Id":"container-id"})"};
        }
        if (call.url.ends_with("/containers/container-id/start")) return {204, ""};
        if (call.url.ends_with("/containers/container-id/wait?condition=not-running")) return {200, R"({"StatusCode":0})"};
        if (call.url.ends_with("/containers/container-id/logs?stdout=true&stderr=true")) return {200, frame("OK")};
        if (call.url.ends_with("/containers/container-id?force=true&v=true") || call.url.find("/images/") != std::string::npos) return {204, ""};
        throw std::runtime_error("unexpected Docker route " + call.url);
    };
    const auto result = ark::DevOpsAgent(transport, "/tmp/docker.sock").run(request());
    check(result.at("status") == "DONE" && result.at("output").at("stdout") == "OK", "DevOps run did not return successful output");
    check(!result.at("payload").contains("files"), "project source leaked into the response payload");
    check(routes.size() == 7, "DevOps did not complete build, run, and cleanup lifecycle");
}

void rejects_path_traversal_before_docker() {
    int calls = 0;
    const ark::DevOpsAgent agent([&](const ark::Request&) -> ark::Response { ++calls; return {500, "{}"}; }, "/tmp/docker.sock");
    const auto result = agent.run(request({{"Dockerfile", "FROM scratch"}, {"../escape", "no"}}));
    check(result.at("status") == "FAILED" && calls == 0, "path traversal was sent to Docker");
}
}

int main() {
    try { builds_runs_and_cleans_up(); rejects_path_traversal_before_docker(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
