#include "ark/tool_benchmark.hpp"

#include "ark/browser_agent.hpp"
#include "ark/document_agent.hpp"
#include "ark/security_agent.hpp"
#include "ark/terminal.hpp"

#include <httplib.h>

#include <thread>

namespace ark {
namespace {

Json document_case(const Json& spec) {
    const auto root = std::filesystem::temp_directory_path() / ("ark-bench-" + random_uuid());
    if (!std::filesystem::create_directory(root)) throw std::runtime_error("Could not create document benchmark directory");
    struct Cleanup {
        const std::filesystem::path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};
    const auto format = spec.at("format").get<std::string>();
    if (format != "docx" && format != "pdf" && format != "xlsx") throw std::invalid_argument("Unknown document benchmark format");
    const auto path = root / ("fixture." + format);
    auto input = spec.at("content");
    input["path"] = path.string();
    input["action"] = "write_" + format;
    const DocumentAgent agent;
    auto result = agent.run(make_message("benchmark", "document_agent", "DOCUMENT", std::move(input)));
    if (result.at("status") != "DONE") return result;
    result = agent.run(make_message("benchmark", "document_agent", "DOCUMENT",
                                    {{"path", path.string()}, {"action", "read_" + format}}));
    if (result.at("status") == "DONE" && format == "pdf") {
        auto& pages = result["output"]["pages"];
        for (auto& page : pages) {
            auto& text = page.get_ref<std::string&>();
            const auto last = text.find_last_not_of(" \t\r\n\f");
            text.resize(last == std::string::npos ? 0 : last + 1);
        }
    }
    return result;
}

Json browser_case(const Json& spec, const Transport& transport, std::string_view model_url,
                  const AgentProfile& profile, BrowserOptions options) {
    struct Site {
        httplib::Server server;
        std::jthread thread;
        ~Site() { server.stop(); }
    } site;
    const auto& html = spec.at("html").get_ref<const std::string&>();
    site.server.Get("/", [&](const auto&, httplib::Response& response) { response.set_content(html, "text/html"); });
    const auto port = site.server.bind_to_any_port("127.0.0.1");
    if (port <= 0) throw std::runtime_error("Could not bind the browser benchmark fixture");
    site.thread = std::jthread([&] { site.server.listen_after_bind(); });
    site.server.wait_until_ready();
    const auto url = "http://127.0.0.1:" + std::to_string(port) + "/";
    const BrowserAgent agent(transport, std::string(model_url), profile, std::move(options));
    auto result = agent.run(make_message("benchmark", "browser_agent", "BROWSE",
        {{"url", url}, {"instruction", "Browse " + url + " and " + spec.at("instruction").get<std::string>()}, {"airgap_mode", true}}));
    auto& output = result.at("output");
    output["screenshot_count"] = output.at("screenshots").size();
    output.erase("screenshots");
    return result;
}

} // namespace

Json run_tool_case(const Json& task, const Transport& transport, std::string_view model_url,
                   const AgentProfile& profile, BrowserOptions options, const DockerSandbox& sandbox) {
    const auto& spec = task.at("fixture");
    const auto kind = task.at("kind").get<std::string>();
    if (kind == "browser") return browser_case(spec, transport, model_url, profile, std::move(options));
    if (kind == "document") return document_case(spec);
    if (kind == "terminal") return TerminalAgent(sandbox).run(make_message("benchmark", "terminal_agent", "EXECUTE", spec));
    if (kind == "security") {
        auto payload = spec;
        payload["action"] = "check_dependencies";
        auto result = SecurityAgent(sandbox).run(make_message("benchmark", "security_agent", "SECURITY", std::move(payload)));
        if (result.at("status") == "DONE") {
            auto ids = Json::array();
            for (const auto& item : result.at("output").at("findings")) ids.push_back(item.at("advisory"));
            result["output"]["matched_advisories"] = std::move(ids);
        }
        return result;
    }
    throw std::invalid_argument("Unsupported benchmark tool kind");
}

} // namespace ark
