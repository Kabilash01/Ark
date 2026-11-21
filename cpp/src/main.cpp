#include "ark/kernel.hpp"
#include "ark/research.hpp"
#include "ark/runtime_files.hpp"
#include "ark/storage.hpp"
#include "ark/vector_memory.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>

namespace {

std::string env(const char* name, std::string fallback = "") {
    const auto* value = std::getenv(name);
    return value && *value ? value : std::move(fallback);
}

ark::ResearchConfig config(bool chat_required = true) {
    ark::ResearchConfig config;
    const std::filesystem::path dir = env("ARK_CONFIG_DIR", "config");
    config.model = env("ARK_MODEL");
    if (config.model.empty() && chat_required) {
        config.model = ark::read_json(dir / "active-models.json").at("models").at("general").get<std::string>();
    }
    const auto privacy = dir / "privacy_config.json";
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
    const std::filesystem::path dir = env("ARK_CONFIG_DIR", "config");
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

int dispatch(const ark::Json& dag, const ark::ResearchConfig& settings, std::string_view intent = "PLAN") {
    const auto parent = ark::make_message("user", "supervisor_agent", intent, ark::Json::object());
    const auto& parent_id = parent.at("task_id").get_ref<const std::string&>();
    const auto redis_port = env("ARK_REDIS_PORT");
    const auto episode_path = env("ARK_EPISODIC_DB");
    const std::filesystem::path state_path = env("ARK_STATE_PATH", "STATE.md");
    const auto goal = dag.value("goal", "Saved task plan");
    const bool private_mode = dag.value("privacy", false) || dag.value("airgap_mode", false)
        || settings.privacy.value("privacy_mode", false) || settings.privacy.value("airgap_mode", false);
    std::mutex state_mutex;
    int completed = 0;
    auto last = ark::Json::object();
    std::unique_ptr<ark::Redis> redis;
    std::unique_ptr<ark::EpisodicMemory> episodes;
    if (!redis_port.empty()) {
        redis = std::make_unique<ark::Redis>(std::stoi(redis_port));
        ark::MessageBus(*redis).log({{"event", "dispatch_start"}, {"task_id", parent_id}});
        ark::AgentRegistry registry(*redis);
        if (registry.get("research_agent").is_null()) {
            registry.add("research_agent", {"RESEARCH", "SEARCH"}, settings.model, "1.0.0");
        }
    }
    if (!episode_path.empty()) {
        episodes = std::make_unique<ark::EpisodicMemory>(episode_path);
    }
    const auto results = ark::dispatch_plan(dag, [&](const ark::Task& task, const ark::Json& dependencies) {
        ark::CurlTransport http;
        ark::ResearchAgent agent(std::ref(http), settings);
        auto message = ark::make_message("router", task.agent, task.intent, {
            {"instruction", task.instruction}, {"privacy", dag.value("privacy", false)},
            {"airgap_mode", dag.value("airgap_mode", false)},
        }, parent_id);
        message["memory_context"] = dag.value("memory_context", ark::Json::array());
        if (!dependencies.empty()) {
            message["memory_context"].push_back({{"tier", "dependencies"}, {"data", dependencies}});
        }
        if (!redis_port.empty()) {
            ark::Redis worker_bus(std::stoi(redis_port));
            ark::MessageBus(worker_bus).log({{"event", "worker_start"}, {"task_id", message.at("task_id")},
                                            {"parent_task_id", parent_id}, {"agent", task.agent}});
        }
        auto result = agent.run(std::move(message));
        {
            const std::lock_guard lock(state_mutex);
            last = {{"agent", task.agent}, {"task", task.id}, {"version", result.at("agent_version")}};
            if (result.at("quality_score") != -1.0) {
                last["score"] = result.at("quality_score");
            }
            ark::write_state(state_path, goal, parent_id, "RUNNING", private_mode, ++completed, last);
        }
        if (episodes) {
            const auto& output = result.at("output");
            episodes->log({
                {"task_id", result.at("task_id")}, {"agent_name", task.agent},
                {"agent_version", result.at("agent_version")}, {"intent", task.intent},
                {"summary", output.value("summary", output.value("error_message", std::string{}))},
                {"quality_score", result.at("quality_score") == -1.0 ? ark::Json(nullptr) : result.at("quality_score")},
                {"metadata", {{"parent_task_id", parent_id}, {"status", result.at("status")}}},
            });
        }
        return result;
    }, [&](std::string_view name) {
        if (name != "research_agent") {
            return false;
        }
        if (!redis) {
            return true;
        }
        auto registry = ark::AgentRegistry(*redis);
        const auto record = registry.get(name);
        return !record.is_null() && record.at("status") != "OFFLINE" && registry.heartbeat(name);
    });
    const bool done = std::all_of(results.begin(), results.end(), [](const auto& result) {
        return result.at("status") == "DONE";
    });
    ark::write_state(state_path, goal, parent_id, done ? "DONE" : "FAILED", private_mode,
                     static_cast<int>(results.size()), last);
    if (redis) {
        ark::WorkingMemory(*redis).set(parent_id, "results", results);
        ark::MessageBus(*redis).log({{"event", "dispatch_done"}, {"task_id", parent_id},
                                     {"status", done ? "DONE" : "FAILED"}});
    }
    std::cout << ark::Json{{"task_id", parent_id}, {"intent", intent},
                           {"status", done ? "DONE" : "FAILED"}, {"dag", dag}, {"results", results}}.dump(2) << '\n';
    return done ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: ark-cpp research [instruction]\n"
                     "       ark-cpp models [vram-gib]\n"
                     "       ark-cpp classify|plan|run <goal>\n"
                     "       ark-cpp dispatch < dag.json\n"
                     "       ark-cpp audit|episodes\n"
                     "       ark-cpp memory <query>\n"
                     "       ark-cpp skill-add < skill.json\n"
                     "Research without an instruction reads an ARKMessage from stdin.\n";
        return argc < 2 ? 2 : 0;
    }
    try {
        const std::string_view command = argv[1];
        if (command == "models" && argc <= 3) {
            return choose_models(argc == 3 ? argv[2] : nullptr);
        }
        if (command == "research" && argc <= 3) {
            auto message = argc == 3
                ? ark::make_message("user", "research_agent", "RESEARCH", {{"instruction", argv[2]}})
                : ark::Json::parse(std::cin);
            ark::CurlTransport http;
            ark::ResearchAgent agent(std::ref(http), config());
            const auto result = agent.run(std::move(message));
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "DONE" ? 0 : 1;
        }
        if (command == "dispatch" && argc == 2) {
            return dispatch(ark::Json::parse(std::cin), config());
        }
        if (command == "audit" && argc == 2) {
            ark::Redis redis(std::stoi(env("ARK_REDIS_PORT", "6379")));
            std::cout << ark::MessageBus(redis).audit().dump(2) << '\n';
            return 0;
        }
        if (command == "episodes" && argc == 2) {
            const std::filesystem::path path = env("ARK_EPISODIC_DB", "data/episodic_memory.db");
            const auto entries = std::filesystem::exists(path) ? ark::EpisodicMemory(path).recent() : ark::Json::array();
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
        if ((command == "classify" || command == "plan" || command == "run") && argc == 3) {
            const auto settings = config();
            ark::CurlTransport http;
            const ark::Planner planner(std::ref(http), settings.ollama_url, settings.model);
            if (command == "classify") {
                std::cout << ark::Json{{"intent", planner.classify(argv[2])}}.dump() << '\n';
                return 0;
            }
            if (command == "plan") {
                std::cout << planner.plan(argv[2], {"research_agent", "coding_agent", "evaluation_agent", "memory_agent"}).dump(2) << '\n';
                return 0;
            }
            const auto intent = planner.classify(argv[2]);
            if (intent != "RESEARCH" && intent != "SEARCH") {
                throw std::invalid_argument("No native worker available for intent " + intent);
            }
            const auto context = recall(settings, argv[2]);
            auto dag = planner.plan(argv[2], {"research_agent"}, context);
            dag["goal"] = argv[2];
            dag["memory_context"] = context;
            return dispatch(dag, settings, intent);
        }
        std::cerr << "Invalid command or arguments. See ark-cpp --help.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
