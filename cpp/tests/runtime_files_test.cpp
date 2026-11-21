#include "ark/research.hpp"
#include "ark/runtime_files.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main() {
    try {
        const auto path = std::filesystem::temp_directory_path() / ("ark-io-" + ark::random_uuid());
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
        } cleanup{path};
        ark::Json library;
        for (auto name : {"T1", "T2", "T3", "T4", "T5"}) {
            library["tiers"][name] = {{"models", {{"general", name}}}};
        }
        const auto selected = ark::pick_models(library, 8.0, 32.0);
        ark::write_json(path / "active-models.json", selected);
        const auto written = ark::read_json(path / "active-models.json");
        if (written.at("tier") != "T2" || written.at("models").at("general") != "T2"
            || ark::pick_models(library, 0, 32).at("tier") != "T5"
            || ark::pick_models(library, 14, 32).at("tier") != "T3") {
            throw std::runtime_error("Hardware model mapping changed");
        }
        const auto snapshot = path / "STATE.md";
        ark::write_state(snapshot, "Explain attention", "ark_1", "RUNNING", true, 0, ark::Json::object());
        ark::write_state(snapshot, "Explain attention", "ark_1", "DONE", true, 1,
                         {{"agent", "research_agent"}, {"task", "t1"}, {"version", "research_agent@1.0.0"}});
        std::ifstream file(snapshot);
        std::ostringstream contents;
        contents << file.rdbuf();
        const auto text = contents.str();
        if (text.find("DONE") == std::string::npos || text.find("RUNNING") != std::string::npos
            || text.find("Privacy mode:    True") == std::string::npos
            || text.find("research_agent -> t1") == std::string::npos) {
            throw std::runtime_error("State snapshot did not reflect the latest completion");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
