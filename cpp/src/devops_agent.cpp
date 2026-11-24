#include "ark/devops_agent.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>

namespace ark {
namespace {

constexpr std::size_t max_files = 50;
constexpr std::size_t max_source_bytes = 1024 * 1024;
constexpr std::size_t max_logs = 64 * 1024;

std::string safe_path(std::string_view input) {
    const std::filesystem::path path(input);
    if (input.empty() || input.size() > 100 || path.is_absolute() || input.find('\\') != std::string_view::npos
        || input.find('\0') != std::string_view::npos) throw std::invalid_argument("Project file path is invalid");
    for (const auto& part : path) if (part.empty() || part == "." || part == "..") throw std::invalid_argument("Project file path escapes its build context");
    return path.generic_string();
}

void octal(std::span<unsigned char> field, std::uint64_t value) {
    std::fill(field.begin(), field.end(), static_cast<unsigned char>('0'));
    if (field.size() < 2) throw std::logic_error("invalid tar field");
    auto cursor = field.size() - 2;
    do {
        field[cursor--] = static_cast<unsigned char>('0' + value % 8);
        value /= 8;
    } while (value && cursor < field.size());
    if (value) throw std::invalid_argument("Project file is too large for a tar archive");
    field.back() = 0;
}

void append_tar(std::string& tar, std::string_view name, std::string_view contents) {
    std::array<unsigned char, 512> header{};
    std::copy(name.begin(), name.end(), header.begin());
    octal(std::span(header).subspan(100, 8), 0644);
    octal(std::span(header).subspan(108, 8), 0);
    octal(std::span(header).subspan(116, 8), 0);
    octal(std::span(header).subspan(124, 12), contents.size());
    octal(std::span(header).subspan(136, 12), 0);
    octal(std::span(header).subspan(329, 8), 0);
    octal(std::span(header).subspan(337, 8), 0);
    std::fill(header.begin() + 148, header.begin() + 156, static_cast<unsigned char>(' '));
    header[156] = '0';
    std::copy_n("ustar", 5, header.begin() + 257);
    header[262] = 0; header[263] = '0'; header[264] = '0';
    const auto checksum = std::accumulate(header.begin(), header.end(), std::uint64_t{0});
    octal(std::span(header).subspan(148, 7), checksum);
    header[154] = 0;
    header[155] = ' ';
    tar.append(reinterpret_cast<const char*>(header.data()), header.size());
    tar.append(contents);
    const auto padding = (512 - contents.size() % 512) % 512;
    tar.append(padding, '\0');
}

std::string archive(const Json& files) {
    if (!files.is_object() || files.empty() || files.size() > max_files || !files.contains("Dockerfile")) {
        throw std::invalid_argument("DevOps build requires 1–50 files including Dockerfile");
    }
    std::string tar;
    std::size_t total = 0;
    for (const auto& [raw_path, value] : files.items()) {
        const auto path = safe_path(raw_path);
        if (!value.is_string()) throw std::invalid_argument("Project files must contain text");
        const auto& contents = value.get_ref<const std::string&>();
        total += contents.size();
        if (total > max_source_bytes) throw std::invalid_argument("DevOps source bundle exceeds 1 MiB");
        append_tar(tar, path, contents);
    }
    tar.append(1024, '\0');
    return tar;
}

Response docker(const Transport& transport, const std::filesystem::path& socket,
                std::string route, std::string method, std::string body = {},
                std::string content_type = "application/json", std::chrono::seconds timeout = std::chrono::seconds(30)) {
    auto result = transport({"http://localhost/v1.45" + route, std::move(method), std::move(body),
        {"Content-Type: " + content_type}, timeout, socket.string()});
    if (result.status < 200 || result.status >= 300) {
        const auto detail = "Docker DevOps operation returned HTTP " + std::to_string(result.status)
            + (result.body.empty() ? std::string{} : ": " + result.body.substr(0, 400));
        if (result.status == 408 || result.status == 429 || result.status == 500 || result.status == 502
            || result.status == 503 || result.status == 504) throw TransientHttpError(detail);
        throw std::runtime_error(detail);
    }
    return result;
}

std::string docker_logs(std::string_view stream) {
    std::string output;
    while (!stream.empty()) {
        if (stream.size() < 8) throw std::runtime_error("Docker returned truncated container logs");
        std::size_t length = 0;
        for (int i = 4; i < 8; ++i) length = (length << 8) | static_cast<unsigned char>(stream[i]);
        if (length > stream.size() - 8) throw std::runtime_error("Docker returned truncated container log frame");
        const auto copy = std::min(length, max_logs - output.size());
        output.append(stream.substr(8, copy));
        stream.remove_prefix(8 + length);
        if (output.size() == max_logs) break;
    }
    return output;
}

} // namespace

DevOpsAgent::DevOpsAgent(Transport transport, std::filesystem::path socket)
    : transport_(std::move(transport)), socket_(std::move(socket)) {
    if (!transport_ || !socket_.is_absolute()) throw std::invalid_argument("DevOps Agent requires transport and an absolute Docker socket path");
}

Json DevOpsAgent::run(Json message) const {
    if (!message.is_object()) throw std::invalid_argument("DevOps Agent requires an ARKMessage object");
    message["agent_version"] = "devops_agent@1.0.0";
    std::string image;
    std::string container;
    bool built = false;
    try {
        if (!pending_for(message, "devops_agent") || message.at("intent") != "DEVOPS") throw std::invalid_argument("DevOps Agent requires a pending DEVOPS message");
        const auto& payload = message.at("payload");
        const auto files = archive(payload.at("files"));
        if (!payload.contains("command") || !payload.at("command").is_array() || payload.at("command").empty() || payload.at("command").size() > 20) {
            throw std::invalid_argument("DevOps run requires a 1–20 element command argument array");
        }
        Json command = Json::array();
        for (const auto& arg : payload.at("command")) {
            if (!arg.is_string() || arg.get_ref<const std::string&>().empty() || arg.get<std::string>().size() > 4096 || arg.get<std::string>().find('\0') != std::string::npos)
                throw std::invalid_argument("DevOps command arguments must be non-empty bounded strings");
            command.push_back(arg);
        }
        const auto token = random_uuid();
        image = "ark-devops:" + token.substr(0, 12);
        container = "ark-devops-" + token.substr(0, 12);
        const auto query = "/build?dockerfile=Dockerfile&t=" + url_encode(image) + "&rm=1&forcerm=1&networkmode=none";
        const auto build = docker(transport_, socket_, query, "POST", files, "application/x-tar", std::chrono::seconds(180));
        built = true;
        for (auto line = std::string_view(build.body); !line.empty();) {
            const auto newline = line.find('\n');
            const auto item = line.substr(0, newline);
            if (!item.empty()) {
                const auto event = Json::parse(item);
                if (event.contains("error")) throw std::runtime_error("Docker build failed: " + event.at("error").get<std::string>().substr(0, 500));
            }
            if (newline == std::string_view::npos) break;
            line.remove_prefix(newline + 1);
        }
        const Json spec{{"Image", image}, {"Cmd", command}, {"User", "65534:65534"}, {"WorkingDir", "/"},
            {"Tty", false}, {"NetworkDisabled", true}, {"HostConfig", {
                {"NetworkMode", "none"}, {"ReadonlyRootfs", true}, {"CapDrop", {"ALL"}},
                {"SecurityOpt", {"no-new-privileges"}}, {"PidsLimit", 64}, {"Memory", 256 * 1024 * 1024},
                {"MemorySwap", 256 * 1024 * 1024}, {"NanoCpus", 500'000'000},
                {"Tmpfs", {{"/tmp", "rw,noexec,nosuid,nodev,size=32m,mode=1777"}}}}}};
        const auto created = docker(transport_, socket_, "/containers/create?name=" + container, "POST", spec.dump());
        const auto id = Json::parse(created.body).at("Id").get<std::string>();
        container = id;
        docker(transport_, socket_, "/containers/" + id + "/start", "POST");
        const auto waited = docker(transport_, socket_, "/containers/" + id + "/wait?condition=not-running", "POST", {}, "application/json", std::chrono::seconds(120));
        const auto exit = Json::parse(waited.body).at("StatusCode").get<int>();
        const auto logs = docker(transport_, socket_, "/containers/" + id + "/logs?stdout=true&stderr=true", "GET");
        const auto output = docker_logs(logs.body);
        docker(transport_, socket_, "/containers/" + id + "?force=true&v=true", "DELETE");
        container.clear();
        docker(transport_, socket_, "/images/" + url_encode(image) + "?force=true", "DELETE");
        image.clear(); built = false;
        message["status"] = exit == 0 ? "DONE" : "FAILED";
        message["quality_score"] = -1.0;
        message["output"] = {{"exit_code", exit}, {"stdout", output}, {"image", "removed"}};
        message["payload"].erase("files");
        return message;
    } catch (const std::exception& error) {
        if (!container.empty()) {
            try { docker(transport_, socket_, "/containers/" + container + "?force=true&v=true", "DELETE"); } catch (...) {}
        }
        if (built && !image.empty()) {
            try { docker(transport_, socket_, "/images/" + url_encode(image) + "?force=true", "DELETE"); } catch (...) {}
        }
        message["status"] = dynamic_cast<const TransientHttpError*>(&error) ? "RETRY" : "FAILED";
        message["quality_score"] = -1.0;
        message["output"] = {{"error_type", "DevOpsExecutionFailed"}, {"error_message", std::string(error.what()).substr(0, 500)}};
        if (message.contains("payload")) message["payload"].erase("files");
        return message;
    }
}

} // namespace ark
