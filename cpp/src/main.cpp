#include "ark/api_server.hpp"
#include "ark/coding.hpp"
#include "ark/evaluation.hpp"
#include "ark/kernel.hpp"
#include "ark/reflection.hpp"
#include "ark/research.hpp"
#include "ark/runtime_files.hpp"
#include "ark/sandbox.hpp"
#include "ark/storage.hpp"
#include "ark/vector_memory.hpp"
#include "ark/versions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace {

std::string env(const char* name, std::string fallback = "") {
    const auto* value = std::getenv(name);
    return value && *value ? value : std::move(fallback);
}

std::filesystem::path config_dir() { return env("ARK_CONFIG_DIR", "config"); }

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
    config.scholar_key = env("SEMANTIC_SCHOLAR_API_KEY");
    return config;
}

std::filesystem::path skills_path() { return env("ARK_PROCEDURAL_DB", "data/procedural_cpp.db"); }

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
    const ark::DockerSandbox sandbox(std::ref(http), env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"),
                                     env("ARK_SANDBOX_IMAGE", "python:3.11-slim"));
    const auto timeout = std::chrono::seconds(input.value("timeout_seconds", 30));
    const auto output = input.value("tests", false)
        ? sandbox.run_tests(workspace, timeout)
        : sandbox.run_python(input.at("code").get<std::string>(), workspace, timeout);
    std::cout << output.json().dump(2, ' ', false, ark::Json::error_handler_t::replace) << '\n';
    return output.exit_code == 0 ? 0 : output.timed_out ? 124 : 1;
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
    } else {
        throw std::invalid_argument("Unknown versioned agent " + std::string(name));
    }
    return redis ? ark::AgentVersioner(*redis).ensure(name, capabilities, defaults) : defaults;
}

std::unique_ptr<ark::Redis> version_store(bool required = false) {
    const auto port = env("ARK_REDIS_PORT", required ? "6379" : "");
    return port.empty() ? nullptr : std::make_unique<ark::Redis>(std::stoi(port), std::stoi(env("ARK_REDIS_DB", "0")));
}

ark::Json run_coding(ark::CurlTransport& http, const ark::ResearchConfig& settings, ark::Json message,
                     const ark::AgentProfile& profile) {
    const ark::DockerSandbox sandbox(std::ref(http), env("ARK_DOCKER_SOCKET", "/var/run/docker.sock"),
                                     env("ARK_SANDBOX_IMAGE", "python:3.11-slim"));
    const ark::CodingAgent worker(std::ref(http), settings.ollama_url, profile,
                                  [&](const auto& workspace) { return sandbox.run_tests(workspace); });
    return worker.run(std::move(message));
}

ark::Json run_evaluation(ark::CurlTransport& http, const ark::ResearchConfig& settings, ark::Json message,
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
    ark::SemanticMemory semantic(std::ref(http), settings.qdrant_url, encoder);
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
            ark::MessageBus(*redis).log({{"event", "task_escalated"}, {"task_id", task_id}, {"report", report}});
        }
    }
}

void attach_reflection(ark::Json& result, const ark::ResearchConfig& settings, std::string_view goal,
                       const ark::Json& attempts, ark::Redis* redis, ark::EpisodicMemory* episodes = nullptr) {
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
        ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
        ark::ProceduralMemory skills(skills_path(), std::move(encoder));
        ark::ReflectionAgent review(std::ref(http), settings.ollama_url, profile);
        result["reflection"] = review.run({{"task_id", result.at("task_id")}, {"intent", result.at("intent")},
            {"goal", goal.substr(0, 2000)}, {"status", result.at("status")}, {"quality_score", result.at("quality_score")},
            {"attempts", attempts}, {"escalations", result.at("escalations")},
            {"agent_versions", versions}}, *episodes, skills);
    } catch (const std::exception& error) {
        result["reflection"] = {{"status", "FAILED"}, {"error", error.what()}};
    }
}

ark::Json dispatch(const ark::Json& dag, const ark::ResearchConfig& settings, std::string_view intent = "PLAN",
                   std::string parent_id = {}, ark::Json versions = ark::Json::object()) {
    const bool via_api = !parent_id.empty();
    if (!via_api) {
        parent_id = ark::make_message("user", "supervisor_agent", intent, ark::Json::object())
            .at("task_id").get<std::string>();
    }
    const auto redis_port = env("ARK_REDIS_PORT", via_api ? "6379" : "");
    const auto redis_db = std::stoi(env("ARK_REDIS_DB", "0"));
    const auto episode_path = env("ARK_EPISODIC_DB", via_api ? "data/episodic_memory.db" : "");
    const std::filesystem::path state_path = env("ARK_STATE_PATH", "STATE.md");
    const auto goal = dag.value("goal", "Saved task plan");
    const bool private_mode = dag.value("privacy", false) || dag.value("airgap_mode", false)
        || settings.privacy.value("privacy_mode", false) || settings.privacy.value("airgap_mode", false);
    std::mutex state_mutex;
    int completed = 0;
    auto last = ark::Json::object();
    auto attempts = ark::Json::array();
    std::unique_ptr<ark::Redis> redis;
    std::unique_ptr<ark::EpisodicMemory> episodes;
    if (!redis_port.empty()) {
        redis = std::make_unique<ark::Redis>(std::stoi(redis_port), redis_db);
        ark::MessageBus(*redis).log({{"event", "dispatch_start"}, {"task_id", parent_id}});
    }
    const auto profiles = [&] {
        std::map<std::string, ark::AgentProfile> resolved;
        for (const auto& layer : ark::order_tasks(dag)) {
            for (const auto& task : layer) {
                if (task.agent != "memory_agent" && !resolved.contains(task.agent)) {
                    resolved.emplace(task.agent, actor_profile(task.agent, settings.model, redis.get()));
                }
            }
        }
        return resolved;
    }();
    for (const auto& [name, profile] : profiles) {
        versions[name] = profile.version;
    }
    if (!episode_path.empty()) {
        episodes = std::make_unique<ark::EpisodicMemory>(episode_path);
    }
    const auto [results, escalations] = ark::supervise_plan(dag, [&](const ark::Task& task, const ark::Json& dependencies,
                                                                   int attempt, const ark::Json& feedback) {
        ark::CurlTransport http;
        auto message = ark::make_message("router", task.agent, task.intent, {
            {"instruction", task.instruction}, {"goal", goal}, {"privacy", dag.value("privacy", false)},
            {"airgap_mode", dag.value("airgap_mode", false)},
        }, parent_id);
        message["memory_context"] = dag.value("memory_context", ark::Json::array());
        if (!dependencies.empty()) {
            message["memory_context"].push_back({{"tier", "dependencies"}, {"data", dependencies}});
        }
        message["retry_count"] = attempt - 1;
        if (feedback.contains("review")) {
            message["payload"]["review_feedback"] = feedback.at("review");
        }
        if (!redis_port.empty()) {
            ark::Redis worker_bus(std::stoi(redis_port), redis_db);
            ark::MessageBus(worker_bus).log({{"event", "worker_start"}, {"task_id", message.at("task_id")},
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
            result = run_coding(http, settings, std::move(message), profiles.at(task.agent));
        } else if (task.agent == "evaluation_agent") {
            message["payload"]["instruction"] = goal + "\n" + task.instruction;
            result = run_evaluation(http, settings, std::move(message), profiles.at(task.agent));
        } else {
            result = ark::ResearchAgent(std::ref(http), settings, profiles.at(task.agent)).run(std::move(message));
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
            ark::write_state(state_path, goal, parent_id, "RUNNING", private_mode, ++completed, last);
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
        }
        return result;
    }, [&](std::string_view name) {
        if (name != "research_agent" && name != "coding_agent" && name != "evaluation_agent") {
            return false;
        }
        if (!redis) {
            return true;
        }
        auto registry = ark::AgentRegistry(*redis);
        const auto record = registry.get(name);
        return !record.is_null() && record.at("status") != "OFFLINE" && registry.heartbeat(name);
    });
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
    ark::write_state(state_path, goal, parent_id, done ? "DONE" : "FAILED", private_mode,
                     static_cast<int>(results.size()), last);
    if (redis) {
        ark::WorkingMemory(*redis).set(parent_id, "results", results);
        ark::MessageBus(*redis).log({{"event", "dispatch_done"}, {"task_id", parent_id},
                                     {"status", done ? "DONE" : "FAILED"}});
    }
    ark::Json result{{"task_id", parent_id}, {"intent", intent}, {"status", done ? "DONE" : "FAILED"},
                     {"dag", dag}, {"results", results}, {"quality_score", quality}, {"escalations", escalations},
                     {"agent_versions", versions}};
    attach_reflection(result, settings, goal, attempts, redis.get(), episodes.get());
    return result;
}

ark::Json run_goal(std::string_view goal, const ark::ResearchConfig& settings, const ark::Json& trusted_tests,
                   bool privacy = false, bool airgap = false, std::string task_id = {}) {
    if (!trusted_tests.is_object()) {
        throw std::invalid_argument("Trusted tests must be a JSON object mapping names to file contents");
    }
    ark::CurlTransport http;
    const auto redis = version_store(!task_id.empty());
    const auto supervisor = actor_profile("supervisor_agent", settings.model, redis.get());
    const auto plan_profile = actor_profile("planner_agent", settings.model, redis.get());
    const ark::Planner planner(std::ref(http), settings.ollama_url, supervisor, plan_profile);
    const ark::Json versions{{"supervisor_agent", supervisor.version}, {"planner_agent", plan_profile.version}};
    auto lower_goal = std::string(goal);
    std::transform(lower_goal.begin(), lower_goal.end(), lower_goal.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool explicit_research = lower_goal.starts_with("research ") || lower_goal.find("arxiv:") != std::string::npos;
    std::string intent = "SUPERVISE";
    ark::Json dag;
    for (int attempt = 1; ; ++attempt) {
        try {
            intent = planner.classify(goal);
            if (intent != "RESEARCH" && intent != "SEARCH" && intent != "CODE") {
                throw std::invalid_argument("No native worker available for intent " + intent);
            }
            const auto agents = intent == "CODE" && !explicit_research ? std::vector<std::string>{"coding_agent"}
                : intent == "SEARCH" ? std::vector<std::string>{"research_agent"}
                : std::vector<std::string>{"research_agent", "coding_agent"};
            auto context = recall(settings, goal);
            dag = planner.plan(goal, agents, context);
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
            ark::Json result{{"task_id", task_id}, {"intent", intent}, {"status", "FAILED"},
                             {"results", ark::Json::object()}, {"quality_score", nullptr}, {"escalations", reports},
                             {"agent_versions", versions}};
            attach_reflection(result, settings, goal, ark::Json::array(), redis.get());
            return result;
        }
    }
    ensure_evaluation(dag);
    dag["goal"] = goal;
    dag["privacy"] = privacy;
    dag["airgap_mode"] = airgap;
    if (!trusted_tests.empty()) {
        dag["coding_test_files"] = trusted_tests;
    }
    return dispatch(dag, settings, intent, std::move(task_id), versions);
}

int print_run(const ark::Json& result) {
    std::cout << result.dump(2) << '\n';
    return result.at("status") == "DONE" ? 0 : 1;
}

int manage_versions(std::string_view command, std::string_view agent, char** arguments) {
    const auto redis = version_store(true);
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
        ark::Redis redis(std::stoi(env("ARK_REDIS_PORT", "6379")), std::stoi(env("ARK_REDIS_DB", "0")));
        return ark::WorkingMemory(redis).all(task_id);
    }
    if (tier == "episodic") {
        const std::filesystem::path path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
        return std::filesystem::exists(path) ? ark::EpisodicMemory(path).recent(limit) : ark::Json::array();
    }
    if (query.empty()) {
        return ark::Json::array();
    }
    const auto settings = config(false);
    ark::CurlTransport http;
    ark::Embeddings encoder(std::ref(http), settings.ollama_url, settings.embedding_model);
    if (tier == "semantic") {
        return ark::SemanticMemory(std::ref(http), settings.qdrant_url, encoder).retrieve(query, limit);
    }
    if (tier == "procedural" && std::filesystem::exists(skills_path())) {
        return ark::ProceduralMemory(skills_path(), encoder).retrieve(query, limit, std::max(limit, 20));
    }
    return ark::Json::array();
}

int serve(int port) {
    const auto redis_port = std::stoi(env("ARK_REDIS_PORT", "6379"));
    const auto redis_db = std::stoi(env("ARK_REDIS_DB", "0"));
    const bool listened = ark::serve_api(port, {
        config_dir() / "privacy_config.json",
        [](const ark::Json& task) {
            const auto& payload = task.at("payload");
            return run_goal(payload.at("goal").get_ref<const std::string&>(), config(),
                            payload.value("test_files", ark::Json::object()),
                            payload.value("privacy", false), payload.value("airgap_mode", false),
                            task.at("task_id").get<std::string>());
        },
        [=] {
            ark::Redis redis(redis_port, redis_db);
            return ark::AgentRegistry(redis).all();
        },
        [=](std::string_view name) {
            ark::Redis redis(redis_port, redis_db);
            ark::AgentRegistry registry(redis);
            return registry.get(name).is_null() ? ark::Json(nullptr) : registry.history(name);
        },
        memory_tier,
        [=](const ark::Json& report) {
            ark::Redis redis(redis_port, redis_db);
            ark::MessageBus(redis).log({{"event", "conflict_detected"}, {"task_id", report.at("task_id")},
                                      {"report", report}});
        },
    });
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
                     "       ark-cpp models [vram-gib]\n"
                     "       ark-cpp classify|plan <goal>\n"
                     "       ark-cpp run <goal> [tests.json]\n"
                     "       ark-cpp dispatch < dag.json\n"
                     "       ark-cpp audit|episodes\n"
                     "       ark-cpp reflection-queue\n"
                     "       ark-cpp versions <agent>\n"
                     "       ark-cpp version-bump <agent> <patch|minor|major> <change.json>\n"
                     "       ark-cpp version-rollback <agent> <version>\n"
                     "       ark-cpp version-compare <agent> <from> <to>\n"
                     "       ark-cpp memory <query>\n"
                     "       ark-cpp skill-add < skill.json\n"
                     "       ark-cpp sandbox < workspace.json\n"
                     "       ark-cpp serve [port]\n"
                     "Research without an instruction reads an ARKMessage from stdin.\n";
        return argc < 2 ? 2 : 0;
    }
    try {
        const std::string_view command = argv[1];
        if ((command == "versions" && argc == 3) || (command == "version-rollback" && argc == 4)
            || ((command == "version-bump" || command == "version-compare") && argc == 5)) {
            return manage_versions(command, argv[2], argv + 3);
        }
        if (command == "serve" && argc <= 3) {
            return serve(argc == 3 ? std::stoi(argv[2]) : 8000);
        }
        if (command == "models" && argc <= 3) {
            return choose_models(argc == 3 ? argv[2] : nullptr);
        }
        if (command == "sandbox" && argc == 2) {
            return run_sandbox();
        }
        if (command == "research" && argc <= 3) {
            auto message = argc == 3
                ? ark::make_message("user", "research_agent", "RESEARCH", {{"instruction", argv[2]}})
                : ark::Json::parse(std::cin);
            ark::CurlTransport http;
            const auto settings = config();
            const auto redis = version_store();
            ark::ResearchAgent agent(std::ref(http), settings, actor_profile("research_agent", settings.model, redis.get()));
            const auto result = agent.run(std::move(message));
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "code" && argc <= 3) {
            auto message = argc == 3
                ? ark::make_message("user", "coding_agent", "CODE", {{"instruction", argv[2]}})
                : ark::Json::parse(std::cin);
            ark::CurlTransport http;
            const auto redis = version_store();
            const auto result = run_coding(http, config(false), std::move(message),
                                           actor_profile("coding_agent", "", redis.get()));
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "evaluate" && argc == 2) {
            ark::CurlTransport http;
            const auto settings = config();
            const auto redis = version_store();
            const auto result = run_evaluation(http, settings, ark::Json::parse(std::cin),
                                               actor_profile("evaluation_agent", settings.model, redis.get()));
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "dispatch" && argc == 2) {
            return print_run(dispatch(ark::Json::parse(std::cin), config()));
        }
        if (command == "audit" && argc == 2) {
            ark::Redis redis(std::stoi(env("ARK_REDIS_PORT", "6379")), std::stoi(env("ARK_REDIS_DB", "0")));
            std::cout << ark::MessageBus(redis).audit().dump(2) << '\n';
            return 0;
        }
        if ((command == "episodes" || command == "reflection-queue") && argc == 2) {
            const std::filesystem::path path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
            auto entries = ark::Json::array();
            if (std::filesystem::exists(path)) {
                ark::EpisodicMemory episodes(path);
                entries = command == "episodes" ? episodes.recent() : episodes.pending_improvements();
            }
            std::cout << entries.dump(2) << '\n';
            return 0;
        }
        if (command == "memory" && argc == 3) {
            std::cout << recall(config(false), argv[2]).dump(2) << '\n';
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
        if (command == "run" && (argc == 3 || argc == 4)) {
            const auto trusted_tests = argc == 4 ? ark::read_json(argv[3]) : ark::Json::object();
            return print_run(run_goal(argv[2], config(), trusted_tests));
        }
        if ((command == "classify" || command == "plan") && argc == 3) {
            const auto settings = config();
            ark::CurlTransport http;
            const auto redis = version_store();
            const ark::Planner planner(std::ref(http), settings.ollama_url,
                actor_profile("supervisor_agent", settings.model, redis.get()),
                actor_profile("planner_agent", settings.model, redis.get()));
            if (command == "classify") {
                std::cout << ark::Json{{"intent", planner.classify(argv[2])}}.dump() << '\n';
                return 0;
            }
            std::cout << planner.plan(argv[2], {"research_agent", "coding_agent", "evaluation_agent", "memory_agent"}).dump(2) << '\n';
            return 0;
        }
        std::cerr << "Invalid command or arguments. See ark-cpp --help.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
