#include "ark/sandbox.hpp"

#include <algorithm>
#include <regex>
#include <stdexcept>

namespace ark {
namespace {

constexpr auto max_bundle_bytes = 64 * 1024;
constexpr auto max_output_bytes = 128 * 1024;
constexpr auto launcher = R"py(
import json, os, pathlib, subprocess, sys, unittest
request = json.loads(sys.argv[1])
root = pathlib.Path("/work/project")
root.mkdir()
for name, content in request["files"].items():
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
if request["packages"]:
    subprocess.run([sys.executable, "-I", "-m", "pip", "install", "--disable-pip-version-check",
                    "--no-index", "--find-links=/wheels", "--target=/work/site", *request["packages"]],
                   check=True)
    sys.path.insert(0, "/work/site")
os.chdir(root)
sys.path.insert(0, str(root))
if request["tests"]:
    for path in root.rglob("*.py"):
        compile(path.read_bytes(), str(path), "exec")
    suite = unittest.defaultTestLoader.discover(str(root))
    if not suite.countTestCases():
        raise SystemExit("No unittest tests discovered")
    raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
sys.argv = ["<ark-sandbox>"]
exec(compile(request["code"], "<ark-sandbox>", "exec"), {"__name__": "__main__"})
)py";

std::string project_path(std::string_view value) {
    const std::filesystem::path path(value);
    if (value.empty() || value.size() > 200 || path.is_absolute() || value.find('\\') != std::string_view::npos
        || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Sandbox file path must be relative");
    }
    for (const auto& part : path) {
        if (part.empty() || part == "." || part == "..") {
            throw std::invalid_argument("Sandbox file path must stay within the project directory");
        }
    }
    return path.generic_string();
}

void read_logs(std::string_view log, SandboxResult& result) {
    // Docker multiplexes stdout and stderr when Tty is disabled.
    while (!log.empty()) {
        if (log.size() < 8) {
            throw std::runtime_error("Docker returned a truncated log frame header");
        }
        std::size_t length = 0;
        for (int byte = 4; byte < 8; ++byte) {
            length = (length << 8) | static_cast<unsigned char>(log[byte]);
        }
        if (length > log.size() - 8) {
            throw std::runtime_error("Docker returned a truncated log frame");
        }
        auto& stream = log.front() == 1 ? result.output : result.error;
        stream.append(log.substr(8, std::min(length, max_output_bytes - stream.size())));
        log.remove_prefix(8 + length);
    }
}

} // namespace

void SandboxWorkspace::write_file(std::string_view path, const std::string& contents) {
    files_[project_path(path)] = contents;
}

std::string SandboxWorkspace::read_file(std::string_view path) const {
    return files_.at(project_path(path)).get<std::string>();
}

void SandboxWorkspace::install_package(std::string_view requirement) {
    static const std::regex package(R"([a-zA-Z0-9][a-zA-Z0-9._-]*(==[a-zA-Z0-9][a-zA-Z0-9._+-]*)?)");
    if (requirement.size() > 128 || !std::regex_match(requirement.begin(), requirement.end(), package)) {
        throw std::invalid_argument("Expected a package name or an exact version such as package==1.2");
    }
    if (std::find(packages_.begin(), packages_.end(), requirement) == packages_.end()) {
        packages_.push_back(requirement);
    }
}

Json SandboxResult::json() const {
    return {{"exit_code", exit_code}, {"timed_out", timed_out}, {"stdout", output}, {"stderr", error}};
}

DockerSandbox::DockerSandbox(Transport transport, std::filesystem::path socket, std::string image)
    : transport_(std::move(transport)), socket_(std::move(socket)), image_(std::move(image)) {
    if (!socket_.is_absolute() || image_.empty()) {
        throw std::invalid_argument("Docker sandbox requires a local Unix socket and a preloaded image");
    }
}

Response DockerSandbox::request(std::string_view route, std::string_view method, const std::string& body,
                                std::chrono::seconds timeout) const {
    return transport_({"http://localhost/v1.45" + std::string(route), std::string(method), body,
                       {"Content-Type: application/json"}, timeout, socket_.string()});
}

SandboxResult DockerSandbox::run_python(std::string_view code, const SandboxWorkspace& workspace,
                                        std::chrono::seconds timeout) const {
    return run_python_bundle({{"code", code}, {"files", workspace.files()}, {"packages", workspace.packages()},
                              {"tests", false}}, timeout);
}

SandboxResult DockerSandbox::run_tests(const SandboxWorkspace& workspace, std::chrono::seconds timeout) const {
    return run_python_bundle({{"code", ""}, {"files", workspace.files()}, {"packages", workspace.packages()},
                              {"tests", true}}, timeout);
}

SandboxResult DockerSandbox::run_command(std::string_view command, std::chrono::seconds timeout) const {
    if (command.find_first_not_of(" \t\r\n") == std::string_view::npos || command.size() > max_bundle_bytes
        || command.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Sandbox command must contain non-whitespace text, be at most 64 KiB, and contain no NUL bytes");
    }
    return run("/bin/bash", {"--noprofile", "--norc", "-c", command}, timeout);
}

SandboxResult DockerSandbox::run_python_bundle(const Json& input, std::chrono::seconds timeout) const {
    const auto bundle = input.dump();
    if (bundle.size() > max_bundle_bytes) {
        throw std::invalid_argument("Sandbox request exceeds 64 KiB");
    }
    return run("/usr/local/bin/python", {"-I", "-B", "-u", "-c", launcher, bundle}, timeout);
}

SandboxResult DockerSandbox::run(std::string_view executable, const Json& arguments,
                                std::chrono::seconds timeout) const {
    if (timeout < std::chrono::seconds(1) || timeout > std::chrono::seconds(180)) {
        throw std::invalid_argument("Sandbox timeout must be between 1 and 180 seconds");
    }
    const auto name = "ark-run-" + random_uuid();
    const auto container = "/containers/" + name;
    const Json specification{
        {"Image", image_}, {"User", "65534:65534"}, {"WorkingDir", "/work"},
        {"Entrypoint", Json::array({executable})}, {"Cmd", arguments},
        {"Tty", false}, {"NetworkDisabled", true},
        {"Env", {"HOME=/tmp", "OPENBLAS_NUM_THREADS=1", "OMP_NUM_THREADS=1"}},
        {"Labels", {{"io.ark.sandbox", "true"}}},
        {"HostConfig", {
            {"NetworkMode", "none"}, {"ReadonlyRootfs", true}, {"CapDrop", {"ALL"}},
            {"SecurityOpt", {"no-new-privileges"}}, {"PidsLimit", 64}, {"Memory", 512 * 1024 * 1024},
            {"MemorySwap", 512 * 1024 * 1024}, {"NanoCpus", 1'000'000'000},
            {"Tmpfs", {{"/work", "rw,noexec,nosuid,nodev,size=64m,mode=1777"},
                        {"/tmp", "rw,noexec,nosuid,nodev,size=32m,mode=1777"}}},
            {"LogConfig", {{"Type", "json-file"}, {"Config", {{"max-size", "1m"}, {"max-file", "1"}}}}},
        }},
    };
    const auto remove = [&] {
        const auto stopped = request(container + "?force=true&v=true", "DELETE", {}, std::chrono::seconds(10));
        if (stopped.status != 404) {
            stopped.require_success("Docker sandbox cleanup");
        }
    };

    try {
        const auto created = request("/containers/create?name=" + name, "POST", specification.dump());
        if (created.status == 404) {
            throw std::runtime_error("Sandbox image is missing. Pull or build " + image_ + " first.");
        }
        created.require_success("Docker sandbox creation");
        request(container + "/start", "POST").require_success("Docker sandbox start");

        SandboxResult result{124, false, "", ""};
        try {
            const auto waited = request(container + "/wait?condition=not-running", "POST", {}, timeout);
            waited.require_success("Docker sandbox wait");
            result.exit_code = Json::parse(waited.body).at("StatusCode").get<int>();
        } catch (const HttpTimeout&) {
            result.timed_out = true;
            const auto killed = request(container + "/kill?signal=SIGKILL", "POST", {}, std::chrono::seconds(10));
            if (killed.status != 409) {
                killed.require_success("Timed out sandbox termination");
            }
        }

        const auto logs = request(container + "/logs?stdout=true&stderr=true", "GET");
        logs.require_success("Docker sandbox logs");
        read_logs(logs.body, result);
        remove();
        return result;
    } catch (...) {
        // A timeout or daemon error after create may leave a named container.
        remove();
        throw;
    }
}

} // namespace ark
