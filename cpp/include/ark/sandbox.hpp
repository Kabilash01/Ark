#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

#include <filesystem>

namespace ark {

class SandboxWorkspace {
public:
    void write_file(std::string_view path, const std::string& contents);
    std::string read_file(std::string_view path) const;
    void install_package(std::string_view requirement);

    const Json& files() const { return files_; }
    const Json& packages() const { return packages_; }

private:
    Json files_ = Json::object();
    Json packages_ = Json::array();
};

struct SandboxResult {
    int exit_code;
    bool timed_out;
    std::string output;
    std::string error;

    Json json() const;
};

class DockerSandbox {
public:
    DockerSandbox(Transport transport, std::filesystem::path socket = "/var/run/docker.sock",
                  std::string image = "python:3.11-slim");
    SandboxResult run_python(std::string_view code, const SandboxWorkspace& workspace = {},
                             std::chrono::seconds timeout = std::chrono::seconds(30)) const;
    SandboxResult run_tests(const SandboxWorkspace& workspace,
                            std::chrono::seconds timeout = std::chrono::seconds(30)) const;
    SandboxResult run_command(std::string_view command,
                              std::chrono::seconds timeout = std::chrono::seconds(30)) const;

private:
    Transport transport_;
    std::filesystem::path socket_;
    std::string image_;

    SandboxResult run_python_bundle(const Json& input, std::chrono::seconds timeout) const;
    SandboxResult run(std::string_view executable, const Json& arguments, std::chrono::seconds timeout) const;
    Response request(std::string_view route, std::string_view method, const std::string& body = {},
                     std::chrono::seconds timeout = std::chrono::seconds(30)) const;
};

} // namespace ark
