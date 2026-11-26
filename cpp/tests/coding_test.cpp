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
    check(body.at("format").at("required").at(1) == "source" && body.at("think") == false,
          "missing structured response and bounded model options");
    const auto input = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
    if (calls > 0) {
        check(!body.at("format").at("properties").contains("test_source"), "repairs should not generate tests");
        check(input.at("locked_tests").at("test_solution.py") == tests, "repair changed original tests");
        check(input.at("test_result").at("exit_code") == 1, "missing failure diagnostics");
    }
    const bool first = calls++ == 0;
    const Json output{{"filename", "solution"}, {"source", (!first && repair) ? good_solution : bad_solution},
                      {"test_source", first ? tests : "weakened test"}};
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

void retries_invalid_generated_source() {
    int calls = 0;
    ark::CodingAgent agent([&](const ark::Request& request) {
        const auto body = Json::parse(request.body);
        const auto input = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
        if (calls == 1) {
            check(input.at("review_feedback").get<std::string>().find("Invalid coding source file solution.py")
                      != std::string::npos, "invalid source was not returned as repair feedback");
            check(!input.contains("locked_tests"), "invalid generated tests were locked");
        }
        const Json output{{"filename", "solution.py"}, {"source", calls++ == 0 ? "" : good_solution},
                          {"test_source", tests}};
        return ark::Response{200, Json{{"message", {{"content", output.dump()}}}}.dump()};
    }, "http://localhost:11434", "fixture-coder", [](const auto&) {
        return ark::SandboxResult{0, false, "", "Ran 1 test"};
    });
    const auto result = agent.run(ark::make_message("router", "coding_agent", "CODE", {{"instruction", "Implement add"}}));
    check(result.at("status") == "DONE" && result.at("retry_count") == 1 && calls == 2,
          "invalid generated source did not trigger a bounded repair");
}

void unwraps_generated_code_fences() {
    ark::CodingAgent agent([](const ark::Request&) {
        const Json output{{"filename", "solution.py"},
            {"source", std::string("```python\n") + good_solution + "```"},
            {"test_source", std::string("```python\n") + tests + "```"}};
        return ark::Response{200, Json{{"message", {{"content", output.dump()}}}}.dump()};
    }, "http://localhost:11434", "fixture-coder", [](const auto& workspace) {
        check(workspace.read_file("solution.py") == good_solution
                  && workspace.read_file("test_solution.py") == tests,
              "code fences reached the Python sandbox");
        return ark::SandboxResult{0, false, "", "Ran 1 test"};
    });
    const auto result = agent.run(ark::make_message("router", "coding_agent", "CODE", {{"instruction", "Implement add"}}));
    check(result.at("status") == "DONE", "fenced model output failed after normalization");
}

void adds_math_import_without_moving_module_header(const char* socket) {
    const std::string source = "\"\"\"Attention example.\"\"\"\nfrom __future__ import annotations\n"
                               "def weight(value): return math.exp(value)\n";
    const std::string test = "import unittest\nfrom solution import weight\n"
                             "class TestWeight(unittest.TestCase):\n"
                             " def test_zero(self): self.assertEqual(weight(0), 1.0)\n";
    ark::CurlTransport http;
    const ark::DockerSandbox sandbox(std::ref(http), socket ? socket : "/var/run/docker.sock");
    ark::CodingAgent agent([&](const ark::Request&) {
        const Json output{{"filename", "solution.py"}, {"source", source}, {"test_source", test}};
        return ark::Response{200, Json{{"message", {{"content", output.dump()}}}}.dump()};
    }, "http://localhost:11434", "fixture-coder", [&](const auto& workspace) {
        const auto fixed = workspace.read_file("solution.py");
        check(fixed.starts_with("\"\"\"Attention example.\"\"\"\nfrom __future__ import annotations\n")
                  && fixed.find("import math") > fixed.find("from __future__"),
              "missing import repair moved the module docstring or future import");
        return socket ? sandbox.run_tests(workspace) : ark::SandboxResult{0, false, "", "Ran 1 test"};
    });
    const auto result = agent.run(ark::make_message("router", "coding_agent", "CODE", {{"instruction", "Implement weight"}}));
    check(result.at("status") == "DONE", "missing math import was not repaired");
}

void repairs_generated_nan_assertion(const char* socket) {
    constexpr auto generated = "import unittest\nfrom solution import add\n"
        "class TestSolution(unittest.TestCase):\n"
        " def test_nan(self):\n"
        "  self.assertEqual(add(float('nan'), 0), float('nan'))\n";
    int calls = 0;
    ark::CurlTransport http;
    const ark::DockerSandbox sandbox(std::ref(http), socket ? socket : "/var/run/docker.sock");
    ark::CodingAgent agent([&](const auto&) {
        ++calls;
        return ark::Response{200, Json{{"message", {{"content", Json{{"filename", "solution.py"},
            {"source", good_solution}, {"test_source", generated}}.dump()}}}}.dump()};
    }, "http://localhost:11434", "fixture-coder", [&](const auto& workspace) {
        const auto corrected = workspace.read_file("test_solution.py");
        check(corrected.find("import math") != std::string::npos
              && corrected.find("self.assertTrue(math.isnan(add(float('nan'), 0)))") != std::string::npos,
              "invalid generated NaN equality was not corrected");
        return socket ? sandbox.run_tests(workspace) : ark::SandboxResult{0, false, "", "Ran 1 test"};
    });
    const auto result = agent.run(ark::make_message("router", "coding_agent", "CODE", {{"instruction", "Implement integer add"}}));
    check(result.at("status") == "DONE" && calls == 1, result.dump());
}

} // namespace

int main() {
    try {
        repair_injected_bug(std::getenv("ARK_TEST_DOCKER_SOCKET"));
        caps_repairs();
        retries_invalid_generated_source();
        unwraps_generated_code_fences();
        adds_math_import_without_moving_module_header(std::getenv("ARK_TEST_DOCKER_SOCKET"));
        repairs_generated_nan_assertion(std::getenv("ARK_TEST_DOCKER_SOCKET"));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
