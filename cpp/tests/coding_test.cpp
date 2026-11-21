#include "ark/coding.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

constexpr auto bad_solution = "def add(a, b): return a - b\n";
constexpr auto good_solution = "def add(a, b): return a + b\n";
constexpr auto tests = "import unittest\nfrom solution import add\n"
                       "class TestSolution(unittest.TestCase):\n"
                       " def test_add(self): self.assertEqual(add(4, 3), 7)\n";

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

ark::Response model(const ark::Request& request, int& calls, bool repair) {
    const auto body = Json::parse(request.body);
    check(body.at("model") == "fixture-coder", "wrong configured coder");
    const auto input = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
    if (calls > 0) {
        check(input.at("locked_tests").at("test_solution.py") == tests, "repair changed original tests");
        check(input.at("test_result").at("exit_code") == 1, "missing failure diagnostics");
    }
    const Json output{{"files", {{"solution.py", (calls++ > 0 && repair) ? good_solution : bad_solution},
                                 {"test_solution.py", "untrusted duplicate"}}},
                      {"tests", {{"test_solution.py", calls == 1 ? tests : "weakened test"}}}};
    return {200, Json{{"message", {{"content", output.dump()}}}}.dump()};
}

void repair_injected_bug(const char* socket) {
    int calls = 0;
    ark::CurlTransport http;
    const ark::DockerSandbox sandbox(std::ref(http), socket ? socket : "/var/run/docker.sock");
    ark::CodingAgent agent([&](const auto& request) { return model(request, calls, true); },
        "http://localhost:11434", "fixture-coder", [&](const auto& workspace) {
            check(workspace.read_file("test_solution.py") == tests, "tests on disk must be identical");
            if (socket) {
                return sandbox.run_tests(workspace);
            }
            const bool ok = workspace.read_file("solution.py") == good_solution;
            return ark::SandboxResult{ok ? 0 : 1, false, "", ok ? "Ran 1 test" : "Expected 7, got 1"};
        });
    const auto result = agent.run(ark::make_message("router", "coding_agent", "CODE", {{"instruction", "Implement add"}}));
    check(result.at("status") == "DONE" && result.at("retry_count") == 1 && calls == 2, result.dump());
    check(result.at("output").at("files").at("solution.py") == good_solution, "no final code");
}

void caps_repairs() {
    int calls = 0;
    ark::CodingAgent agent([&](const auto& request) { return model(request, calls, false); },
        "http://localhost:11434", "fixture-coder", [](const auto&) {
            return ark::SandboxResult{1, false, "", "Expected 7, got 1"};
        });
    auto message = ark::make_message("router", "coding_agent", "CODE", {{"instruction", "Implement add"}});
    message["max_retries"] = 99;
    const auto result = agent.run(std::move(message));
    check(result.at("status") == "FAILED" && result.at("retry_count") == 3 && calls == 4, result.dump());
}

} // namespace

int main() {
    try {
        repair_injected_bug(std::getenv("ARK_TEST_DOCKER_SOCKET"));
        caps_repairs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
