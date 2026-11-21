#include "ark/sandbox.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void path_and_package_guards() {
    ark::SandboxWorkspace workspace;
    workspace.write_file("tests/test_demo.py", "sample");
    check(workspace.read_file("tests/test_demo.py") == "sample", "workspace did not retain a file");
    workspace.install_package("example==1.2");
    workspace.install_package("example==1.2");
    check(workspace.packages().size() == 1, "duplicated package");
    try {
        workspace.write_file("../outside", "data");
        throw std::runtime_error("sandbox accepted an escaping path");
    } catch (const std::invalid_argument&) {}
    try {
        workspace.install_package("https://example.org/package");
        throw std::runtime_error("sandbox accepted an arbitrary package URL");
    } catch (const std::invalid_argument&) {}
}

void engine_protocol_and_timeout() {
    bool removed = false;
    bool killed = false;
    auto transport = [&](const ark::Request& request) -> ark::Response {
        check(request.unix_socket == "/var/run/docker.sock", "sandbox must use a Unix socket");
        if (request.url.find("/containers/create?name=ark-run-") != std::string::npos) {
            const auto options = Json::parse(request.body);
            const auto flags = options.at("HostConfig");
            check(flags.at("NetworkMode") == "none" && options.at("User") == "65534:65534"
                  && flags.at("ReadonlyRootfs") == true && !flags.contains("Binds")
                  && flags.at("CapDrop").at(0) == "ALL", "sandbox lacks isolation flags");
            return {201, "{}"};
        }
        if (request.url.ends_with("/wait?condition=not-running")) {
            check(request.timeout == std::chrono::seconds(1), "wrong execution timeout");
            throw ark::HttpTimeout("simulated timeout");
        }
        if (request.url.ends_with("/kill?signal=SIGKILL")) {
            killed = true;
        }
        if (request.method == "DELETE") {
            removed = true;
        }
        if (request.url.ends_with("/logs?stdout=true&stderr=true")) {
            return {200, std::string({1, 0, 0, 0, 0, 0, 0, 2}) + "ok"};
        }
        return {204, ""};
    };
    const ark::DockerSandbox sandbox(transport);
    const auto result = sandbox.run_python("print('ok')", {}, std::chrono::seconds(1));
    check(result.timed_out && result.exit_code == 124 && killed && removed && result.output == "ok",
          "timed-out sandbox not terminated, logged, or removed");
}

void real_docker(const char* socket) {
    ark::CurlTransport http;
    const ark::DockerSandbox sandbox(std::ref(http), socket);
    const auto host_path = std::filesystem::temp_directory_path() / ("ark-host-marker-" + ark::random_uuid());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{host_path};
    std::ofstream(host_path) << "host only";
    ark::SandboxWorkspace workspace;
    workspace.write_file("fixture.txt", "workspace");
    const auto code = "import os, pathlib, socket\n"
        "assert os.getuid() == 65534\n"
        "assert pathlib.Path('fixture.txt').read_text() == 'workspace'\n"
        "assert not pathlib.Path(" + Json(host_path.string()).dump() + ").exists()\n"
        "assert not pathlib.Path('/var/run/docker.sock').exists()\n"
        "assert [name for _, name in socket.if_nameindex()] == ['lo']\n"
        "print('isolated')\n";
    const auto probe = sandbox.run_python(code, workspace);
    check(probe.exit_code == 0 && probe.output == "isolated\n", probe.json().dump());
    const auto timeout = sandbox.run_python("while True: pass", {}, std::chrono::seconds(1));
    check(timeout.timed_out && timeout.exit_code == 124, timeout.json().dump());

    workspace.write_file("test_fixture.py", "import pathlib, unittest\n"
        "class Fixture(unittest.TestCase):\n"
        " def test_file(self): self.assertEqual(pathlib.Path('fixture.txt').read_text(), 'workspace')\n");
    const auto tests = sandbox.run_tests(workspace);
    check(tests.exit_code == 0 && tests.error.find("Ran 1 test") != std::string::npos, tests.json().dump());
}

} // namespace

int main() {
    try {
        path_and_package_guards();
        engine_protocol_and_timeout();
        if (const auto* socket = std::getenv("ARK_TEST_DOCKER_SOCKET")) {
            real_docker(socket);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
