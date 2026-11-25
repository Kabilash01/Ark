#include "ark/tool_benchmark.hpp"

#include "ark/browser_agent.hpp"
#include "ark/document_agent.hpp"
#include "ark/kernel.hpp"
#include "ark/security_agent.hpp"
#include "ark/terminal.hpp"

#include <httplib.h>

#include <map>
#include <thread>

namespace ark {
namespace {

Json coordinated_case(const Json& task, const Transport& transport, std::string_view model_url,
                     const AgentProfile& profile, BrowserOptions options, const DockerSandbox& sandbox) {
    const auto& stages = task.at("fixture").at("stages");
    if (!stages.is_array() || stages.empty() || stages.size() > 12) {
        throw std::invalid_argument("Workflow fixtures require 1 to 12 stages");
    }
    Json dag{{"tasks", Json::array()}};
    std::map<std::string, Json> by_id;
    for (const auto& stage : stages) {
        const auto id = stage.at("task_id").get<std::string>();
        const auto kind = stage.at("kind").get<std::string>();
        const std::string_view agent = kind == "document" ? "document_agent" : kind == "terminal" ? "terminal_agent"
            : kind == "security" ? "security_agent" : kind == "browser" ? "browser_agent" : "";
        if (agent.empty() || !by_id.emplace(id, stage).second) {
            throw std::invalid_argument("Workflow stage kind or ID is invalid");
        }
        const auto intent = kind == "security" ? "SECURITY" : kind == "browser" ? "BROWSE" : "EXECUTE";
        Json planned_stage{{"task_id", id}, {"agent", agent}, {"intent", intent},
            {"instruction", stage.value("instruction", "Run the declared local benchmark stage")},
            {"depends_on", stage.value("depends_on", Json::array())}, {"parallel_with", Json::array()}};
        if (kind == "security") {
            planned_stage["payload"] = stage.at("fixture");
            planned_stage["payload"]["action"] = "check_dependencies";
        } else if (kind == "document") {
            const auto& fixture = stage.at("fixture");
            const auto format = fixture.at("format").get<std::string>();
            if (format != "docx" && format != "xlsx" && format != "pdf") {
                throw std::invalid_argument("Workflow document stages must write DOCX, XLSX, or PDF");
            }
            planned_stage["intent"] = "DOCUMENT";
            planned_stage["payload"] = fixture.at("content");
            planned_stage["payload"]["action"] = "write_" + format;
        }
        dag["tasks"].push_back(std::move(planned_stage));
    }
    const auto max_workers = task.at("fixture").value("max_workers", 4);
    if (max_workers < 1 || max_workers > 12) throw std::invalid_argument("Workflow max_workers must be from 1 to 12");
    const auto layers = order_tasks(dag, true);
    const auto results = dispatch_plan(dag, [&](const Task& current, const Json& dependencies) {
        const auto& stage = by_id.at(current.id);
        auto fixture = stage.at("fixture");
        std::size_t bindings_applied = 0;
        for (const auto& binding : stage.value("bindings", Json::array())) {
            const auto from = binding.at("from").get<std::string>();
            const auto source = binding.at("source").get<std::string>();
            const auto target = binding.at("target").get<std::string>();
            if (!dependencies.contains(from) || dependencies.at(from).value("status", "") != "DONE"
                || (source != "/output/stdout" && source != "/output/matched_advisories/0")
                || !target.starts_with("/content/paragraphs/")) {
                throw std::invalid_argument("Workflow bindings must copy a declared string into document paragraph text");
            }
            const auto& value = dependencies.at(from).at(ark::Json::json_pointer(source));
            if (!value.is_string()) throw std::invalid_argument("Workflow binding source must be a string");
            auto pointer = ark::Json::json_pointer(target);
            if (!fixture.contains(pointer) || !fixture.at(pointer).is_string()) {
                throw std::invalid_argument("Workflow binding destination must be an existing document paragraph");
            }
            auto& text = fixture[pointer].get_ref<std::string&>();
            const auto addition = value.get<std::string>();
            if (text.size() + addition.size() > 4000) throw std::invalid_argument("Workflow bound paragraph exceeds 4 KiB");
            text += " | " + addition;
            ++bindings_applied;
        }
        Json item{{"kind", stage.at("kind")}, {"fixture", std::move(fixture)}};
        auto result = run_tool_case(item, transport, model_url, profile, options, sandbox);
        result["workflow_binding_count"] = bindings_applied;
        if (!dependencies.empty()) {
            auto dependency_status = Json::object();
            for (const auto& [id, dependency] : dependencies.items()) {
                dependency_status[id] = dependency.value("status", "UNKNOWN");
            }
            result["workflow_dependencies"] = std::move(dependency_status);
        }
        return result;
    }, [](std::string_view) { return true; }, max_workers, true);

    auto statuses = Json::object();
    auto outputs = Json::object();
    auto versions = Json::object();
    bool done = true;
    for (const auto& stage : stages) {
        const auto id = stage.at("task_id").get<std::string>();
        const auto& result = results.at(id);
        const auto status = result.value("status", "FAILED");
        statuses[id] = status;
        done = done && status == "DONE";
        if (result.contains("output")) outputs[id] = result.at("output");
        if (result.contains("workflow_dependencies")) outputs[id]["workflow_dependencies"] = result.at("workflow_dependencies");
        if (result.contains("workflow_binding_count")) outputs[id]["workflow_binding_count"] = result.at("workflow_binding_count");
        if (result.contains("agent_version")) {
            const auto value = result.at("agent_version").get<std::string>();
            const auto split = value.find('@');
            if (split != std::string::npos) versions[value.substr(0, split)] = value.substr(split + 1);
        }
    }
    return {{"task_id", task.at("task_id")}, {"status", done ? "DONE" : "FAILED"},
        {"agent_version", task.at("kind") == "coordination" ? "coordination_agent@1.0.0" : "mixed_workflow@1.0.0"},
        {"agent_versions", std::move(versions)}, {"quality_score", done ? 1.0 : 0.0},
        {"output", {{"stage_status", std::move(statuses)}, {"stage_outputs", std::move(outputs)},
                     {"parallel_layers", layers.size()}}}};
}

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
    if (kind == "coordination" || kind == "mixed") {
        return coordinated_case(task, transport, model_url, profile, std::move(options), sandbox);
    }
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
