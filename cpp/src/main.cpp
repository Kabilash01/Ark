#include "ark/api_server.hpp"
#include "ark/benchmark.hpp"
#include "ark/browser.hpp"
#include "ark/browser_agent.hpp"
#include "ark/citation_graph.hpp"
#include "ark/capability_discovery.hpp"
#include "ark/checkpoint_manager.hpp"
#include "ark/coding.hpp"
#include "ark/conflict_resolver.hpp"
#include "ark/document_agent.hpp"
#include "ark/devops_agent.hpp"
#include "ark/data_analysis.hpp"
#include "ark/done_condition.hpp"
#include "ark/evaluation.hpp"
#include "ark/experiment_designer.hpp"
#include "ark/file_system.hpp"
#include "ark/github_agent.hpp"
#include "ark/hypothesis_engine.hpp"
#include "ark/inference.hpp"
#include "ark/kernel.hpp"
#include "ark/ml_engineer.hpp"
#include "ark/materials_project.hpp"
#include "ark/mcp_server.hpp"
#include "ark/obsidian_export.hpp"
#include "ark/privacy.hpp"
#include "ark/reflection.hpp"
#include "ark/reflection_study.hpp"
#include "ark/research.hpp"
#include "ark/runtime_files.hpp"
#include "ark/sandbox.hpp"
#include "ark/security_agent.hpp"
#include "ark/scientific_report.hpp"
#include "ark/self_improvement.hpp"
#include "ark/storage.hpp"
#include "ark/terminal.hpp"
#include "ark/task_scheduler.hpp"
#include "ark/tool_benchmark.hpp"
#include "ark/vector_memory.hpp"
#include "ark/versions.hpp"
#include "ark/vision_agent.hpp"
#include "ark/vision_runtime.hpp"
#include "ark/voice_input.hpp"
#include "ark/web_search.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

std::string env(const char* name, std::string fallback = "") {
    const auto* value = std::getenv(name);
    return value && *value ? value : std::move(fallback);
}

std::filesystem::path config_dir() { return env("ARK_CONFIG_DIR", "config"); }

ark::ResearchPool research_pool;
std::optional<std::pair<std::string, std::string>> active_prompt_override;
std::mutex active_prompt_override_mutex;

std::string run_child_json(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                           const ark::Json& input) {
    int to_child[2]{-1, -1}, from_child[2]{-1, -1};
    const auto close_fd = [](int& fd) { if (fd >= 0) { close(fd); fd = -1; } };
    if (pipe(to_child) != 0) throw std::runtime_error("Could not create MCP child input pipe");
    if (pipe(from_child) != 0) {
        close_fd(to_child[0]); close_fd(to_child[1]);
        throw std::runtime_error("Could not create MCP child output pipe");
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close_fd(to_child[0]); close_fd(to_child[1]); close_fd(from_child[0]); close_fd(from_child[1]);
        throw std::runtime_error("Could not initialize MCP child process");
    }
    int action_error = posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    if (!action_error) action_error = posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
    if (!action_error) action_error = posix_spawn_file_actions_addclose(&actions, to_child[1]);
    if (!action_error) action_error = posix_spawn_file_actions_addclose(&actions, from_child[0]);
    if (!action_error) action_error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (action_error) {
        posix_spawn_file_actions_destroy(&actions);
        close_fd(to_child[0]); close_fd(to_child[1]); close_fd(from_child[0]); close_fd(from_child[1]);
        throw std::runtime_error("Could not configure MCP child process pipes");
    }
    std::vector<std::string> command{executable.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& part : command) argv.push_back(part.data());
    argv.push_back(nullptr);
    pid_t child = -1;
    const int error = posix_spawn(&child, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close_fd(to_child[0]); close_fd(from_child[1]);
    if (error != 0) { close_fd(to_child[1]); close_fd(from_child[0]); throw std::runtime_error("Could not start native SAGE workflow subprocess"); }
    const auto text = input.dump() + "\n";
    std::size_t written = 0;
    while (written < text.size()) {
        const auto count = write(to_child[1], text.data() + written, text.size() - written);
        if (count < 0) { if (errno == EINTR) continue; close_fd(to_child[1]); close_fd(from_child[0]); kill(child, SIGKILL); waitpid(child, nullptr, 0); throw std::runtime_error("Could not send SAGE input"); }
        if (count == 0) { close_fd(to_child[1]); close_fd(from_child[0]); kill(child, SIGKILL); waitpid(child, nullptr, 0); throw std::runtime_error("Could not send SAGE input"); }
        written += static_cast<std::size_t>(count);
    }
    close_fd(to_child[1]);
    std::string output;
    char buffer[8192];
    for (;;) {
        const auto count = read(from_child[0], buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; close_fd(from_child[0]); kill(child, SIGKILL); waitpid(child, nullptr, 0); throw std::runtime_error("Could not read SAGE result"); }
        if (output.size() + static_cast<std::size_t>(count) > 16 * 1024 * 1024) { close_fd(from_child[0]); kill(child, SIGKILL); waitpid(child, nullptr, 0); throw std::runtime_error("SAGE result exceeded 16 MiB"); }
        output.append(buffer, static_cast<std::size_t>(count));
    }
    close_fd(from_child[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) throw std::runtime_error("Could not wait for SAGE subprocess");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error("Native SAGE workflow failed: " + output.substr(0, 1000));
    return output;
}

class PromptOverrideGuard {
public:
    PromptOverrideGuard(std::string agent, std::string prompt) {
        const std::lock_guard lock(active_prompt_override_mutex);
        previous_ = active_prompt_override;
        active_prompt_override = std::make_pair(std::move(agent), std::move(prompt));
    }
    ~PromptOverrideGuard() {
        const std::lock_guard lock(active_prompt_override_mutex);
        active_prompt_override = std::move(previous_);
    }
private:
    std::optional<std::pair<std::string, std::string>> previous_;
};

std::string configured_model(const char* override, const char* role) {
    const auto selected = env(override);
    return selected.empty()
        ? ark::read_json(config_dir() / "active-models.json").at("models").at(role).get<std::string>()
        : selected;
}

ark::ResearchConfig config(bool chat_required = true) {
    ark::ResearchConfig config;
    if (chat_required) {
        config.model = configured_model("ARK_MODEL", "general");
    }
    const auto privacy = config_dir() / "privacy_config.json";
    if (std::filesystem::exists(privacy)) {
        config.privacy = ark::read_json(privacy);
    }
    config.embedding_model = env("ARK_EMBEDDING_MODEL", config.embedding_model);
    config.ollama_url = env("ARK_OLLAMA_URL", config.ollama_url);
    config.qdrant_url = env("ARK_QDRANT_URL", config.qdrant_url);
    config.semantic_db = env("ARK_SEMANTIC_DB", "data/semantic_cpp.db");
    config.scholar_key = env("SEMANTIC_SCHOLAR_API_KEY");
    return config;
}

std::filesystem::path skills_path() { return env("ARK_PROCEDURAL_DB", "data/procedural_cpp.db"); }

std::filesystem::path conflict_path() { return env("ARK_CONFLICT_DB", "data/conflicts.db"); }

void record_conflict(std::string_view id, const ark::Json& event) {
    try {
        ark::ConflictJournal(conflict_path()).record(id, event);
    } catch (const std::exception& error) {
        std::cerr << "Could not record conflict telemetry: " << error.what() << '\n';
    }
}

ark::Json conflict_report() {
    auto events = ark::ConflictJournal(conflict_path()).recent();
    if (std::filesystem::exists(skills_path())) {
        ark::CurlTransport http;
        const auto settings = config(false);
        ark::ProceduralMemory skills(skills_path(), ark::Embeddings(std::ref(http), settings.ollama_url,
                                                                    settings.embedding_model));
        for (auto& event : skills.conflict_history()) events.push_back(std::move(event));
    }
    auto report = ark::summarize_conflicts(std::move(events));
    report["coverage"] = {{"window_days", 30}, {"historical_backfill", false},
        {"source", "native runtime conflict telemetry and procedural-memory review history"},
        {"note", "The runtime journal records events going forward; prior incidents without stored telemetry cannot be reconstructed."}};
    return report;
}

ark::DockerSandbox docker_sandbox(ark::Transport transport) {
    return ark::DockerSandbox(std::move(transport), env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"),
                              env("ARK_SANDBOX_IMAGE", "python:3.11-slim"));
}

int choose_models(const char* override_vram) {
    const auto dir = config_dir();
    double vram = 0;
    if (override_vram) {
        std::size_t end = 0;
        const std::string value = override_vram;
        vram = std::stod(value, &end);
        if (end != value.size()) {
            throw std::invalid_argument("VRAM override must be a number in GiB");
        }
    } else {
        vram = ark::detect_vram_gib();
    }
    const auto selected = ark::pick_models(ark::read_json(dir / "model-library.json"), vram, ark::detect_ram_gib());
    ark::write_json(dir / "active-models.json", selected);
    std::cout << selected.dump(2) << '\n';
    return 0;
}

int run_sandbox() {
    const auto input = ark::Json::parse(std::cin);
    ark::SandboxWorkspace workspace;
    const auto files = input.value("files", ark::Json::object());
    if (!files.is_object()) {
        throw std::invalid_argument("Sandbox files must be an object of relative paths and contents");
    }
    for (const auto& [path, contents] : files.items()) {
        workspace.write_file(path, contents.get<std::string>());
    }
    for (const auto& package : input.value("packages", std::vector<std::string>{})) {
        workspace.install_package(package);
    }
    ark::CurlTransport http;
    const auto sandbox = docker_sandbox(std::ref(http));
    const auto timeout = std::chrono::seconds(input.value("timeout_seconds", 30));
    const auto output = input.value("tests", false)
        ? sandbox.run_tests(workspace, timeout)
        : sandbox.run_python(input.at("code").get<std::string>(), workspace, timeout);
    std::cout << output.json().dump(2, ' ', false, ark::Json::error_handler_t::replace) << '\n';
    return output.exit_code == 0 ? 0 : output.timed_out ? 124 : 1;
}

int run_terminal(const char* command) {
    auto payload = command ? ark::Json{{"command", command}} : ark::Json::parse(std::cin);
    ark::CurlTransport http;
    const ark::TerminalAgent agent(docker_sandbox(std::ref(http)));
    const auto result = agent.run(ark::make_message("user", "terminal_agent", "EXECUTE", std::move(payload)));
    std::cout << result.dump(2, ' ', false, ark::Json::error_handler_t::replace) << '\n';
    return result.at("status") == "DONE" ? 0 : result.at("output").value("timed_out", false) ? 124 : 1;
}

int run_browser() {
    const auto input = ark::Json::parse(std::cin);
    const auto& actions = input.at("actions");
    if (!actions.is_array() || actions.empty()) {
        throw std::invalid_argument("Browser requires a nonempty actions array");
    }
    const auto timeout = input.value("timeout_ms", ark::Json(10000));
    if (!timeout.is_number_integer() || timeout.get<double>() < 1 || timeout.get<double>() > 120000) {
        throw std::invalid_argument("Browser timeout_ms must be between 1 and 120000");
    }
    ark::Browser browser({env("ARK_BROWSER_BINARY", "chromium"), std::chrono::milliseconds(timeout.get<int>()),
                          config(false).privacy, input.value("privacy", false), input.value("airgap_mode", false)});
    auto results = ark::Json::array();
    for (const auto& action : actions) {
        const auto name = action.at("action").get<std::string>();
        const auto selector = [&] { return action.at("selector").get<std::string>(); };
        ark::Json value = true;
        if (name == "navigate") {
            browser.navigate(action.at("url").get_ref<const std::string&>());
        } else if (name == "get_page_text") {
            value = browser.get_page_text();
        } else if (name == "get_page_html") {
            value = browser.get_page_html();
        } else if (name == "click") {
            browser.click(selector());
        } else if (name == "type_text") {
            browser.type_text(selector(), action.at("text").get_ref<const std::string&>());
        } else if (name == "screenshot") {
            value = {{"mime_type", "image/png"}, {"data", browser.screenshot()}};
        } else if (name == "fill_form") {
            browser.fill_form(action.at("fields"));
        } else if (name == "extract_links") {
            value = browser.extract_links();
        } else if (name == "extract_table") {
            value = browser.extract_table(selector());
        } else if (name == "wait_for_element") {
            value = browser.wait_for_element(selector());
        } else if (name == "close_browser") {
            browser.close_browser();
        } else {
            throw std::invalid_argument("Unknown browser action: " + name);
        }
        results.push_back({{"action", name}, {"result", std::move(value)}});
    }
    std::cout << ark::Json{{"results", std::move(results)}}.dump(2) << '\n';
    return 0;
}

ark::AgentProfile actor_profile(std::string_view name, const std::string& general_model, ark::Redis* redis) {
    ark::Json capabilities;
    ark::AgentProfile defaults;
    if (name == "research_agent") {
        defaults = ark::research_profile(general_model);
        capabilities = {"RESEARCH", "SEARCH"};
    } else if (name == "coding_agent") {
        defaults = ark::coding_profile(configured_model("ARK_CODE_MODEL", "code"));
        capabilities = {"CODE"};
    } else if (name == "evaluation_agent") {
        defaults = ark::evaluation_profile(env("ARK_EVAL_MODEL", general_model));
        capabilities = {"EVALUATE"};
    } else if (name == "planner_agent") {
        defaults = ark::planner_profile(general_model);
        capabilities = {"PLAN"};
    } else if (name == "supervisor_agent") {
        defaults = ark::supervisor_profile(general_model);
        capabilities = {"SUPERVISE"};
    } else if (name == "reflection_agent") {
        defaults = ark::reflection_profile(env("ARK_REFLECTION_MODEL", general_model));
        capabilities = {"REFLECT"};
    } else if (name == "web_search_agent") {
        defaults = ark::web_search_profile(general_model);
        capabilities = {"BROWSE"};
    } else if (name == "browser_agent") {
        defaults = ark::browser_profile(general_model);
        capabilities = {"BROWSE"};
    } else if (name == "vision_agent") {
        defaults = ark::vision_profile(general_model);
        capabilities = {"VISION"};
    } else if (name == "terminal_agent") {
        defaults = {"1.0.0", "docker-sandbox", {{"system", "Run requested shell commands only inside the restricted Docker sandbox."}}};
        capabilities = {"EXECUTE"};
    } else if (name == "security_agent") {
        defaults = {"1.0.0", "semgrep-bandit", {{"system", "Run defensive source and dependency checks on supplied files only."}}};
        capabilities = {"SECURITY"};
    } else if (name == "document_agent") {
        defaults = {"1.0.0", "native-document-io", {{"system", "Create the requested document from supplied content."}}};
        capabilities = {"DOCUMENT"};
    } else {
        throw std::invalid_argument("Unknown versioned agent " + std::string(name));
    }
    auto selected = defaults;
    if (redis) {
        try { selected = ark::AgentVersioner(*redis).ensure(name, capabilities, defaults); }
        catch (const ark::RedisUnavailable&) {}
    }
    {
        const std::lock_guard lock(active_prompt_override_mutex);
        if (active_prompt_override && active_prompt_override->first == name && selected.prompts.contains("system"))
            selected.prompts["system"] = active_prompt_override->second;
    }
    return selected;
}

std::unique_ptr<ark::Redis> runtime_redis(int port, int db) {
    try { return std::make_unique<ark::Redis>(port, db); }
    catch (const ark::RedisUnavailable&) { return nullptr; }
}

std::unique_ptr<ark::Redis> version_store(bool use_default_port = false, bool strict = false) {
    const auto port = env("ARK_REDIS_PORT", use_default_port ? "6379" : "");
    if (port.empty()) return nullptr;
    const auto db = std::stoi(env("ARK_REDIS_DB", "0"));
    return strict ? std::make_unique<ark::Redis>(std::stoi(port), db) : runtime_redis(std::stoi(port), db);
}

void audit_event(ark::Redis* redis, ark::Json event) {
    if (!redis) return;
    try { ark::MessageBus(*redis).log(std::move(event)); }
    catch (const ark::RedisUnavailable&) {}
}

ark::Json run_web_search(const ark::Transport& http, const ark::ResearchConfig& settings, ark::Json message,
                         const ark::AgentProfile& profile) {
    const ark::WebSearchAgent worker(std::ref(http), settings.ollama_url, profile, env("TAVILY_API_KEY"), settings.privacy);
    return worker.run(std::move(message));
}

ark::Json run_browser_agent(const ark::Transport& http, const ark::ResearchConfig& settings, ark::Json message,
                            const ark::AgentProfile& profile, const ark::BrowserActivity& activity = {}) {
    const ark::BrowserAgent agent(http, settings.ollama_url, profile,
        {.executable = env("ARK_BROWSER_BINARY", "chromium"), .privacy = settings.privacy});
    return agent.run(std::move(message), activity);
}

ark::Json run_vision_agent(const ark::Transport& http, const ark::ResearchConfig& settings, ark::Json message,
                           const ark::AgentProfile& profile) {
    const auto llama_url = ark::configured_llama_url();
    const bool use_llama = !llama_url.empty();
    ark::VisionAgent agent(http, use_llama ? llama_url : settings.ollama_url, profile,
        use_llama ? ark::InferenceProvider::llama_cpp : ark::InferenceProvider::ollama);
    return agent.run(std::move(message));
}

ark::Json run_file_operation(ark::Json payload) {
    const auto path = config_dir() / "file_system.json";
    const auto settings = std::filesystem::exists(path) ? ark::read_json(path) : ark::Json::object();
    ark::EpisodicMemory episodes(env("ARK_EPISODIC_DB", "data/episodic_memory.db"));
    const ark::FileSystemAgent worker(settings.value("allowed_directories", ark::Json::object()));
    return worker.run(ark::make_message("user", "file_system_agent", "FILES", std::move(payload)), episodes);
}

ark::Json run_coding(const ark::Transport& http, const ark::ResearchConfig& settings, ark::Json message,
                     const ark::AgentProfile& profile) {
    const auto sandbox = docker_sandbox(std::ref(http));
    const ark::CodingAgent worker(std::ref(http), settings.ollama_url, profile,
                                  [&](const auto& workspace) { return sandbox.run_tests(workspace); });
    return worker.run(std::move(message));
}

ark::Json run_evaluation(const ark::Transport& http, const ark::ResearchConfig& settings, ark::Json message,
                         const ark::AgentProfile& profile) {
    const ark::EvaluationAgent worker(std::ref(http), settings.ollama_url, profile);
    return worker.run(std::move(message));
}

void ensure_evaluation(ark::Json& dag) {
    auto dependencies = ark::Json::array();
    for (const auto& task : dag.at("tasks")) {
        if (task.at("agent") == "evaluation_agent") {
            return;
        }
        dependencies.push_back(task.at("task_id"));
    }
    dag["tasks"].push_back({{"task_id", "evaluate_" + ark::random_uuid()}, {"agent", "evaluation_agent"},
                            {"intent", "EVALUATE"}, {"instruction", "Score the completed artifact against the goal"},
                            {"depends_on", dependencies}, {"parallel_with", ark::Json::array()}});
}

ark::Json recall(const ark::ResearchConfig& settings, std::string_view goal) {
    ark::CurlTransport http;
    const ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
    ark::SemanticMemory semantic(std::ref(http), settings.qdrant_url, encoder, settings.semantic_db);
    const auto path = skills_path();
    std::unique_ptr<ark::ProceduralMemory> skills;
    if (std::filesystem::exists(path)) {
        skills = std::make_unique<ark::ProceduralMemory>(path, encoder);
    }
    return ark::memory_context("", goal, nullptr, semantic, skills.get());
}

void record_escalations(std::string_view task_id, const ark::Json& reports,
                        ark::EpisodicMemory* episodes = nullptr, ark::Redis* redis = nullptr,
                        std::string_view supervisor_version = "1.0.0") {
    if (reports.empty()) {
        return;
    }
    std::unique_ptr<ark::EpisodicMemory> fallback;
    if (!episodes) {
        fallback = std::make_unique<ark::EpisodicMemory>(env("ARK_EPISODIC_DB", "data/episodic_memory.db"));
        episodes = fallback.get();
    }
    for (const auto& report : reports) {
        episodes->log({{"task_id", task_id}, {"agent_name", "supervisor_agent"},
                       {"agent_version", "supervisor_agent@" + std::string(supervisor_version)}, {"intent", "SUPERVISE"},
                       {"summary", "Escalation: " + report.at("detail").get<std::string>()},
                       {"metadata", {{"event", "task_escalated"}, {"report", report}}}});
        if (redis) {
            audit_event(redis, {{"event", "task_escalated"}, {"task_id", task_id}, {"report", report}});
        }
    }
}

void emit(const ark::TaskEvents& events, std::string_view name, const ark::Json& report) {
    if (events) {
        events(name, report);
    }
}

ark::Response report_request(const ark::TaskEvents& events, ark::CurlTransport& http, const ark::Request& request,
                             const ark::Json& source) {
    if (!events) {
        return http(request);
    }
    auto report = source;
    const auto scheme = request.url.find("://");
    const auto end = request.url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    report["service"] = request.unix_socket.empty() ? request.url.substr(0, end) : "docker";
    report["method"] = request.method;
    emit(events, "tool_called", report);
    try {
        auto response = http(request);
        report["http_status"] = response.status;
        emit(events, "tool_result", report);
        if (request.method == "PUT" && request.url.ends_with("/points?wait=true")
            && response.status >= 200 && response.status < 300) {
            emit(events, "memory_written", {{"agent", source.at("agent")}, {"tier", "semantic"}});
        }
        return response;
    } catch (const std::exception&) {
        report["status"] = "FAILED";
        emit(events, "tool_result", report);
        throw;
    }
}

void attach_reflection(ark::Json& result, const ark::ResearchConfig& settings, std::string_view goal,
                       const ark::Json& attempts, ark::Redis* redis, ark::EpisodicMemory* episodes,
                       const ark::TaskEvents& events) {
    const ark::Json source{{"agent", "reflection_agent"}};
    emit(events, "agent_started", source);
    try {
        const auto profile = actor_profile("reflection_agent", settings.model, redis);
        const auto versions = result.at("agent_versions");
        result["agent_versions"]["reflection_agent"] = profile.version;
        std::unique_ptr<ark::EpisodicMemory> fallback;
        if (!episodes) {
            fallback = std::make_unique<ark::EpisodicMemory>(env("ARK_EPISODIC_DB", "data/episodic_memory.db"));
            episodes = fallback.get();
        }
        ark::CurlTransport http;
        const ark::Transport transport = [&](const auto& request) { return report_request(events, http, request, source); };
        ark::Embeddings encoder(transport, settings.ollama_url, settings.embedding_model);
        ark::ProceduralMemory skills(skills_path(), std::move(encoder));
        ark::ReflectionAgent review(transport, settings.ollama_url, profile);
        result["reflection"] = review.run({{"task_id", result.at("task_id")}, {"intent", result.at("intent")},
            {"goal", goal.substr(0, 2000)}, {"status", result.at("status")}, {"quality_score", result.at("quality_score")},
            {"attempts", attempts}, {"escalations", result.at("escalations")},
            {"agent_versions", versions}}, *episodes, skills);
        emit(events, "memory_written", {{"agent", "reflection_agent"}, {"tier", "episodic"}});
        if (result.at("reflection").contains("skill_id")) {
            emit(events, "memory_written", {{"agent", "reflection_agent"}, {"tier", "procedural"}});
        }
    } catch (const std::exception& error) {
        result["reflection"] = {{"status", "FAILED"}, {"error", error.what()}};
    }
    emit(events, "agent_done", {{"agent", "reflection_agent"}, {"status", result.at("reflection").at("status")}});
}

ark::Json dispatch(const ark::Json& dag, const ark::ResearchConfig& settings, std::string_view intent = "PLAN",
                   std::string parent_id = {}, ark::Json versions = ark::Json::object(), bool via_api = false,
                   const ark::TaskEvents& events = {}, bool include_native_tools = false) {
    if (parent_id.empty()) {
        parent_id = ark::make_message("user", "supervisor_agent", intent, ark::Json::object())
            .at("task_id").get<std::string>();
    }
    const auto redis_port = env("ARK_REDIS_PORT", via_api ? "6379" : "");
    const auto redis_db = std::stoi(env("ARK_REDIS_DB", "0"));
    const auto episode_path = env("ARK_EPISODIC_DB", via_api ? "data/episodic_memory.db" : "");
    const std::filesystem::path state_path = env("ARK_STATE_PATH", "STATE.md");
    const auto goal = dag.value("goal", "Saved task plan");
    const auto privacy = ark::TaskPrivacy::resolve(settings.privacy, dag);
    std::mutex state_mutex;
    int completed = 0;
    auto last = ark::Json::object();
    auto attempts = ark::Json::array();
    std::unique_ptr<ark::Redis> redis;
    std::unique_ptr<ark::EpisodicMemory> episodes;
    if (!redis_port.empty()) {
        redis = runtime_redis(std::stoi(redis_port), redis_db);
        audit_event(redis.get(), {{"event", "dispatch_start"}, {"task_id", parent_id}});
    }
    const auto profiles = [&] {
        std::map<std::string, ark::AgentProfile, std::less<>> resolved;
        for (const auto& layer : ark::order_tasks(dag, include_native_tools)) {
            for (const auto& task : layer) {
                if (task.agent != "memory_agent" && task.agent != "data_analysis_agent"
                    && task.agent != "scientific_report_generator"
                    && task.agent != "ml_engineer_agent"
                    && task.agent != "file_system_agent"
                    && task.agent != "capability_discovery_agent"
                    && task.agent != "github_agent"
                    && task.agent != "experiment_designer"
                    && task.agent != "devops_agent"
                    && task.agent != "hypothesis_engine"
                    && task.agent != "self_improvement_agent"
                    && task.agent != "materials_project_agent"
                    && !resolved.contains(task.agent)) {
                    const auto model = task.agent == "vision_agent" ? configured_model("ARK_VISION_MODEL", "vision") : settings.model;
                    resolved.emplace(task.agent, actor_profile(task.agent, model, redis.get()));
                }
            }
        }
        return resolved;
    }();
    for (const auto& [name, profile] : profiles) {
        versions[name] = profile.version;
    }
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "memory_agent";
        })) versions["memory_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "data_analysis_agent";
        })) versions["data_analysis_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "scientific_report_generator";
        })) versions["scientific_report_generator"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "ml_engineer_agent";
        })) versions["ml_engineer_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "file_system_agent";
        })) versions["file_system_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "capability_discovery_agent";
        })) versions["capability_discovery_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "github_agent";
        })) versions["github_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "experiment_designer";
        })) versions["experiment_designer"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "devops_agent";
        })) versions["devops_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "hypothesis_engine";
        })) versions["hypothesis_engine"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "self_improvement_agent";
        })) versions["self_improvement_agent"] = "1.0.0";
    if (std::any_of(dag.at("tasks").begin(), dag.at("tasks").end(), [](const auto& task) {
            return task.value("agent", "") == "materials_project_agent";
        })) versions["materials_project_agent"] = "1.0.0";
    if (events) {
        auto plan = ark::Json::array();
        for (const auto& task : dag.at("tasks")) {
            plan.push_back({{"task_id", task.at("task_id")}, {"agent", task.at("agent")},
                            {"depends_on", task.at("depends_on")}});
        }
        emit(events, "plan_ready", {{"tasks", std::move(plan)}});
    }
    if (!episode_path.empty()) {
        episodes = std::make_unique<ark::EpisodicMemory>(episode_path);
    }
    const auto [results, escalations, conflicts] = ark::supervise_plan(dag, [&](const ark::Task& task, const ark::Json& dependencies,
                                                                   int attempt, const ark::Json& feedback) {
        ark::CurlTransport http;
        const ark::Json source{{"agent", task.agent}, {"plan_task_id", task.id}, {"attempt", attempt}};
        emit(events, "agent_started", source);
        auto activity = source;
        activity["activity"] = "Executing " + task.intent;
        emit(events, "agent_thought", activity);
        const ark::Transport transport = [&](const auto& request) { return report_request(events, http, request, source); };
        auto message = ark::make_message("router", task.agent, task.intent, {
            {"instruction", task.instruction}, {"goal", goal}, {"privacy", privacy.enabled},
            {"airgap_mode", privacy.airgap},
        }, parent_id);
        message["memory_context"] = dag.value("memory_context", ark::Json::array());
        if (!dependencies.empty()) {
            message["memory_context"].push_back({{"tier", "dependencies"}, {"data", dependencies}});
        }
        message["retry_count"] = attempt - 1;
        if (feedback.contains("review")) {
            message["payload"]["review_feedback"] = feedback.at("review");
        }
        if (feedback.contains("output_conflicts")) {
            message["payload"]["output_conflicts"] = feedback.at("output_conflicts");
        }
        if (redis) {
            const auto worker_bus = runtime_redis(std::stoi(redis_port), redis_db);
            audit_event(worker_bus.get(), {{"event", "worker_start"}, {"task_id", message.at("task_id")},
                                            {"parent_task_id", parent_id}, {"agent", task.agent},
                                            {"plan_task_id", task.id}, {"attempt", attempt}});
        }
        ark::Json result;
        if (task.agent == "coding_agent") {
            if (dag.contains("coding_test_files")) {
                message["payload"]["test_files"] = dag.at("coding_test_files");
            } else if (feedback.contains("tests")) {
                message["payload"]["test_files"] = feedback.at("tests");
            }
            result = run_coding(transport, settings, std::move(message), profiles.at(task.agent));
        } else if (task.agent == "evaluation_agent") {
            message["payload"]["instruction"] = goal + "\n" + task.instruction;
            result = run_evaluation(transport, settings, std::move(message), profiles.at(task.agent));
            const auto target = result.at("output").value("evaluated_task", "");
            if (dependencies.contains(target) && dependencies.at(target).contains("agent_instance")) {
                research_pool.record_score(dependencies.at(target).at("agent_instance").get_ref<const std::string&>(),
                                           result.at("quality_score").get<double>());
            }
        } else if (task.agent == "web_search_agent") {
            result = run_web_search(transport, settings, std::move(message), profiles.at(task.agent));
        } else if (task.agent == "browser_agent") {
            result = run_browser_agent(transport, settings, std::move(message), profiles.at(task.agent),
                [&](std::string_view action, std::string_view status) {
                    auto report = source;
                    report["activity"] = "Browser " + std::string(action);
                    report["status"] = status;
                    emit(events, status == "STARTED" ? "tool_called" : "tool_result", report);
                });
        } else if (task.agent == "vision_agent") {
            result = run_vision_agent(transport, settings, std::move(message), profiles.at(task.agent));
        } else if (task.agent == "terminal_agent") {
            message["payload"]["command"] = task.instruction;
            message["payload"]["timeout_seconds"] = 30;
            result = ark::TerminalAgent(docker_sandbox(std::ref(http))).run(std::move(message));
            result["agent_version"] = "terminal_agent@" + profiles.at(task.agent).version;
        } else if (task.agent == "security_agent") {
            message["payload"] = task.payload;
            result = ark::SecurityAgent(docker_sandbox(std::ref(http))).run(std::move(message));
            result["agent_version"] = "security_agent@" + profiles.at(task.agent).version;
        } else if (task.agent == "document_agent") {
            message["payload"] = task.payload;
            const auto action = task.payload.at("action").get<std::string>();
            const auto extension = action == "write_docx" ? ".docx" : action == "write_xlsx" ? ".xlsx" : ".pdf";
            std::string encoded_id;
            constexpr char hex[] = "0123456789abcdef";
            for (const unsigned char c : task.id) {
                encoded_id.push_back(hex[c >> 4]);
                encoded_id.push_back(hex[c & 0x0f]);
            }
            const std::filesystem::path requested_root = env("ARK_DOCUMENT_OUTPUT_DIR", "data/documents");
            std::filesystem::create_directories(requested_root);
            const auto output_root = std::filesystem::canonical(requested_root);
            const auto target = output_root / ("ark-" + encoded_id + extension);
            if (std::filesystem::is_symlink(target)) throw std::invalid_argument("Document output target must not be a symlink");
            message["payload"]["path"] = target.string();
            result = ark::DocumentAgent().run(std::move(message));
            result["agent_version"] = "document_agent@" + profiles.at(task.agent).version;
        } else if (task.agent == "memory_agent") {
            const auto action = task.payload.at("action").get<std::string>();
            const std::filesystem::path path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
            ark::EpisodicMemory store(path);
            ark::Json output;
            if (action == "store_episode") {
                const auto record_id = store.log({{"task_id", task.id}, {"agent_name", "memory_agent"},
                    {"agent_version", "memory_agent@1.0.0"}, {"intent", "STORE"},
                    {"summary", task.payload.at("summary")}, {"metadata", task.payload.value("metadata", ark::Json::object())}});
                output = {{"summary", "Stored episodic memory record"}, {"record_id", record_id}};
            } else {
                auto records = store.search(task.payload.at("query").get<std::string>(), task.payload.value("limit", 10));
                output = {{"summary", "Found " + std::to_string(records.size()) + " episodic memory records"},
                          {"records", std::move(records)}};
            }
            result = { {"task_id", message.at("task_id")}, {"status", "DONE"}, {"intent", task.intent},
                {"agent_version", "memory_agent@1.0.0"}, {"quality_score", -1.0}, {"output", std::move(output)} };
        } else if (task.agent == "data_analysis_agent") {
            message["payload"] = task.payload;
            result = ark::analyze_csv(std::move(message));
            result["agent_version"] = "data_analysis_agent@1.0.0";
        } else if (task.agent == "scientific_report_generator") {
            message["payload"] = task.payload;
            const ark::Transport no_graph_access = [](const ark::Request&) -> ark::Response {
                throw std::logic_error("Saved-DAG scientific reports cannot query the citation graph");
            };
            ark::CitationGraph graph(no_graph_access, "http://127.0.0.1:7474");
            result = ark::ScientificReportGenerator(graph).run(std::move(message));
            result["agent_version"] = "scientific_report_generator@1.0.0";
        } else if (task.agent == "ml_engineer_agent") {
            result = { {"task_id", message.at("task_id")}, {"status", "DONE"}, {"intent", task.intent},
                {"agent_version", "ml_engineer_agent@1.0.0"}, {"quality_score", -1.0},
                {"output", ark::run_linear_regression_experiment(task.payload)} };
        } else if (task.agent == "file_system_agent") {
            result = run_file_operation(task.payload);
        } else if (task.agent == "capability_discovery_agent") {
            auto evidence = task.payload;
            if (evidence.size() == 1 && evidence.contains("question")) {
                const auto store = version_store(true);
                if (!store) throw std::runtime_error("Capability discovery requires a local agent registry");
                evidence["registry"] = ark::AgentRegistry(*store).all();
                const std::filesystem::path episode_path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
                evidence["reflections"] = std::filesystem::exists(episode_path)
                    ? ark::EpisodicMemory(episode_path).recent(500) : ark::Json::array();
                auto histories = ark::Json::object();
                const std::filesystem::path benchmark_dir = env("ARK_BENCHMARK_RESULTS", "benchmarks/arkbench/results");
                for (const auto& agent : evidence.at("registry")) {
                    if (agent.is_object() && agent.contains("name") && agent.at("name").is_string()) {
                        const auto name = agent.at("name").get<std::string>();
                        histories[name] = ark::benchmark_history(benchmark_dir, name);
                    }
                }
                evidence["benchmark_histories"] = std::move(histories);
            }
            const auto capability_map = ark::build_capability_map(
                evidence.value("registry", ark::Json::array()),
                evidence.value("reflections", ark::Json::array()),
                evidence.value("benchmark_histories", ark::Json::object()));
            const auto answer = ark::answer_capability_question(capability_map,
                task.payload.at("question").get_ref<const std::string&>());
            result = {{"task_id", message.at("task_id")}, {"status", "DONE"}, {"intent", task.intent},
                {"agent_version", "capability_discovery_agent@1.0.0"}, {"quality_score", -1.0},
                {"output", {{"summary", answer.at("reason")}, {"answer", answer}, {"capability_map", capability_map}}}};
        } else if (task.agent == "github_agent") {
            result = ark::GitHubAgent(transport, env("GH_TOKEN", env("GITHUB_TOKEN")))
                .run(ark::make_message("router", "github_agent", task.intent, task.payload, parent_id));
            result["agent_version"] = "github_agent@1.0.0";
        } else if (task.agent == "experiment_designer") {
            const auto llama = ark::configured_llama_url();
            const auto provider = llama.empty() ? ark::InferenceProvider::ollama : ark::InferenceProvider::llama_cpp;
            const auto model_url = llama.empty() ? settings.ollama_url : llama;
            const auto profile = ark::experiment_designer_profile(configured_model("ARK_MODEL", "general"));
            result = ark::ExperimentDesigner(transport, model_url, profile, provider)
                .run(ark::make_message("router", "experiment_designer", task.intent, task.payload, parent_id));
            result["agent_version"] = "experiment_designer@" + profile.version;
        } else if (task.agent == "devops_agent") {
            result = ark::DevOpsAgent(transport, env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"))
                .run(ark::make_message("router", "devops_agent", task.intent, task.payload, parent_id));
            result["agent_version"] = "devops_agent@1.0.0";
        } else if (task.agent == "hypothesis_engine") {
            ark::Embeddings encoder(transport, settings.ollama_url, settings.embedding_model);
            ark::SemanticMemory papers(transport, settings.qdrant_url, encoder, settings.semantic_db);
            const auto password = env("ARK_NEO4J_PASSWORD");
            ark::CitationGraph graph(transport, env("ARK_NEO4J_URL", "http://127.0.0.1:7474"),
                env("ARK_NEO4J_DATABASE", "neo4j"), env("ARK_NEO4J_USER", password.empty() ? "" : "neo4j"), password);
            const auto profile = ark::hypothesis_profile(configured_model("ARK_MODEL", "general"));
            result = ark::HypothesisEngine(transport, settings.ollama_url, profile, papers, graph, encoder)
                .run(ark::make_message("router", "hypothesis_engine", task.intent, task.payload, parent_id));
            result["agent_version"] = "hypothesis_engine@" + profile.version;
        } else if (task.agent == "self_improvement_agent") {
            const auto target = task.payload.at("target_agent").get<std::string>();
            const auto redis = version_store();
            const auto current = actor_profile(target, settings.model, redis.get());
            const std::filesystem::path episode_path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
            const auto failures = std::filesystem::exists(episode_path)
                ? ark::EpisodicMemory(episode_path).pending_improvements(50) : ark::Json::array();
            const auto llama = ark::configured_llama_url();
            const auto provider = llama.empty() ? ark::InferenceProvider::ollama : ark::InferenceProvider::llama_cpp;
            const auto model_url = llama.empty() ? settings.ollama_url : llama;
            const auto proposal = ark::SelfImprovementAgent(transport, model_url, settings.model, provider)
                .propose(target, current, failures);
            result = {{"task_id", message.at("task_id")}, {"status", "DONE"}, {"intent", task.intent},
                {"agent_version", "self_improvement_agent@1.0.0"}, {"quality_score", -1.0},
                {"output", {{"summary", proposal.value("status", "NO_ACTION")}, {"proposal", proposal}}}};
        } else if (task.agent == "materials_project_agent") {
            const auto data = ark::MaterialsProjectClient(transport, env("MP_API_KEY"),
                env("MP_API_ENDPOINT", "https://api.materialsproject.org")).summaries(
                    task.payload.at("material_ids").get<std::vector<std::string>>(),
                    task.payload.value("fields", std::vector<std::string>{}), task.payload.value("limit", 100));
            result = {{"task_id", message.at("task_id")}, {"status", "DONE"}, {"intent", task.intent},
                {"agent_version", "materials_project_agent@1.0.0"}, {"quality_score", -1.0},
                {"output", {{"summary", "Retrieved " + std::to_string(data.size()) + " materials summaries"},
                             {"materials", data}}}};
        } else {
            result = research_pool.run([&](std::string_view instance) {
                auto assigned = source;
                assigned["agent_instance"] = instance;
                emit(events, "agent_assigned", assigned);
                return ark::ResearchAgent(transport, settings, profiles.at(task.agent)).run(std::move(message));
            });
        }
        result["retry_count"] = attempt - 1;
        const auto& output = result.at("output");
        const auto summary = output.value("summary", output.value("error_message", std::string{}));
        {
            const std::lock_guard lock(state_mutex);
            last = {{"agent", task.agent}, {"task", task.id}, {"version", result.at("agent_version")}};
            if (result.at("quality_score") != -1.0) {
                last["score"] = result.at("quality_score");
            }
            if (attempts.size() < 60) {
                attempts.push_back({{"plan_task_id", task.id}, {"agent", task.agent}, {"intent", task.intent},
                                    {"attempt", attempt}, {"status", result.at("status")},
                                    {"quality_score", result.at("quality_score")}, {"summary", summary.substr(0, 500)}});
                if (output.contains("repairs")) {
                    attempts.back()["repairs"] = output.at("repairs");
                }
            }
            ark::write_state(state_path, goal, parent_id, "RUNNING", privacy.enabled, ++completed, last);
        }
        if (episodes) {
            episodes->log({
                {"task_id", result.at("task_id")}, {"agent_name", task.agent},
                {"agent_version", result.at("agent_version")}, {"intent", task.intent},
                {"summary", summary},
                {"quality_score", result.at("quality_score") == -1.0 ? ark::Json(nullptr) : result.at("quality_score")},
                {"metadata", {{"parent_task_id", parent_id}, {"status", result.at("status")},
                              {"plan_task_id", task.id}, {"attempt", attempt}}},
            });
            emit(events, "memory_written", {{"agent", task.agent}, {"tier", "episodic"}});
        }
        activity.erase("activity");
        activity["status"] = result.at("status");
        if (result.contains("agent_instance")) activity["agent_instance"] = result.at("agent_instance");
        emit(events, "agent_done", activity);
        return result;
    }, [&](std::string_view name) {
        if (name == "memory_agent" || name == "data_analysis_agent"
            || name == "scientific_report_generator" || name == "ml_engineer_agent"
            || name == "file_system_agent" || name == "capability_discovery_agent"
            || name == "github_agent" || name == "experiment_designer" || name == "hypothesis_engine") return true;
        if (name == "self_improvement_agent" || name == "materials_project_agent") return true;
        if (name == "devops_agent") return true;
        if (!profiles.contains(name)) {
            return false;
        }
        if (!redis) {
            return true;
        }
        try {
            auto registry = ark::AgentRegistry(*redis);
            const auto record = registry.get(name);
            return !record.is_null() && record.at("status") != "OFFLINE" && registry.heartbeat(name);
        } catch (const ark::RedisUnavailable&) { return true; }
    }, 4, [&](const ark::Json& research, const ark::Json& coding) {
        ark::CurlTransport http;
        const ark::Json source{{"agent", "supervisor_agent"}};
        return ark::review_output_conflict([&](const auto& request) { return report_request(events, http, request, source); },
                                           settings.ollama_url, env("ARK_EVAL_MODEL", settings.model), research, coding);
    }, include_native_tools);
    for (auto report : conflicts) {
        report["task_id"] = parent_id;
        if (report.contains("case_id")) record_conflict(report.at("case_id").get<std::string>(), report);
        emit(events, "conflict_detected", report);
        audit_event(redis.get(), {{"event", "conflict_detected"}, {"task_id", parent_id}, {"report", report}});
    }
    record_escalations(parent_id, escalations, episodes.get(), redis.get(),
                       versions.value("supervisor_agent", "1.0.0"));
    const bool done = std::all_of(results.begin(), results.end(), [](const auto& result) {
        return result.at("status") == "DONE";
    });
    ark::Json quality;
    for (const auto& result : results) {
        if (result.value("intent", "") == "EVALUATE" && result.contains("quality_score")) {
            const auto score = result.at("quality_score").get<double>();
            quality = quality.is_null() ? score : std::min(quality.get<double>(), score);
        }
    }
    ark::write_state(state_path, goal, parent_id, done ? "DONE" : "FAILED", privacy.enabled,
                     static_cast<int>(results.size()), last);
    ark::WorkingMemory working(redis.get());
    working.set(parent_id, "results", results);
    working.finish(parent_id);
    emit(events, "memory_written", {{"tier", "working"}});
    audit_event(redis.get(), {{"event", "dispatch_done"}, {"task_id", parent_id}, {"status", done ? "DONE" : "FAILED"}});
    ark::Json result{{"task_id", parent_id}, {"intent", intent}, {"status", done ? "DONE" : "FAILED"},
                     {"dag", dag}, {"results", results}, {"quality_score", quality}, {"escalations", escalations},
                     {"agent_versions", versions}, {"output_conflicts", conflicts}};
    attach_reflection(result, settings, goal, attempts, redis.get(), episodes.get(), events);
    return result;
}

ark::Json run_goal(std::string_view goal, const ark::ResearchConfig& settings, const ark::Json& trusted_tests,
                   bool privacy, bool airgap, std::string task_id, bool via_api, const ark::TaskEvents& events,
                   std::string_view followup = {}) {
    if (!trusted_tests.is_object()) {
        throw std::invalid_argument("Trusted tests must be a JSON object mapping names to file contents");
    }
    const auto task_privacy = ark::TaskPrivacy::resolve(settings.privacy, {{"privacy", privacy}, {"airgap_mode", airgap}});
    ark::CurlTransport http;
    const auto redis = version_store(via_api);
    const auto supervisor = actor_profile("supervisor_agent", settings.model, redis.get());
    const auto plan_profile = actor_profile("planner_agent", settings.model, redis.get());
    std::string_view owner = "supervisor_agent";
    const ark::Planner planner([&](const auto& request) { return report_request(events, http, request, {{"agent", owner}}); },
                               settings.ollama_url, supervisor, plan_profile, env("ARK_GROQ_MODEL"),
                               task_privacy.enabled ? std::string{} : env("ARK_GROQ_API_KEY"));
    ark::Json versions{{"supervisor_agent", supervisor.version}};
    auto lower_goal = std::string(goal);
    std::transform(lower_goal.begin(), lower_goal.end(), lower_goal.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool explicit_research = lower_goal.starts_with("research ") || lower_goal.find("arxiv:") != std::string::npos;
    const bool has_browser_site = ark::browser_start_url(goal).has_value();
    const bool explicit_browse = has_browser_site && (lower_goal.starts_with("browse ") || lower_goal.starts_with("navigate ")
        || lower_goal.starts_with("visit ") || lower_goal.starts_with("open "));
    std::string intent = "SUPERVISE";
    ark::Json dag;
    for (int attempt = 1; ; ++attempt) {
        try {
            owner = "supervisor_agent";
            emit(events, "agent_started", {{"agent", owner}});
            intent = explicit_browse ? "BROWSE" : planner.classify(goal);
            emit(events, "agent_done", {{"agent", owner}, {"intent", intent}});
            if (intent == "BROWSE") {
                dag["tasks"] = ark::Json::array({{{"task_id", "t1"},
                    {"agent", has_browser_site ? "browser_agent" : "web_search_agent"},
                    {"intent", "BROWSE"}, {"instruction", goal}, {"depends_on", ark::Json::array()},
                    {"parallel_with", ark::Json::array()}}});
                break;
            }
            if (intent != "RESEARCH" && intent != "SEARCH" && intent != "CODE" && intent != "EXECUTE"
                && intent != "EVALUATE"
                && intent != "DESIGN_EXPERIMENT" && intent != "HYPOTHESIZE" && intent != "MATERIALS_QUERY"
                && intent != "PROPOSE_IMPROVEMENT" && intent != "MEMORY" && intent != "SECURITY"
                && intent != "DISCOVER" && intent != "GITHUB" && intent != "DOCUMENT") {
                throw std::invalid_argument("No native worker available for intent " + intent);
            }
            const bool include_native_tools = intent == "EXECUTE" || intent == "DESIGN_EXPERIMENT"
                || intent == "HYPOTHESIZE" || intent == "MATERIALS_QUERY" || intent == "PROPOSE_IMPROVEMENT"
                || intent == "SECURITY" || intent == "DISCOVER" || intent == "GITHUB" || intent == "DOCUMENT";
            const auto agents = intent == "EXECUTE" ? std::vector<std::string>{"terminal_agent"}
                : intent == "EVALUATE" ? std::vector<std::string>{"evaluation_agent"}
                : intent == "DESIGN_EXPERIMENT" ? std::vector<std::string>{"experiment_designer"}
                : intent == "HYPOTHESIZE" ? std::vector<std::string>{"hypothesis_engine"}
                : intent == "MATERIALS_QUERY" ? std::vector<std::string>{"materials_project_agent"}
                : intent == "PROPOSE_IMPROVEMENT" ? std::vector<std::string>{"self_improvement_agent"}
                : intent == "MEMORY" ? std::vector<std::string>{"memory_agent"}
                : intent == "SECURITY" ? std::vector<std::string>{"security_agent"}
                : intent == "DISCOVER" ? std::vector<std::string>{"capability_discovery_agent"}
                : intent == "GITHUB" ? std::vector<std::string>{"github_agent"}
                : intent == "DOCUMENT" ? std::vector<std::string>{"document_agent"}
                : intent == "CODE" && !explicit_research ? std::vector<std::string>{"coding_agent"}
                : intent == "SEARCH" ? std::vector<std::string>{"research_agent"}
                : std::vector<std::string>{"research_agent", "coding_agent"};
            auto context = (intent == "DESIGN_EXPERIMENT" || intent == "HYPOTHESIZE" || intent == "MATERIALS_QUERY"
                || intent == "PROPOSE_IMPROVEMENT" || intent == "MEMORY" || intent == "SECURITY"
                || intent == "DISCOVER" || intent == "GITHUB" || intent == "DOCUMENT")
                ? ark::Json::array() : recall(settings, goal);
            versions["planner_agent"] = plan_profile.version;
            const auto instruction = std::string(goal) + std::string(followup);
            owner = "planner_agent";
            emit(events, "agent_started", {{"agent", owner}});
            dag = planner.plan(instruction, agents, context, include_native_tools);
            emit(events, "agent_done", {{"agent", owner}, {"status", "DONE"}});
            dag["memory_context"] = std::move(context);
            break;
        } catch (const std::exception& error) {
            const bool retryable = dynamic_cast<const ark::TransientHttpError*>(&error) != nullptr;
            if (retryable && attempt < ark::supervisor_attempt_limit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                continue;
            }
            if (task_id.empty()) {
                task_id = ark::make_message("user", "supervisor_agent", "SUPERVISE", ark::Json::object())
                    .at("task_id").get<std::string>();
            }
            const auto reports = ark::Json::array({{{"plan_task_id", "planning"}, {"agent", "supervisor_agent"},
                {"attempts", attempt}, {"last_status", retryable ? "RETRY" : "FAILED"}, {"detail", error.what()}}});
            record_escalations(task_id, reports, nullptr, redis.get(), supervisor.version);
            ark::write_state(env("ARK_STATE_PATH", "STATE.md"), goal, task_id, "FAILED", task_privacy.enabled,
                             0, ark::Json::object());
            ark::Json result{{"task_id", task_id}, {"intent", intent}, {"status", "FAILED"},
                             {"results", ark::Json::object()}, {"quality_score", nullptr}, {"escalations", reports},
                             {"agent_versions", versions}};
            attach_reflection(result, settings, goal, ark::Json::array(), redis.get(), nullptr, events);
            return result;
        }
    }
    ensure_evaluation(dag);
    dag["goal"] = goal;
    dag["privacy"] = task_privacy.enabled;
    dag["airgap_mode"] = task_privacy.airgap;
    if (!trusted_tests.empty()) {
        dag["coding_test_files"] = trusted_tests;
    }
    return dispatch(dag, settings, intent, std::move(task_id), versions, via_api, events,
                    intent == "EXECUTE");
}

ark::Json run_task(std::string_view goal, const ark::ResearchConfig& settings, const ark::Json& trusted_tests,
                   bool privacy, bool airgap, const std::optional<std::string>& until, int max_iterations,
                   std::string task_id = {}, const ark::TaskEvents& events = {}) {
    const bool via_api = !task_id.empty();
    if (!until) {
        return run_goal(goal, settings, trusted_tests, privacy, airgap, std::move(task_id), via_api, events);
    }
    const ark::DoneCondition condition(*until);
    if (task_id.empty()) {
        task_id = ark::make_message("user", "supervisor_agent", "SUPERVISE", ark::Json::object())
            .at("task_id").get<std::string>();
    }
    const auto policy = ark::TaskPrivacy::resolve(settings.privacy, {{"privacy", privacy}, {"airgap_mode", airgap}});
    auto previous = ark::Json::object();
    ark::Json result;
    for (int iteration = 1; iteration <= max_iterations; ++iteration) {
        const auto followup = "\nCompletion condition: " + condition.expression()
            + "\nMeasured values from the previous iteration: " + previous.dump();
        result = run_goal(goal, settings, trusted_tests, privacy, airgap, task_id, via_api, events, followup);
        previous = ark::measured_metrics(result);
        const bool met = condition.evaluate(previous) && result.at("status") == "DONE";
        result["workflow_status"] = result.at("status");
        result["done_condition"] = {{"expression", condition.expression()}, {"met", met}, {"metrics", previous},
                                    {"iteration", iteration}, {"max_iter", max_iterations}};
        if (met) {
            result["status"] = "DONE";
        } else if (iteration == max_iterations) {
            result["status"] = "FAILED";
        }
        ark::write_state(env("ARK_STATE_PATH", "STATE.md"), goal, task_id,
                         met || iteration == max_iterations ? result.at("status").get<std::string>() : "RUNNING",
                         policy.enabled, iteration, {{"score", previous.value("quality_score", ark::Json("n/a"))}},
                         result.at("done_condition"));
        if (met) {
            break;
        }
    }
    return result;
}

int print_run(const ark::Json& result) {
    std::cout << result.dump(2) << '\n';
    return result.at("status") == "DONE" ? 0 : 1;
}

int infer() {
    const auto input = ark::Json::parse(std::cin);
    const auto llama = ark::configured_llama_url();
    const auto role = input.value("role", "general");
    const char* override = role == "general" ? "ARK_MODEL" : role == "code" ? "ARK_CODE_MODEL"
        : role == "vision" ? "ARK_VISION_MODEL" : nullptr;
    if (!override) throw std::invalid_argument("Select the general, code, or vision inference role");
    const auto model = input.contains("model") ? input.at("model").get<std::string>() : configured_model(override, role.c_str());
    ark::CurlTransport http;
    const ark::LocalInference backend(std::ref(http), llama.empty() ? config(false).ollama_url : llama,
        llama.empty() ? ark::InferenceProvider::ollama : ark::InferenceProvider::llama_cpp);
    ark::TextStream stream;
    if (input.value("stream", false)) stream = [](std::string_view text) { std::cout << text << std::flush; };
    const auto tokens = input.value("max_tokens", ark::Json(2048));
    if (!tokens.is_number_integer() || tokens.get<double>() < 1 || tokens.get<double>() > 8192) {
        throw std::invalid_argument("Inference max_tokens must be an integer from 1 to 8192");
    }
    const auto result = backend.generate(model, input.at("messages"), input.value("schema", ark::Json(nullptr)),
                                         tokens.get<int>(), input.value("temperature", 0.0), stream);
    if (stream) std::cout << '\n';
    else std::cout << ark::Json{{"model", model}, {"text", result.text}, {"finish_reason", result.finish_reason},
                               {"tokens", result.tokens}, {"tokens_per_second", result.tokens_per_second}}.dump(2) << '\n';
    return result.text.empty() ? 1 : 0;
}

int inference_benchmark(const std::filesystem::path& suite, const std::filesystem::path& output) {
    if (std::filesystem::weakly_canonical(suite) == std::filesystem::weakly_canonical(output)) {
        throw std::invalid_argument("Inference report path must differ from the suite path");
    }
    const auto llama_url = ark::configured_llama_url();
    const auto cases = ark::read_json(suite);
    if (llama_url.empty() || !cases.is_array() || cases.empty()) {
        throw std::invalid_argument("Inference benchmark requires ARK_LLAMA_URL and a nonempty coding suite");
    }
    const auto llama_model = configured_model("ARK_CODE_MODEL", "code");
    const auto ollama_model = env("ARK_BENCH_OLLAMA_MODEL", llama_model);
    const auto profile = ark::coding_profile(llama_model);
    ark::CurlTransport http;
    const ark::LocalInference llama(std::ref(http), llama_url, ark::InferenceProvider::llama_cpp);
    const ark::LocalInference ollama(std::ref(http), config(false).ollama_url, ark::InferenceProvider::ollama);
    const auto warm = ark::Json::array({{{"role", "user"}, {"content", "Write one sentence about unit tests."}}});
    ollama.generate(ollama_model, warm, nullptr, 32);
    llama.generate(llama_model, warm, nullptr, 32);
    const ark::Json schema{{"type", "object"}, {"properties", {{"filename", {{"type", "string"}}},
        {"source", {{"type", "string"}}}}}, {"required", {"filename", "source"}}, {"additionalProperties", false}};
    auto rows = ark::Json::array();
    double ollama_speed = 0, llama_speed = 0, ollama_latency = 0, llama_latency = 0;
    for (const auto& entry : cases) {
        auto goal = entry.at("payload");
        goal["previous_files"] = goal.at("repair_files");
        goal["locked_tests"] = goal.at("test_files");
        goal.erase("repair_files");
        goal.erase("test_files");
        const auto messages = ark::Json::array({{{"role", "system"}, {"content", profile.prompts.at("system")}},
                                                {{"role", "user"}, {"content", goal.dump()}}});
        const auto measure = [&](const ark::LocalInference& engine, const std::string& model, double& speed, double& latency) {
            const auto begin = std::chrono::steady_clock::now();
            const auto result = engine.generate(model, messages, schema, 240);
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            if (result.text.empty() || result.tokens_per_second <= 0) throw std::runtime_error("Inference server returned no timing evidence");
            speed += result.tokens_per_second;
            latency += ms;
            const auto artifact = ark::Json::parse(result.text, nullptr, false);
            return ark::Json{{"latency_ms", ms}, {"tokens", result.tokens}, {"tokens_per_second", result.tokens_per_second},
                {"structured_output", artifact.is_object() && artifact.contains("source") && artifact.contains("filename")}};
        };
        auto via_ollama = measure(ollama, ollama_model, ollama_speed, ollama_latency);
        auto via_llama = measure(llama, llama_model, llama_speed, llama_latency);
        rows.push_back({{"id", entry.at("id")}, {"ollama", std::move(via_ollama)}, {"llama_cpp", std::move(via_llama)}});
    }
    const ark::Json result{{"ollama_model", ollama_model}, {"llama_model", llama_model}, {"cases", std::move(rows)},
        {"ollama_avg_tokens_per_second", ollama_speed / cases.size()}, {"llama_avg_tokens_per_second", llama_speed / cases.size()},
        {"ollama_avg_latency_ms", ollama_latency / cases.size()}, {"llama_avg_latency_ms", llama_latency / cases.size()},
        {"generation_speed_change_percent", 100 * (llama_speed / ollama_speed - 1)}};
    ark::write_json(output, result);
    std::cout << result.dump(2) << '\n';
    return 0;
}

int repair_benchmark(const std::filesystem::path& suite, const std::filesystem::path& report_path) {
    if (std::filesystem::weakly_canonical(suite) == std::filesystem::weakly_canonical(report_path)) {
        throw std::invalid_argument("Repair report path must differ from the suite path");
    }
    const auto cases = ark::read_json(suite);
    if (!cases.is_array() || cases.empty()) throw std::invalid_argument("Repair suite requires cases");
    const auto settings = config(false);
    const auto redis = version_store();
    const auto profile = actor_profile("coding_agent", "", redis.get());
    ark::CurlTransport http;
    const auto sandbox = docker_sandbox(std::ref(http));
    const ark::CodingAgent agent(std::ref(http), settings.ollama_url, profile,
                                 [&](const auto& workspace) { return sandbox.run_tests(workspace); });
    ark::Json report{{"model", profile.model}, {"max_repairs", 3}, {"total", cases.size()},
                     {"fixed", 0}, {"cases", ark::Json::array()}, {"by_type", ark::Json::object()}};
    for (const auto& entry : cases) {
        const auto type = entry.at("type").get<std::string>();
        auto& group = report["by_type"][type];
        if (group.is_null()) group = {{"total", 0}, {"fixed", 0}};
        const auto start = std::chrono::steady_clock::now();
        const auto result = agent.run(ark::make_message("benchmark", "coding_agent", "CODE", entry.at("payload")));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        const auto& output = result.at("output");
        const auto baseline = output.value("initial_test_result", ark::Json::object());
        const bool valid_baseline = baseline.value("exit_code", 0) != 0 && !baseline.value("timed_out", false);
        const bool fixed = valid_baseline && result.at("status") == "DONE" && output.at("repairs") > 0;
        auto row = ark::Json{{"id", entry.at("id")}, {"type", type}, {"fixed", fixed},
                             {"valid_baseline", valid_baseline}, {"agent_version", result.at("agent_version")},
                             {"repairs", output.value("repairs", -1)}, {"elapsed_ms", elapsed.count()}};
        if (output.contains("error_message")) row["error"] = output.at("error_message");
        report["cases"].push_back(std::move(row));
        report["fixed"] = report.at("fixed").get<int>() + fixed;
        group["total"] = group.at("total").get<int>() + 1;
        group["fixed"] = group.at("fixed").get<int>() + fixed;
        group["rate"] = group.at("fixed").get<double>() / group.at("total").get<double>();
        report["rate"] = report.at("fixed").get<double>() / cases.size();
        ark::write_json(report_path, report);
    }
    std::cout << report.dump(2) << '\n';
    return report.at("rate") >= 0.7 ? 0 : 1;
}

int reflection_study(const std::filesystem::path& suite, const std::filesystem::path& report_path) {
    if (std::filesystem::weakly_canonical(suite) == std::filesystem::weakly_canonical(report_path)) {
        throw std::invalid_argument("Reflection report path must differ from the study suite path");
    }
    const auto settings = config();
    const auto redis = version_store();
    const auto coding = actor_profile("coding_agent", settings.model, redis.get());
    const auto reflection = actor_profile("reflection_agent", settings.model, redis.get());
    ark::CurlTransport http;
    const auto sandbox = docker_sandbox(std::ref(http));
    const ark::CodingAgent coder(std::ref(http), settings.ollama_url, coding,
                                 [&](const auto& workspace) { return sandbox.run_tests(workspace); });
    const ark::ReflectionAgent reflector(std::ref(http), settings.ollama_url, reflection);
    ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
    auto report = ark::run_reflection_study(ark::read_json(suite), coder, reflector, sandbox, std::move(encoder),
        [&](const auto& partial) { ark::write_json(report_path, partial); });
    report["coding_model"] = coding.model;
    report["reflection_model"] = reflection.model;
    ark::write_json(report_path, report);
    std::cout << report.at("summary").dump(2) << '\n';
    return 0;
}

int benchmark(const std::filesystem::path& suite_path, const std::filesystem::path& report_path) {
    if (!report_path.empty() && std::filesystem::weakly_canonical(suite_path) == std::filesystem::weakly_canonical(report_path)) {
        throw std::invalid_argument("Benchmark report path must differ from the suite path");
    }
    const auto suite = ark::read_json(suite_path);
    const auto settings = config();
    ark::CurlTransport http;
    const auto sandbox = docker_sandbox(std::ref(http));
    const auto report = ark::run_benchmark(suite,
        [&](const ark::Json& dag) { return dispatch(dag, settings, "BENCHMARK"); },
        [&](const ark::SandboxWorkspace& workspace) { return sandbox.run_tests(workspace); },
        [&](const ark::Json& partial) {
            if (!report_path.empty()) {
                ark::write_json(report_path, partial);
            }
        }, [&](const ark::Json& task) {
            const auto redis = task.at("kind") == "browser" ? version_store() : nullptr;
            return ark::run_tool_case(task, std::ref(http), settings.ollama_url,
                actor_profile("browser_agent", settings.model, redis.get()),
                {.executable = env("ARK_BROWSER_BINARY", "chromium"), .privacy = settings.privacy}, sandbox);
        });
    if (!report_path.empty()) {
        ark::write_json(report_path, report);
    }
    std::cout << report.dump(2, ' ', false, ark::Json::error_handler_t::replace) << '\n';
    const auto& totals = report.at("summary");
    return totals.at("scored") == totals.at("total") ? 0 : 1;
}

int self_improve_agent(std::string_view target, const std::filesystem::path& suite_path,
                       const std::filesystem::path& report_path) {
    if (std::filesystem::weakly_canonical(suite_path) == std::filesystem::weakly_canonical(report_path))
        throw std::invalid_argument("Self-improvement report path must differ from the benchmark suite");
    if (target != "research_agent" && target != "coding_agent")
        throw std::invalid_argument("Self-improvement A/B runs currently support research_agent or coding_agent");
    const auto suite = ark::read_json(suite_path);
    if (!suite.contains("tasks") || !suite.at("tasks").is_array() || suite.at("tasks").size() != 10)
        throw std::invalid_argument("Self-improvement requires exactly 10 ARKBench tasks");
    const auto expected_kind = target == "research_agent" ? "research" : "coding";
    for (const auto& task : suite.at("tasks")) {
        if (task.value("kind", "") != expected_kind)
            throw std::invalid_argument("Every A/B case must exercise the target agent using kind " + std::string(expected_kind));
    }

    const auto settings = config();
    const auto redis = version_store(true, true);
    const auto current = actor_profile(target, settings.model, redis.get());
    const auto episodic_path = std::filesystem::path(env("ARK_EPISODIC_DB", "data/episodic_memory.db"));
    const auto logs = std::filesystem::exists(episodic_path)
        ? ark::EpisodicMemory(episodic_path).pending_improvements(50) : ark::Json::array();
    ark::CurlTransport http;
    const auto llama_url = ark::configured_llama_url();
    const bool use_llama = !llama_url.empty();
    ark::SelfImprovementAgent improver(std::ref(http), use_llama ? llama_url : settings.ollama_url,
        settings.model, use_llama ? ark::InferenceProvider::llama_cpp : ark::InferenceProvider::ollama);
    auto proposal = improver.propose(target, current, logs);
    if (proposal.value("status", "") == "NO_ACTION") {
        ark::write_json(report_path, proposal);
        std::cout << proposal.dump(2) << '\n';
        return 0;
    }

    const auto sandbox = docker_sandbox(std::ref(http));
    const auto candidate_prompt = proposal.at("candidate_system_prompt").get<std::string>();
    const auto comparison = improver.compare(current, candidate_prompt, suite.at("tasks"),
        [&](const ark::AgentProfile& tested_profile, const ark::Json& task) {
            auto one_case = suite;
            one_case["tasks"] = ark::Json::array({task});
            PromptOverrideGuard override_prompt(std::string(target), tested_profile.prompts.at("system"));
            const auto result = ark::run_benchmark(one_case,
                [&](const ark::Json& dag) { return dispatch(dag, settings, "SELF_IMPROVE", {}, ark::Json::object(), true); },
                [&](const ark::SandboxWorkspace& workspace) { return sandbox.run_tests(workspace); });
            const auto score = result.at("summary").at("mean_score");
            if (!score.is_number()) throw std::runtime_error("A/B benchmark case returned no measured score");
            return score.get<double>();
        });
    const auto promotion = improver.promote_if_better(target, current, candidate_prompt, comparison,
        [&](const ark::AgentProfile& candidate, double score) {
            return ark::AgentVersioner(*redis).bump(target, "patch", {
                {"prompts", {{"system", candidate.prompts.at("system")}}}, {"benchmark_score", score},
                {"changed_by", "self_improvement_agent"},
                {"change_summary", "Prompt passed a ten-case A/B improvement gate"}});
        });
    const ark::Json report{{"proposal", proposal}, {"comparison", comparison}, {"promotion", promotion}};
    ark::write_json(report_path, report);
    std::cout << report.dump(2) << '\n';
    return 0;
}

int mcp_stdio(const std::filesystem::path& executable) {
    ark::serve_mcp_stdio(std::cin, std::cout, [&](std::string_view name, const ark::Json& args) -> ark::Json {
        if (name == "ark_task" || name == "ark_research" || name == "ark_code") {
            const bool privacy = args.value("privacy", false);
            std::string goal;
            if (name == "ark_task") goal = args.at("goal").get<std::string>();
            else if (name == "ark_research") goal = "Research " + args.at("topic").get<std::string>();
            else {
                const auto language = args.value("language", "python");
                if (language != "python") throw std::invalid_argument("ARK Coding Agent currently accepts Python tasks only");
                goal = "Write and test " + language + " code: " + args.at("instruction").get<std::string>();
            }
            if (goal.empty() || goal.size() > 8192) throw std::invalid_argument("ARK task text must be 1–8192 bytes");
            return run_task(goal, config(), ark::Json::object(), privacy, false, std::nullopt, 10);
        }
        if (name == "ark_security") {
            ark::CurlTransport http;
            const ark::SecurityAgent agent(ark::DockerSandbox(std::ref(http), env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"),
                env("ARK_SECURITY_IMAGE", "ark-security:local")));
            auto message = agent.run(ark::make_message("mcp", "security_agent", "SECURITY", args));
            if (message.value("status", "FAILED") != "DONE")
                throw std::runtime_error(message.value("output", ark::Json::object()).value("error_message", "Security scan failed"));
            return message;
        }
        if (name == "ark_sage") {
            const auto loop_id = args.at("loop_id").get<std::string>();
            if (loop_id.empty() || loop_id.size() > 64) throw std::invalid_argument("SAGE loop_id must be 1–64 characters");
            const auto serialized = run_child_json(executable, {"sage-materials", loop_id}, args);
            const auto result = ark::Json::parse(serialized, nullptr, false);
            if (!result.is_object() || result.value("status", "") != "DONE")
                throw std::runtime_error("SAGE subprocess did not return a completed result");
            return result;
        }
        throw std::invalid_argument("Unknown ARK MCP tool");
    });
    return 0;
}

void api_command(std::string_view path, std::string_view method = "GET") {
    ark::CurlTransport http;
    const auto port = std::stoi(env("ARK_API_PORT", "8000"));
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid local API port");
    }
    const auto response = http({"http://127.0.0.1:" + std::to_string(port) + std::string(path),
                                std::string(method)});
    response.require_success("Native API");
    std::cout << ark::Json::parse(response.body).dump(2) << '\n';
}

int manage_versions(std::string_view command, std::string_view agent, char** arguments) {
    const auto redis = version_store(true, true);
    ark::AgentVersioner versioner(*redis);
    ark::Json result;
    if (command == "versions") {
        result = versioner.history(agent);
    } else if (command == "version-compare") {
        result = versioner.compare(agent, arguments[0], arguments[1]);
    } else if (command == "version-rollback") {
        result = versioner.rollback(agent, arguments[0]);
    } else {
        const auto change = ark::read_json(arguments[1]);
        if (!versioner.initialized(agent)) {
            actor_profile(agent, agent == "coding_agent" ? "" : configured_model("ARK_MODEL", "general"), redis.get());
        }
        result = versioner.bump(agent, arguments[0], change);
    }
    std::cout << result.dump(2) << '\n';
    return 0;
}

ark::Json memory_tier(std::string_view tier, std::string_view query, std::string_view task_id, int limit) {
    if (tier == "working" && !task_id.empty()) {
        const auto redis = version_store(true);
        return ark::WorkingMemory(redis.get()).all(task_id);
    }
    if (tier == "episodic") {
        const std::filesystem::path path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
        if (!std::filesystem::exists(path)) {
            return ark::Json::array();
        }
        ark::EpisodicMemory episodes(path);
        return query.empty() ? episodes.recent(limit) : episodes.search(query, limit);
    }
    if (tier == "working") {
        return ark::Json::array();
    }
    const auto settings = config(false);
    ark::CurlTransport http;
    ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
    if (tier == "semantic") {
        ark::SemanticMemory semantic(std::ref(http), settings.qdrant_url, std::move(encoder), settings.semantic_db);
        return query.empty() ? semantic.by_metadata(ark::Json::object(), limit) : semantic.retrieve(query, limit);
    }
    if (tier == "procedural" && std::filesystem::exists(skills_path())) {
        ark::ProceduralMemory skills(skills_path(), std::move(encoder));
        return query.empty() ? skills.all(limit) : skills.retrieve(query, limit, std::max(limit, 20));
    }
    return ark::Json::array();
}

int serve(int port) {
    const auto redis_port = std::stoi(env("ARK_REDIS_PORT", "6379"));
    const auto redis_db = std::stoi(env("ARK_REDIS_DB", "0"));
    ark::TaskScheduler scheduler(env("ARK_SCHEDULE_DB", "data/schedules.db"), [](std::string_view goal, bool privacy) {
        const auto task_id = ark::make_message("scheduler", "supervisor_agent", "SUPERVISE", ark::Json::object())
            .at("task_id").get<std::string>();
        return run_task(goal, config(), ark::Json::object(), privacy, false, std::nullopt, 10, task_id, {});
    });
    scheduler.start();
    const bool listened = ark::serve_api(port, {
        config_dir() / "privacy_config.json",
        [](const ark::Json& task, const ark::TaskEvents& events) {
            const auto& payload = task.at("payload");
            const auto until = payload.contains("until") ? std::optional(payload.at("until").get<std::string>()) : std::nullopt;
            return run_task(payload.at("goal").get_ref<const std::string&>(), config(),
                            payload.value("test_files", ark::Json::object()),
                            payload.value("privacy", false), payload.value("airgap_mode", false),
                            until, payload.value("max_iter", 10),
                            task.at("task_id").get<std::string>(), events);
        },
        [=] {
            const auto redis = runtime_redis(redis_port, redis_db);
            return redis ? ark::AgentRegistry(*redis).all() : ark::Json::array();
        },
        [=](std::string_view name) {
            ark::Redis redis(redis_port, redis_db);
            ark::AgentRegistry registry(redis);
            auto history = ark::AgentVersioner(redis).history_with_rollback_availability(name);
            return history.empty() && registry.get(name).is_null() ? ark::Json(nullptr) : std::move(history);
        },
        memory_tier,
        [=](const ark::Json& report) {
            const auto redis = runtime_redis(redis_port, redis_db);
            record_conflict("priority:" + report.at("task_id").get<std::string>(), report);
            audit_event(redis.get(), {{"event", "conflict_detected"}, {"task_id", report.at("task_id")},
                                      {"report", report}});
        },
        [=](std::string_view cursor) {
            ark::Redis redis(redis_port, redis_db);
            const auto rows = redis.command({"XREAD", "COUNT", "64", "STREAMS", "ark:version_events", std::string(cursor)});
            auto reports = ark::Json::array();
            if (!rows.is_null()) {
                for (const auto& row : rows.at(0).at(1)) {
                    reports.push_back({{"id", row.at(0)},
                        {"report", ark::Json::parse(row.at(1).at(1).get_ref<const std::string&>())}});
                }
            }
            return reports;
        },
        [=](std::string_view name, std::string_view version) {
            ark::Redis redis(redis_port, redis_db);
            return ark::AgentVersioner(redis).rollback(name, version);
        },
        [](std::string_view name) {
            return ark::benchmark_history(env("ARK_BENCHMARK_RESULTS", "benchmarks/arkbench/results"), name);
        },
    });
    scheduler.stop();
    if (!listened) {
        std::cerr << "Could not listen on 127.0.0.1:" << port << '\n';
    }
    return listened ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: ark-cpp research [instruction]\n"
                     "       ark-cpp code [instruction]\n"
                     "       ark-cpp evaluate < message.json\n"
                     "       ark-cpp web-search [--privacy|--airgap] <query>\n"
                     "       ark-cpp browse [--privacy|--airgap] <instruction>\n"
                     "       ark-cpp vision < request.json\n"
                     "       ark-cpp files < operation.json\n"
                     "       ark-cpp status [task_id] | list | stop <task_id> | agents | logs\n"
                     "       ark-cpp models [vram-gib]\n"
                     "       ark-cpp capability-discovery <question> [map.json]\n"
                     "       ark-cpp voice [--file recording.wav] [--seconds 1..60]\n"
                     "       ark-cpp mcp  # MCP stdio server for Claude Code, Cursor, and other MCP clients\n"
                     "       ark-cpp export-obsidian <vault-directory> < result.json\n"
                     "       ark-cpp export-notion < result.json  # requires ARK_NOTION_TOKEN and ARK_NOTION_PARENT_PAGE_ID\n"
                     "       ark-cpp classify|plan <goal>\n"
                     "       ark-cpp run [--privacy] [--airgap] [--until <condition>] [--max-iter <n>] <goal> [tests.json]\n"
                     "       ark-cpp --privacy|--airgap <goal> [tests.json]\n"
                     "       ark-cpp dispatch < dag.json\n"
                     "       ark-cpp conflict-check < evidence.json\n"
                     "       ark-cpp conflict-report [report.json]\n"
                     "       ark-cpp audit|episodes\n"
                     "       ark-cpp episodes-archive | memory-prune\n"
                     "       ark-cpp reflection-queue\n"
                     "       ark-cpp self-improve <research_agent|coding_agent> <suite.json> <report.json>\n"
                     "       ark-cpp versions <agent>\n"
                     "       ark-cpp version-bump <agent> <patch|minor|major> <change.json>\n"
                     "       ark-cpp version-rollback <agent> <version>\n"
                     "       ark-cpp version-compare <agent> <from> <to>\n"
                     "       ark-cpp memory <query>\n"
                     "       ark-cpp memory search <query> | show <tier> [task_id]\n"
                     "       ark-cpp skill-add < skill.json\n"
                     "       ark-cpp skill-conflicts [reviewed-conflict-id]\n"
                     "       ark-cpp sandbox < workspace.json\n"
                     "       ark-cpp terminal 'bash command'\n"
                     "       ark-cpp terminal < request.json\n"
                     "       ark-cpp github < request.json\n"
                     "       ark-cpp document < request.json\n"
                     "       ark-cpp security < request.json\n"
                     "       ark-cpp citation-graph < request.json\n"
                     "       ark-cpp benchmark [suite.json] [report.json]\n"
                     "       ark-cpp repair-benchmark <suite.json> <report.json>\n"
                     "       ark-cpp reflection-study <suite.json> <report.json>\n"
                     "       ark-cpp infer < request.json\n"
                     "       ark-cpp llama-presets <ollama-model-dir>\n"
                     "       ark-cpp inference-benchmark <suite.json> <report.json>\n"
                     "       ark-cpp browser < actions.json\n"
                     "       ark-cpp serve [port]\n"
                     "       ark-cpp ml-optimize < request.json\n"
                     "       ark-cpp devops < request.json\n"
                     "       ark-cpp analyze-csv < request.json\n"
                     "       ark-cpp hypothesize < request.json\n"
                     "       ark-cpp design-experiment < request.json\n"
                     "       ark-cpp scientific-report < request.json\n"
                     "       ark-cpp sage-checkpoint {list|resume LOOP|load LOOP STEP|save LOOP STEP|clear LOOP}\n"
                     "       ark-cpp schedule {cron EXPR GOAL|interval SECONDS GOAL|once ISO_UTC GOAL|watch PATH GOAL|cancel|pause|resume ID}\n"
                     "       ark-cpp schedules\n"
                     "Research without an instruction reads an ARKMessage from stdin.\n";
        return argc < 2 ? 2 : 0;
    }
    try {
        ark::set_resource_conflict_observer([](std::string_view resource, std::chrono::milliseconds waited) {
            const auto id = ark::random_uuid();
            record_conflict(id, {{"conflict_type", "resource"}, {"status", "ACQUIRED"},
                                 {"elapsed_ms", waited.count()}, {"resource", resource}});
        });
        const std::string_view command = argv[1];
        if (command == "schedules" && argc == 2) {
            ark::TaskScheduler scheduler(env("ARK_SCHEDULE_DB", "data/schedules.db"), [](std::string_view, bool) { return ark::Json::object(); });
            std::cout << scheduler.list().dump(2) << '\n'; return 0;
        }
        if (command == "schedule" && argc >= 3) {
            ark::TaskScheduler scheduler(env("ARK_SCHEDULE_DB", "data/schedules.db"), [](std::string_view, bool) { return ark::Json::object(); });
            const std::string_view action = argv[2];
            if (action == "cron" && argc == 5) std::cout << scheduler.schedule_cron(argv[3], argv[4]) << '\n';
            else if (action == "interval" && argc == 5) std::cout << scheduler.schedule_interval(std::chrono::seconds(std::stoll(argv[3])), argv[4]) << '\n';
            else if (action == "once" && argc == 5) std::cout << scheduler.schedule_once(argv[3], argv[4]) << '\n';
            else if (action == "watch" && argc == 5) std::cout << scheduler.schedule_on_file_change(argv[3], argv[4]) << '\n';
            else if (action == "cancel" && argc == 4) { if (!scheduler.cancel(argv[3])) throw std::invalid_argument("Unknown schedule ID"); std::cout << "cancelled\n"; }
            else if (action == "pause" && argc == 4) { if (!scheduler.pause(argv[3])) throw std::invalid_argument("Unknown or completed schedule ID"); std::cout << "paused\n"; }
            else if (action == "resume" && argc == 4) { if (!scheduler.resume(argv[3])) throw std::invalid_argument("Unknown or completed schedule ID"); std::cout << "resumed\n"; }
            else throw std::invalid_argument("Invalid schedule command arguments");
            return 0;
        }
        if (command == "sage-checkpoint" && argc >= 3) {
            const ark::CheckpointManager manager(env("ARK_SAGE_CHECKPOINT_DIR", "data/sage/checkpoints"));
            const std::string_view action = argv[2];
            if (action == "list" && argc == 3) { std::cout << manager.list_loops().dump(2) << '\n'; return 0; }
            if (action == "resume" && argc == 4) {
                const std::vector<std::string> workflow{"literature", "hypothesis", "experiment", "analysis", "report"};
                const std::string loop = argv[3];
                std::cout << ark::Json{{"loop_id", loop}, {"last_completed_step", manager.get_resume_point(loop)},
                    {"resume_step", manager.next_step(loop, workflow)}}.dump(2) << '\n'; return 0;
            }
            if (action == "load" && argc == 5) {
                const auto result = manager.load(argv[3], argv[4]);
                if (!result) { std::cout << "null\n"; return 1; }
                std::cout << result->dump(2) << '\n'; return 0;
            }
            if (action == "save" && argc == 5) {
                manager.save(argv[3], argv[4], ark::Json::parse(std::cin));
                std::cout << "saved\n"; return 0;
            }
            if (action == "clear" && argc == 4) { manager.clear(argv[3]); std::cout << "cleared\n"; return 0; }
            throw std::invalid_argument("Invalid sage-checkpoint command arguments");
        }
        if ((command == "conflict-report" && argc <= 3)) {
            const auto report = conflict_report();
            if (argc == 3) ark::write_json(argv[2], report);
            else std::cout << report.dump(2) << '\n';
            return 0;
        }
        if (command == "browser" && argc == 2) {
            return run_browser();
        }
        if (command == "vision" && argc == 2) {
            ark::CurlTransport http;
            const auto settings = config(false);
            const auto redis = version_store();
            const auto profile = actor_profile("vision_agent", configured_model("ARK_VISION_MODEL", "vision"), redis.get());
            const auto result = run_vision_agent(std::ref(http), settings,
                ark::make_message("user", "vision_agent", "VISION", ark::Json::parse(std::cin)), profile);
            return print_run(result);
        }
        if (command == "vision-check" && argc == 2) {
            const auto endpoint = ark::configured_llama_url();
            if (endpoint.empty()) throw std::invalid_argument("Set ARK_LLAMA_URL to a local llama-server with a vision model loaded");
            ark::CurlTransport http;
            const auto result = ark::inspect_vision_runtime(std::ref(http), endpoint, env("ARK_LLAMA_API_KEY"));
            std::cout << result.dump(2) << '\n';
            return result.at("vision_supported") == true ? 0 : 1;
        }
        if (command == "ml-optimize" && argc == 2) {
            ark::CurlTransport http;
            const auto payload = ark::Json::parse(std::cin);
            const auto result = ark::run_ml_engineer(std::ref(http), env("MLFLOW_TRACKING_URI"),
                ark::make_message("user", "ml_engineer_agent", "ML_OPTIMIZE", payload));
            return print_run(result);
        }
        if (command == "devops" && argc == 2) {
            ark::CurlTransport http;
            const auto result = ark::DevOpsAgent(std::ref(http), env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"))
                .run(ark::make_message("user", "devops_agent", "DEVOPS", ark::Json::parse(std::cin)));
            return print_run(result);
        }
        if (command == "analyze-csv" && argc == 2) {
            const auto result = ark::analyze_csv(ark::make_message("user", "data_analysis_agent", "ANALYZE_DATA", ark::Json::parse(std::cin)));
            return print_run(result);
        }
        if (command == "hypothesize" && argc == 2) {
            ark::CurlTransport http;
            const auto settings = config(false);
            const ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
            ark::SemanticMemory papers(std::ref(http), settings.qdrant_url, encoder, settings.semantic_db);
            const auto password = env("ARK_NEO4J_PASSWORD");
            ark::CitationGraph graph(std::ref(http), env("ARK_NEO4J_URL", "http://127.0.0.1:7474"),
                env("ARK_NEO4J_DATABASE", "neo4j"), env("ARK_NEO4J_USER", password.empty() ? "" : "neo4j"), password);
            auto profile = ark::hypothesis_profile(configured_model("ARK_MODEL", "general"));
            ark::HypothesisEngine engine(std::ref(http), settings.ollama_url, std::move(profile), papers, graph, encoder);
            return print_run(engine.run(ark::make_message("user", "hypothesis_engine", "HYPOTHESIZE", ark::Json::parse(std::cin))));
        }
        if (command == "materials-query" && argc == 2) {
            ark::CurlTransport http;
            const auto input = ark::Json::parse(std::cin);
            const auto ids = input.at("material_ids").get<std::vector<std::string>>();
            const auto fields = input.value("fields", std::vector<std::string>{});
            const auto limit = input.value("limit", 100);
            const auto data = ark::MaterialsProjectClient(std::ref(http), env("MP_API_KEY"))
                .summaries(ids, fields, limit);
            std::cout << data.dump(2) << '\n';
            return 0;
        }
        if (command == "sage-materials" && argc == 3) {
            const auto input = ark::Json::parse(std::cin);
            const auto topic = input.at("topic").get<std::string>();
            const auto material_ids = input.at("material_ids").get<std::vector<std::string>>();
            const auto figure_paths = input.value("figure_paths", std::vector<std::string>{});
            if (topic.empty() || topic.size() > 1000 || material_ids.size() < 2 || material_ids.size() > 100)
                throw std::invalid_argument("SAGE requires a topic and 2–100 Materials Project IDs");
            if (figure_paths.size() > 12) throw std::invalid_argument("SAGE accepts at most 12 figure paths");
            for (const auto& path : figure_paths) {
                if (path.empty() || path.size() > 1024 || path.starts_with('/')
                    || path.find("..") != std::string::npos || path.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./-") != std::string::npos)
                    throw std::invalid_argument("SAGE figure paths must be safe relative image paths");
            }

            const auto settings = config();
            ark::CurlTransport http;
            const ark::Transport transport = std::ref(http);
            const ark::MaterialsProjectClient materials(transport, env("MP_API_KEY"));
            const auto require_done = [](const ark::Json& message, std::string_view stage) {
                if (message.value("status", "") != "DONE") {
                    const auto detail = message.value("output", ark::Json::object()).value("error_message", "stage failed");
                    throw std::runtime_error(std::string(stage) + " stage failed: " + detail);
                }
                return message.at("output");
            };
            const std::vector<std::string> steps{"literature", "hypothesis", "experiment", "analysis", "vision_figures", "report",
                "experiment_second", "analysis_second", "report_second"};
            ark::CheckpointManager checkpoints(env("ARK_SAGE_CHECKPOINT_DIR", "data/sage/checkpoints"));
            const ark::Json request_signature{{"workflow_version", 3}, {"topic", topic},
                {"material_ids", material_ids}, {"figure_paths", figure_paths}};
            if (const auto saved = checkpoints.load(argv[2], "request")) {
                if (*saved != request_signature) throw std::invalid_argument("SAGE loop ID already belongs to a different topic or material ID list");
            } else {
                checkpoints.save(argv[2], "request", request_signature);
            }
            const auto results = checkpoints.run_workflow(argv[2], steps, [&](std::string_view stage, const ark::Json& prior) -> ark::Json {
                if (stage == "literature") {
                    auto research_config = settings;
                    ark::ResearchAgent researcher(transport, std::move(research_config));
                    return require_done(researcher.run(ark::make_message("user", "research_agent", "RESEARCH", {{"instruction", topic}})), stage);
                }
                if (stage == "hypothesis") {
                    const ark::Embeddings encoder(transport, settings.ollama_url, settings.embedding_model);
                    ark::SemanticMemory papers(transport, settings.qdrant_url, encoder, settings.semantic_db);
                    const auto password = env("ARK_NEO4J_PASSWORD");
                    ark::CitationGraph graph(transport, env("ARK_NEO4J_URL", "http://127.0.0.1:7474"),
                        env("ARK_NEO4J_DATABASE", "neo4j"), env("ARK_NEO4J_USER", password.empty() ? "" : "neo4j"), password);
                    ark::HypothesisEngine engine(transport, settings.ollama_url, ark::hypothesis_profile(settings.model), papers, graph, encoder);
                    return require_done(engine.run(ark::make_message("user", "hypothesis_engine", "HYPOTHESIZE", {{"domain", topic}})), stage);
                }
                if (stage == "experiment" || stage == "experiment_second") {
                    const std::size_t candidate_index = stage == "experiment_second" ? 1 : 0;
                    const auto& hypotheses = prior.at("hypothesis").at("hypotheses");
                    if (!hypotheses.is_array() || hypotheses.size() <= candidate_index) throw std::runtime_error("Hypothesis stage returned too few candidates");
                    const auto llama = ark::configured_llama_url();
                    const auto provider = llama.empty() ? ark::InferenceProvider::ollama : ark::InferenceProvider::llama_cpp;
                    ark::ExperimentDesigner designer(transport, llama.empty() ? settings.ollama_url : llama,
                        ark::experiment_designer_profile(settings.model), provider);
                    return require_done(designer.run(ark::make_message("user", "experiment_designer", "DESIGN_EXPERIMENT",
                        {{"hypothesis", hypotheses.at(candidate_index).at("hypothesis")}})), stage);
                }
                if (stage == "analysis_second") return prior.at("analysis");
                if (stage == "analysis") {
                    const auto data = materials.summaries(material_ids,
                        {"material_id", "formula_pretty", "band_gap", "energy_above_hull"}, 100);
                    if (data.size() < 2) throw std::runtime_error("Materials Project returned fewer than two material records");
                    const auto quote = [](const std::string& value) {
                        std::string escaped = "\"";
                        for (const char c : value) { if (c == '\"') escaped += "\"\""; else escaped.push_back(c); }
                        return escaped + "\"";
                    };
                    std::string csv = "material_id,formula_pretty,band_gap,energy_above_hull\n";
                    for (const auto& row : data) {
                        const auto cell = [&](const char* key) { return row.contains(key) && row.at(key).is_string() ? row.at(key).get<std::string>() : row.value(key, std::string{}); };
                        csv += quote(cell("material_id")) + "," + quote(cell("formula_pretty")) + ",";
                        csv += row.contains("band_gap") && row.at("band_gap").is_number() ? row.at("band_gap").dump() : "";
                        csv += ",";
                        csv += row.contains("energy_above_hull") && row.at("energy_above_hull").is_number() ? row.at("energy_above_hull").dump() : "";
                        csv += "\n";
                    }
                    return {{"materials", data}, {"analysis", require_done(ark::analyze_csv(
                        ark::make_message("user", "data_analysis_agent", "ANALYZE_DATA", {{"csv", csv}})), stage)}};
                }
                if (stage == "vision_figures") {
                    auto figures = ark::Json::array();
                    if (figure_paths.empty()) return {{"figures", figures}};
                    const auto profile = ark::vision_profile(configured_model("ARK_VISION_MODEL", "vision"));
                    for (const auto& path : figure_paths) {
                        const auto output = require_done(run_vision_agent(transport, settings,
                            ark::make_message("user", "vision_agent", "VISION", {
                                {"instruction", "Describe this experiment figure with a factual caption. Preserve visible confusion-matrix labels and cell values; do not infer unstated metrics."},
                                {"image_path", path}}), profile), stage);
                        figures.push_back({{"path", path}, {"caption", output.at("caption")},
                            {"chart_type", output.at("chart_type")}, {"summary", output.at("summary")},
                            {"observations", output.at("observations")}, {"uncertainties", output.at("uncertainties")}});
                    }
                    return {{"figures", std::move(figures)}};
                }
                if (stage == "report" || stage == "report_second") {
                    const bool second = stage == "report_second";
                    const auto candidate_index = second ? 1 : 0;
                    const auto& candidate = prior.at("hypothesis").at("hypotheses").at(candidate_index);
                    ark::Json references = ark::Json::array();
                    const auto& available = prior.at("hypothesis").value("references", ark::Json::array());
                    for (const auto& paper_id : candidate.at("paper_ids")) {
                        for (const auto& reference : available) {
                            if (reference.value("paper_id", "") == paper_id.get<std::string>()) {
                                references.push_back({{"paper_id", reference.at("paper_id")},
                                    {"title", reference.at("title")}, {"url", reference.value("url", "")}});
                                break;
                            }
                        }
                    }
                    auto report_results = prior.at(second ? "analysis_second" : "analysis");
                    const auto& analyzed_figures = prior.at("vision_figures").at("figures");
                    report_results["vision_figure_analyses"] = analyzed_figures;
                    const auto password = env("ARK_NEO4J_PASSWORD");
                    ark::CitationGraph graph(transport, env("ARK_NEO4J_URL", "http://127.0.0.1:7474"),
                        env("ARK_NEO4J_DATABASE", "neo4j"), env("ARK_NEO4J_USER", password.empty() ? "" : "neo4j"), password);
                    ark::ScientificReportGenerator report(graph);
                    return require_done(report.run(ark::make_message("user", "scientific_report_generator", "GENERATE_REPORT", {
                        {"title", "Materials screening: hypothesis " + std::to_string(candidate_index + 1) + " — " + topic},
                        {"hypothesis", candidate.at("hypothesis")},
                        {"spec", prior.at(second ? "experiment_second" : "experiment")},
                        {"results", std::move(report_results)}, {"figures", analyzed_figures},
                        {"references", std::move(references)}})), stage);
                }
                throw std::invalid_argument("Unknown SAGE workflow stage");
            });
            std::cout << ark::Json{{"loop_id", argv[2]}, {"status", "DONE"}, {"completed_steps", steps}, {"results", results}}.dump(2) << '\n';
            return 0;
        }
        if (command == "design-experiment" && argc == 2) {
            ark::CurlTransport http;
            const auto settings = config(false);
            const auto llama = ark::configured_llama_url();
            const auto provider = llama.empty() ? ark::InferenceProvider::ollama : ark::InferenceProvider::llama_cpp;
            ark::ExperimentDesigner designer(std::ref(http), llama.empty() ? settings.ollama_url : llama,
                ark::experiment_designer_profile(configured_model("ARK_MODEL", "general")), provider);
            return print_run(designer.run(ark::make_message("user", "experiment_designer", "DESIGN_EXPERIMENT", ark::Json::parse(std::cin))));
        }
        if (command == "scientific-report" && argc == 2) {
            ark::CurlTransport http;
            const auto password = env("ARK_NEO4J_PASSWORD");
            ark::CitationGraph graph(std::ref(http), env("ARK_NEO4J_URL", "http://127.0.0.1:7474"),
                env("ARK_NEO4J_DATABASE", "neo4j"), env("ARK_NEO4J_USER", password.empty() ? "" : "neo4j"), password);
            return print_run(ark::ScientificReportGenerator(graph).run(
                ark::make_message("user", "scientific_report_generator", "GENERATE_REPORT", ark::Json::parse(std::cin))));
        }
        if (command == "benchmark" && argc <= 4) {
            return benchmark(argc >= 3 ? argv[2] : "benchmarks/arkbench/benchmark_v0.1.json",
                             argc == 4 ? argv[3] : "");
        }
        if (command == "repair-benchmark" && argc == 4) {
            return repair_benchmark(argv[2], argv[3]);
        }
        if (command == "reflection-study" && argc == 4) return reflection_study(argv[2], argv[3]);
        if (command == "infer" && argc == 2) return infer();
        if (command == "inference-benchmark" && argc == 4) return inference_benchmark(argv[2], argv[3]);
        if (command == "llama-presets" && argc == 3) {
            const ark::Json models{{"general", configured_model("ARK_MODEL", "general")},
                                   {"code", configured_model("ARK_CODE_MODEL", "code")},
                                   {"vision", configured_model("ARK_VISION_MODEL", "vision")}};
            std::cout << ark::llama_presets(argv[2], models, env("ARK_EMBEDDING_MODEL"));
            return 0;
        }
        if ((command == "versions" && argc == 3) || (command == "version-rollback" && argc == 4)
            || ((command == "version-bump" || command == "version-compare") && argc == 5)) {
            return manage_versions(command, argv[2], argv + 3);
        }
        if (command == "self-improve" && argc == 5)
            return self_improve_agent(argv[2], argv[3], argv[4]);
        if (command == "serve" && argc <= 3) {
            return serve(argc == 3 ? std::stoi(argv[2]) : 8000);
        }
        if (command == "models" && argc <= 3) {
            return choose_models(argc == 3 ? argv[2] : nullptr);
        }
        if (command == "sandbox" && argc == 2) {
            return run_sandbox();
        }
        if (command == "terminal" && argc <= 3) {
            return run_terminal(argc == 3 ? argv[2] : nullptr);
        }
        if (command == "github" && argc == 2) {
            ark::CurlTransport http;
            const ark::GitHubAgent agent(std::ref(http), env("GH_TOKEN", env("GITHUB_TOKEN")));
            return print_run(agent.run(ark::make_message("user", "github_agent", "GITHUB", ark::Json::parse(std::cin))));
        }
        if (command == "document" && argc == 2) {
            return print_run(ark::DocumentAgent().run(
                ark::make_message("user", "document_agent", "DOCUMENT", ark::Json::parse(std::cin))));
        }
        if (command == "security" && argc == 2) {
            ark::CurlTransport http;
            const ark::SecurityAgent agent(ark::DockerSandbox(std::ref(http),
                env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"), env("ARK_SECURITY_IMAGE", "ark-security:local")));
            return print_run(agent.run(ark::make_message("user", "security_agent", "SECURITY", ark::Json::parse(std::cin))));
        }
        if (command == "citation-graph" && argc == 2) {
            ark::CurlTransport http;
            const auto password = env("ARK_NEO4J_PASSWORD");
            const ark::CitationGraph graph(std::ref(http), env("ARK_NEO4J_URL", "http://127.0.0.1:7474"),
                env("ARK_NEO4J_DATABASE", "neo4j"), env("ARK_NEO4J_USER", password.empty() ? "" : "neo4j"), password);
            std::cout << graph.run(ark::Json::parse(std::cin)).dump(2) << '\n';
            return 0;
        }
        if (command == "files" && argc == 2) {
            return print_run(run_file_operation(ark::Json::parse(std::cin)));
        }
        if (command == "web-search" && (argc == 3 || argc == 4)) {
            const std::string_view flag = argc == 4 ? argv[2] : "";
            if (argc == 4 && flag != "--privacy" && flag != "--airgap") {
                throw std::invalid_argument("Expected --privacy or --airgap before query");
            }
            const auto settings = config();
            const auto redis = version_store();
            ark::CurlTransport http;
            auto message = ark::make_message("user", "web_search_agent", "BROWSE", {{"query", argv[argc - 1]},
                {"privacy", flag == "--privacy"}, {"airgap_mode", flag == "--airgap"}});
            return print_run(run_web_search(std::ref(http), settings, std::move(message),
                                           actor_profile("web_search_agent", settings.model, redis.get())));
        }
        if (command == "browse" && (argc == 3 || argc == 4)) {
            const std::string_view flag = argc == 4 ? argv[2] : "";
            if (argc == 4 && flag != "--privacy" && flag != "--airgap") {
                throw std::invalid_argument("Expected --privacy or --airgap before instruction");
            }
            const auto settings = config();
            const auto redis = version_store();
            ark::CurlTransport http;
            auto message = ark::make_message("user", "browser_agent", "BROWSE", {{"instruction", argv[argc - 1]},
                {"privacy", flag == "--privacy"}, {"airgap_mode", flag == "--airgap"}});
            return print_run(run_browser_agent(std::ref(http), settings, std::move(message),
                                               actor_profile("browser_agent", settings.model, redis.get())));
        }
        if (command == "research" && argc <= 3) {
            auto message = argc == 3
                ? ark::make_message("user", "research_agent", "RESEARCH", {{"instruction", argv[2]}})
                : ark::Json::parse(std::cin);
            ark::CurlTransport http;
            const auto settings = config();
            const auto redis = version_store();
            const auto profile = actor_profile("research_agent", settings.model, redis.get());
            const auto result = research_pool.run([&](std::string_view) {
                return ark::ResearchAgent(std::ref(http), settings, profile).run(std::move(message));
            });
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "code" && argc <= 3) {
            auto message = argc == 3
                ? ark::make_message("user", "coding_agent", "CODE", {{"instruction", argv[2]}})
                : ark::Json::parse(std::cin);
            ark::CurlTransport http;
            const auto redis = version_store();
            const auto result = run_coding(std::ref(http), config(false), std::move(message),
                                           actor_profile("coding_agent", "", redis.get()));
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "evaluate" && argc == 2) {
            ark::CurlTransport http;
            const auto settings = config();
            const auto redis = version_store();
            const auto result = run_evaluation(std::ref(http), settings, ark::Json::parse(std::cin),
                                               actor_profile("evaluation_agent", settings.model, redis.get()));
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "dispatch" && argc == 2) {
            const auto dag = ark::Json::parse(std::cin);
            return print_run(dispatch(dag, config(), "DISPATCH", {}, ark::Json::object(), false, {}, true));
        }
        if (command == "conflict-check" && argc == 2) {
            const auto input = ark::Json::parse(std::cin);
            const auto settings = config();
            ark::CurlTransport http;
            std::cout << ark::review_output_conflict(std::ref(http), settings.ollama_url, env("ARK_EVAL_MODEL", settings.model),
                                                     input.at("research"), input.at("code")).dump(2) << '\n';
            return 0;
        }
        if ((command == "audit" || command == "logs") && argc == 2) {
            ark::Redis redis(std::stoi(env("ARK_REDIS_PORT", "6379")), std::stoi(env("ARK_REDIS_DB", "0")));
            std::cout << ark::MessageBus(redis).audit().dump(2) << '\n';
            return 0;
        }
        if (command == "agents" && argc == 2) {
            auto redis = version_store(true);
            std::cout << (redis ? ark::AgentRegistry(*redis).all() : ark::Json::array()).dump(2) << '\n';
            return 0;
        }
        if (command == "list" && argc == 2) {
            api_command("/tasks");
            return 0;
        }
        if ((command == "status" && argc == 3) || (command == "stop" && argc == 3)) {
            api_command("/task/" + ark::url_encode(argv[2]), command == "stop" ? "DELETE" : "GET");
            return 0;
        }
        if (command == "status" && argc == 2) {
            api_command("/tasks?limit=1");
            return 0;
        }
        if ((command == "episodes" || command == "episodes-archive" || command == "reflection-queue") && argc == 2) {
            const std::filesystem::path path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
            auto entries = ark::Json::array();
            if (std::filesystem::exists(path)) {
                ark::EpisodicMemory episodes(path);
                if (command == "episodes") entries = episodes.recent();
                else if (command == "episodes-archive") entries = episodes.archived();
                else entries = episodes.pending_improvements();
            }
            std::cout << entries.dump(2) << '\n';
            return 0;
        }
        if (command == "memory-prune" && argc == 2) {
            const auto settings = config(false);
            ark::CurlTransport http;
            const ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
            ark::SemanticMemory semantic(std::ref(http), settings.qdrant_url, encoder, settings.semantic_db);
            ark::ProceduralMemory skills(skills_path(), encoder);
            std::cout << ark::Json{{"semantic_evicted", semantic.evict_unused()},
                                   {"procedural_evicted", skills.evict_low_success()}}.dump(2) << '\n';
            return 0;
        }
        if (command == "capability-discovery" && argc >= 3) {
            std::string question = argv[2];
            for (int i = 3; i < argc && std::string_view(argv[i]).find(".json") == std::string_view::npos; ++i)
                question += " " + std::string(argv[i]);
            const auto map_path = argc > 3 && std::string_view(argv[argc - 1]).ends_with(".json")
                ? std::filesystem::path(argv[argc - 1]) : config_dir() / "capabilities.json";
            const auto redis = version_store(true);
            const auto registry = ark::AgentRegistry(*redis).all();
            const auto episode_path = std::filesystem::path(env("ARK_EPISODIC_DB", "data/episodic_memory.db"));
            auto reflections = ark::Json::array();
            if (std::filesystem::exists(episode_path)) reflections = ark::EpisodicMemory(episode_path).recent(500);
            const auto benchmark_dir = std::filesystem::path(env("ARK_BENCHMARK_RESULTS", "benchmarks/arkbench/results"));
            auto histories = ark::Json::object();
            for (const auto& agent : registry) {
                const auto name = agent.at("name").get<std::string>();
                histories[name] = ark::benchmark_history(benchmark_dir, name);
            }
            const auto map = ark::build_capability_map(registry, reflections, histories);
            ark::write_capability_map(map_path, map);
            std::cout << ark::answer_capability_question(map, question).dump(2) << '\n';
            return 0;
        }
        if (command == "voice") {
            std::optional<std::filesystem::path> audio;
            int seconds = 5;
            for (int i = 2; i < argc; ++i) {
                const std::string_view arg(argv[i]);
                if (arg == "--file" && i + 1 < argc) audio = argv[++i];
                else if (arg == "--seconds" && i + 1 < argc) seconds = std::stoi(argv[++i]);
                else throw std::invalid_argument("Usage: ark-cpp voice [--file recording.wav] [--seconds 1..60]");
            }
            ark::VoiceInput voice(env("ARK_AUDIO_RECORDER", "arecord"), env("ARK_WHISPER_CLI", "whisper-cli"),
                                  env("ARK_WHISPER_MODEL"));
            const auto transcript = audio ? voice.transcribe(*audio) : voice.capture_and_transcribe(seconds);
            auto result = run_task(transcript, config(), ark::Json::object(), false, false, std::nullopt, 10);
            result["voice_transcript"] = transcript;
            return print_run(result);
        }
        if (command == "mcp" && argc == 2) {
            return mcp_stdio(std::filesystem::canonical("/proc/self/exe"));
        }
        if (command == "export-obsidian" && argc == 3) {
            const auto result = ark::Json::parse(std::cin);
            std::cout << ark::Json{{"path", ark::export_obsidian_note(argv[2], result).string()},
                                   {"task_id", result.at("task_id")}}.dump(2) << '\n';
            return 0;
        }
        if (command == "export-notion" && argc == 2) {
            ark::CurlTransport http;
            const auto result = ark::Json::parse(std::cin);
            const auto exported = ark::export_notion_page(std::ref(http), env("ARK_NOTION_TOKEN"),
                env("ARK_NOTION_PARENT_PAGE_ID"), result);
            std::cout << exported.dump(2) << '\n';
            return 0;
        }
        if (command == "memory" && (argc == 3 || (argc == 4 && std::string_view(argv[2]) == "search"))) {
            std::cout << recall(config(false), argv[argc - 1]).dump(2) << '\n';
            return 0;
        }
        if (command == "memory" && (argc == 4 || argc == 5) && std::string_view(argv[2]) == "show") {
            const std::string_view tier = argv[3];
            if (tier != "working" && tier != "episodic" && tier != "semantic" && tier != "procedural") {
                throw std::invalid_argument("Unknown memory tier");
            }
            std::cout << memory_tier(tier, "", argc == 5 ? argv[4] : "", 50).dump(2) << '\n';
            return 0;
        }
        if (command == "skill-add" && argc == 2) {
            const auto input = ark::Json::parse(std::cin);
            const auto settings = config(false);
            ark::CurlTransport http;
            ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
            ark::ProceduralMemory skills(skills_path(), std::move(encoder));
            const auto id = skills.add(input.at("intent").get<std::string>(), input.at("description").get<std::string>(),
                                       input.at("steps"), input.value("source", "manual"));
            std::cout << skills.get(id).dump(2) << '\n';
            return 0;
        }
        if (command == "skill-conflicts" && argc <= 3) {
            const auto settings = config(false);
            ark::CurlTransport http;
            ark::ProceduralMemory skills(skills_path(), ark::Embeddings(std::ref(http), settings.ollama_url,
                                                                       settings.embedding_model));
            if (argc == 2) {
                std::cout << skills.pending_conflicts().dump(2) << '\n';
                return 0;
            }
            const auto id = ark::Json::parse(argv[2]);
            if (!id.is_number_integer() || id.get<double>() < 1 || id.get<double>() >= 9223372036854775808.0) {
                throw std::invalid_argument("Conflict ID must be a positive integer");
            }
            const bool reviewed = skills.review_conflict(id.get<std::int64_t>());
            std::cout << ark::Json{{"reviewed", reviewed}}.dump() << '\n';
            return reviewed ? 0 : 1;
        }
        if (command == "run" || command == "--privacy" || command == "--airgap" || command == "--until"
            || command == "--max-iter") {
            bool privacy = false;
            bool airgap = false;
            std::optional<std::string> until;
            int max_iterations = 10;
            int argument = command == "run" ? 2 : 1;
            for (; argument < argc; ++argument) {
                const std::string_view flag = argv[argument];
                if (flag == "--privacy") {
                    privacy = true;
                } else if (flag == "--airgap") {
                    airgap = true;
                } else if (flag == "--until" && argument + 1 < argc) {
                    until = argv[++argument];
                } else if (flag == "--max-iter" && argument + 1 < argc) {
                    max_iterations = ark::iteration_limit(ark::Json::parse(argv[++argument]));
                } else {
                    break;
                }
            }
            if (command != "run" && argument + 1 < argc && std::string_view(argv[argument]) == "run") {
                ++argument;
            }
            const auto remaining = argc - argument;
            if (remaining < 1 || remaining > 2 || std::string_view(argv[argument]).starts_with("--")) {
                std::cerr << "Usage: ark-cpp run [--privacy] [--airgap] <goal> [tests.json]\n";
                return 2;
            }
            const auto trusted_tests = remaining == 2 ? ark::read_json(argv[argument + 1]) : ark::Json::object();
            return print_run(run_task(argv[argument], config(), trusted_tests, privacy, airgap, until, max_iterations));
        }
        if ((command == "classify" || command == "plan") && argc == 3) {
            const auto settings = config();
            ark::CurlTransport http;
            const auto redis = version_store();
            const auto policy = ark::TaskPrivacy::resolve(settings.privacy, ark::Json::object());
            const ark::Planner planner(std::ref(http), settings.ollama_url,
                actor_profile("supervisor_agent", settings.model, redis.get()),
                actor_profile("planner_agent", settings.model, redis.get()), env("ARK_GROQ_MODEL"),
                policy.enabled ? std::string{} : env("ARK_GROQ_API_KEY"));
            if (command == "classify") {
                std::cout << ark::Json{{"intent", planner.classify(argv[2])}}.dump() << '\n';
                return 0;
            }
            std::cout << planner.plan(argv[2], {"research_agent", "coding_agent", "evaluation_agent", "memory_agent"}).dump(2) << '\n';
            return 0;
        }
        if (argc == 2 && !command.starts_with('-')) {
            return print_run(run_task(command, config(), ark::Json::object(), false, false, std::nullopt, 10));
        }
        std::cerr << "Invalid command or arguments. See ark-cpp --help.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
