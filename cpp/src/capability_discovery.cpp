#include "ark/capability_discovery.hpp"

#include "ark/runtime_files.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains_words(std::string_view text, std::string_view phrase) {
    std::size_t position = 0;
    while ((position = text.find(phrase, position)) != std::string_view::npos) {
        const auto word_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
        const bool left = position == 0 || !word_char(text[position - 1]);
        const auto end = position + phrase.size();
        const bool right = end == text.size() || !word_char(text[end]);
        if (left && right) return true;
        ++position;
    }
    return false;
}

Json empty_array() { return Json::array(); }

} // namespace

Json build_capability_map(const Json& registry, const Json& reflections, const Json& benchmark_histories) {
    if (!registry.is_array() || !reflections.is_array() || !benchmark_histories.is_object())
        throw std::invalid_argument("Capability discovery expects registry/reflection arrays and benchmark history object");
    struct Evidence { Json agents = Json::array(); double quality_sum = 0; int quality_count = 0;
        double bench_sum = 0; int bench_samples = 0; int reflection_failures = 0; };
    std::map<std::string, Evidence> grouped;
    std::map<std::string, std::set<std::string>> agent_caps;
    for (const auto& agent : registry) {
        if (!agent.is_object() || !agent.contains("name") || !agent.at("name").is_string()
            || !agent.contains("capabilities") || !agent.at("capabilities").is_array()) continue;
        const auto name = agent.at("name").get<std::string>();
        for (const auto& cap_json : agent.at("capabilities")) {
            if (!cap_json.is_string()) continue;
            auto cap = cap_json.get<std::string>();
            if (cap.empty()) continue;
            auto& item = grouped[cap];
            item.agents.push_back(name);
            agent_caps[name].insert(cap);
            if (agent.contains("avg_quality_score") && agent.at("avg_quality_score").is_number()) {
                const auto score = agent.at("avg_quality_score").get<double>();
                if (std::isfinite(score) && score >= 0 && score <= 1) { item.quality_sum += score; ++item.quality_count; }
            }
            if (benchmark_histories.contains(name) && benchmark_histories.at(name).is_array()) {
                for (const auto& run : benchmark_histories.at(name)) {
                    if (!run.is_object() || !run.contains("score") || !run.at("score").is_number()) continue;
                    const auto score = run.at("score").get<double>();
                    if (std::isfinite(score) && score >= 0 && score <= 1) {
                        const int samples = std::max(1, run.value("samples", 1));
                        item.bench_sum += score * samples;
                        item.bench_samples += samples;
                    }
                }
            }
        }
    }
    for (const auto& episode : reflections) {
        if (!episode.is_object() || !episode.contains("agent_name") || !episode.at("agent_name").is_string()) continue;
        const auto name = episode.at("agent_name").get<std::string>();
        if (!episode.contains("metadata") || !episode.at("metadata").is_object()) continue;
        const auto metadata = episode.at("metadata");
        if (!metadata.contains("reflection") || !metadata.at("reflection").is_object()) continue;
        const auto reflection = metadata.at("reflection");
        const bool flagged = reflection.value("flag_for_self_improvement", false);
        const bool low_score = episode.contains("quality_score") && episode.at("quality_score").is_number()
            && episode.at("quality_score").get<double>() < 0.65;
        if (!flagged && !low_score) continue;
        for (auto& [cap, evidence] : grouped) {
            if (agent_caps[name].contains(cap)) ++evidence.reflection_failures;
        }
    }
    Json capabilities = Json::object();
    for (auto& [cap, evidence] : grouped) {
        std::sort(evidence.agents.begin(), evidence.agents.end());
        evidence.agents.erase(std::unique(evidence.agents.begin(), evidence.agents.end()), evidence.agents.end());
        const double quality = evidence.quality_count ? evidence.quality_sum / evidence.quality_count : 0.0;
        const double benchmark = evidence.bench_samples ? evidence.bench_sum / evidence.bench_samples : 0.0;
        double confidence = 0.42 + (evidence.quality_count ? quality * 0.18 : 0.0)
            + (evidence.bench_samples ? (0.25 * benchmark + 0.05 * std::min(1.0, evidence.bench_samples / 10.0)) : 0.0)
            - std::min(0.18, evidence.reflection_failures * 0.03);
        confidence = std::clamp(confidence, 0.05, 0.99);
        capabilities[cap] = {{"supported", true}, {"confidence", confidence}, {"agents", evidence.agents},
            {"registry_records", evidence.agents.size()}, {"quality_samples", evidence.quality_count},
            {"average_quality", evidence.quality_count ? Json(quality) : Json(nullptr)},
            {"benchmark_samples", evidence.bench_samples},
            {"benchmark_score", evidence.bench_samples ? Json(benchmark) : Json(nullptr)},
            {"reflection_failures", evidence.reflection_failures}};
    }
    return {{"schema_version", 1}, {"generated_at", utc_timestamp()}, {"capabilities", std::move(capabilities)}};
}

Json answer_capability_question(const Json& capability_map, std::string_view question) {
    if (!capability_map.is_object() || !capability_map.contains("capabilities") || !capability_map.at("capabilities").is_object())
        throw std::invalid_argument("Malformed capability map");
    const auto text = lower(question);
    const Json* best = nullptr;
    std::string matched;
    std::string canonical;
    for (const auto& [cap, evidence] : capability_map.at("capabilities").items()) {
        const auto needle = lower(cap);
        std::string tokenized = needle;
        std::replace(tokenized.begin(), tokenized.end(), '_', ' ');
        if (contains_words(text, needle) || contains_words(text, tokenized)) {
            if (!best || tokenized.size() > matched.size()) { best = &evidence; matched = tokenized; canonical = cap; }
        }
    }
    if (!best) return {{"question", question}, {"can_do", false}, {"confidence", 0.0},
        {"capability", nullptr}, {"reason", "No matching capability has registry evidence"}, {"evidence", Json::array()}};
    return {{"question", question}, {"can_do", best->value("supported", false)},
        {"confidence", best->value("confidence", 0.0)}, {"capability", canonical},
        {"reason", "Matched registered agent capability; confidence reflects registry quality, ARKBench, and reflection evidence"},
        {"evidence", {{"agents", best->value("agents", empty_array())},
            {"benchmark_samples", best->value("benchmark_samples", 0)},
            {"benchmark_score", best->value("benchmark_score", Json(nullptr))},
            {"reflection_failures", best->value("reflection_failures", 0)}}}};
}

void write_capability_map(const std::filesystem::path& path, const Json& capability_map) {
    write_json(path, capability_map);
}

} // namespace ark
