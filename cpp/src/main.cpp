#include "ark/research.hpp"

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: ark-cpp research [instruction]\n"
                     "With no instruction, read an ARKMessage JSON object from stdin.\n";
        return argc < 2 ? 2 : 0;
    }
    if (std::string_view(argv[1]) != "research" || argc > 3) {
        std::cerr << "Expected 'research [instruction]'.\n";
        return 2;
    }
    try {
        auto message = argc == 3
            ? ark::make_message("user", "research_agent", "RESEARCH", {{"instruction", argv[2]}})
            : ark::Json::parse(std::cin);
        ark::CurlTransport http;
        ark::ResearchAgent agent(std::ref(http), config());
        const auto result = agent.run(std::move(message));
        std::cout << result.dump(2) << '\n';
        return result.at("status") == "DONE" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
