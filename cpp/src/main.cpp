#include "ark/kernel.hpp"
#include "ark/research.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

std::string env(const char* name, std::string fallback = "") {
    const auto* value = std::getenv(name);
    return value && *value ? value : std::move(fallback);
}

ark::ResearchConfig config() {
    ark::ResearchConfig config;
    const std::filesystem::path dir = env("ARK_CONFIG_DIR", "config");
    config.model = env("ARK_MODEL");
    if (config.model.empty()) {
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

int dispatch(const ark::Json& dag, const ark::ResearchConfig& settings, std::string_view intent = "PLAN") {
    const auto parent = ark::make_message("user", "supervisor_agent", intent, ark::Json::object());
    const auto results = ark::dispatch_plan(dag, [&](const ark::Task& task, const ark::Json& dependencies) {
        ark::CurlTransport http;
        ark::ResearchAgent agent(std::ref(http), settings);
        auto message = ark::make_message("router", task.agent, task.intent, {
            {"instruction", task.instruction}, {"privacy", dag.value("privacy", false)},
            {"airgap_mode", dag.value("airgap_mode", false)},
        }, parent.at("task_id").get_ref<const std::string&>());
        if (!dependencies.empty()) {
            message["memory_context"].push_back({{"tier", "dependencies"}, {"data", dependencies}});
        }
        return agent.run(std::move(message));
    }, [](std::string_view name) { return name == "research_agent"; });
    const bool done = std::all_of(results.begin(), results.end(), [](const auto& result) {
        return result.at("status") == "DONE";
    });
    std::cout << ark::Json{{"task_id", parent.at("task_id")}, {"intent", intent},
                           {"status", done ? "DONE" : "FAILED"}, {"dag", dag}, {"results", results}}.dump(2) << '\n';
    return done ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: ark-cpp research [instruction]\n"
                     "       ark-cpp classify|plan|run <goal>\n"
                     "       ark-cpp dispatch < dag.json\n"
                     "Research without an instruction reads an ARKMessage from stdin.\n";
        return argc < 2 ? 2 : 0;
    }
    try {
        const std::string_view command = argv[1];
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
            return dispatch(planner.plan(argv[2], {"research_agent"}), settings, intent);
        }
        std::cerr << "Invalid command or arguments. See ark-cpp --help.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
