#include "ark/inference.hpp"

#include "ark/research.hpp"

#include <cstdlib>
#include <map>
#include <regex>

namespace ark {
namespace {

constexpr auto timeout = std::chrono::seconds(120);

void collect(Generation& result, const Json& event, bool llama, bool streaming, const TextStream& write) {
    if (llama) {
        const auto choices = event.value("choices", Json::array());
        if (!choices.empty()) {
            const auto& choice = choices.at(0);
            const auto part = choice.value(streaming ? "delta" : "message", Json::object()).value("content", Json(nullptr));
            if (part.is_string()) {
                const auto& text = part.get_ref<const std::string&>();
                result.text += text;
                if (write && !text.empty()) write(text);
            }
            if (choice.contains("finish_reason") && choice.at("finish_reason").is_string()) {
                result.finish_reason = choice.at("finish_reason").get<std::string>();
            }
        }
        if (event.contains("timings")) {
            const auto& times = event.at("timings");
            result.tokens = times.value("predicted_n", 0);
            result.tokens_per_second = times.value("predicted_per_second", 0.0);
        }
        return;
    }
    const auto text = event.value("message", Json::object()).value("content", "");
    result.text += text;
    if (write && !text.empty()) write(text);
    if (event.contains("done_reason")) result.finish_reason = event.at("done_reason").get<std::string>();
    if (event.contains("eval_count")) {
        result.tokens = event.at("eval_count").get<int>();
        const auto elapsed = event.value("eval_duration", 0.0);
        result.tokens_per_second = elapsed > 0 ? result.tokens * 1e9 / elapsed : 0.0;
    }
}

std::filesystem::path manifest(const std::filesystem::path& base, const std::string& name) {
    static const std::regex pattern(R"((?:([A-Za-z0-9_-]+)/)?([A-Za-z0-9][A-Za-z0-9_.-]*)(?::([A-Za-z0-9][A-Za-z0-9_.-]*))?)");
    std::smatch match;
    if (!std::regex_match(name, match, pattern)) throw std::invalid_argument("Unsupported Ollama model reference");
    const auto local_ref = std::filesystem::path(match[1].matched ? match[1].str() : "library")
        / match[2].str() / (match[3].matched ? match[3].str() : "latest");
    const auto current = base / "manifests-v2" / "ollama.com" / local_ref;
    if (std::filesystem::is_regular_file(current)) return current;
    return base / "manifests" / "registry.ollama.ai" / local_ref;
}

std::string blob(const std::filesystem::path& root, const Json& layer) {
    const auto hash = layer.at("digest").get<std::string>();
    static const std::regex pattern(R"(sha256:[a-f0-9]{64})");
    if (!std::regex_match(hash, pattern)) throw std::runtime_error("Invalid Ollama model digest");
    const auto path = std::filesystem::absolute(root / "blobs" / ("sha256-" + hash.substr(7)));
    if (!std::filesystem::is_regular_file(path) || path.string().find_first_of("\n\r") != std::string::npos) {
        throw std::runtime_error("Ollama model blob is missing or has an invalid path");
    }
    return path.string();
}

} // namespace

std::string configured_llama_url() {
    const auto* value = std::getenv("ARK_LLAMA_URL");
    return value ? value : "";
}

LocalInference::LocalInference(Transport transport, std::string_view url, InferenceProvider provider)
    : transport_(std::move(transport)), url_(local_base_url(url)), provider_(provider), headers_{"Content-Type: application/json"} {
    if (provider_ == InferenceProvider::llama_cpp) {
        const auto* key = std::getenv("ARK_LLAMA_API_KEY");
        if (key && *key) headers_.push_back("Authorization: Bearer " + std::string(key));
    }
}

Generation LocalInference::generate(std::string_view model, const Json& messages, const Json& schema,
                                    int max_tokens, double temperature, const TextStream& stream) const {
    if (model.empty() || !messages.is_array() || messages.empty() || max_tokens < 1 || max_tokens > 8192
        || !(temperature >= 0 && temperature <= 2)) {
        throw std::invalid_argument("Invalid local generation parameters");
    }
    const bool llama = provider_ == InferenceProvider::llama_cpp;
    const bool streaming = static_cast<bool>(stream);
    Json payload{{"model", model}, {"stream", streaming}, {"messages", messages}};
    if (llama) {
        payload.update({{"temperature", temperature}, {"max_tokens", max_tokens},
                        {"chat_template_kwargs", {{"enable_thinking", false}}}});
        if (!schema.is_null()) {
            payload["response_format"] = {{"type", "json_object"}, {"schema", schema}};
        }
    } else {
        payload.update({{"think", false}, {"options", {{"temperature", temperature}, {"num_predict", max_tokens}}}});
        if (!schema.is_null()) payload["format"] = schema;
    }
    Request request{url_ + (llama ? "/v1/chat/completions" : "/api/chat"), "POST", payload.dump(), headers_, timeout};
    Generation result;
    if (!streaming) {
        const auto response = transport_(request);
        response.require_success("Local model");
        collect(result, Json::parse(response.body), llama, false, {});
        return result;
    }

    auto unfinished = std::string{};
    const auto process = [&](std::string_view line) {
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (llama) {
            if (!line.starts_with("data: ")) return;
            line.remove_prefix(6);
        }
        if (line.empty() || line == "[DONE]") return;
        const auto event = Json::parse(line);
        if (event.contains("error")) throw std::runtime_error("Local inference streaming error");
        collect(result, event, llama, true, stream);
    };
    request.on_chunk = [&](std::string_view bytes) {
        unfinished += bytes;
        std::size_t start = 0;
        for (auto end = unfinished.find('\n'); end != std::string::npos; end = unfinished.find('\n', start)) {
            process(std::string_view(unfinished).substr(start, end - start));
            start = end + 1;
        }
        unfinished.erase(0, start);
    };
    const auto response = transport_(request);
    response.require_success("Local model");
    process(unfinished);
    return result;
}

std::vector<std::vector<float>> LocalInference::embed(std::string_view model, const std::vector<std::string>& texts) const {
    const bool llama = provider_ == InferenceProvider::llama_cpp;
    const Json payload{{"model", model}, {"input", texts}};
    const auto response = transport_({url_ + (llama ? "/v1/embeddings" : "/api/embed"), "POST", payload.dump(), headers_, timeout});
    response.require_success("Local embedding");
    const auto body = Json::parse(response.body);
    if (!llama) return body.at("embeddings").get<std::vector<std::vector<float>>>();
    std::vector<std::vector<float>> vectors(texts.size());
    const auto& rows = body.at("data");
    if (rows.size() != texts.size()) throw std::runtime_error("Unexpected number of embedding rows");
    for (const auto& row : rows) {
        auto& vector = vectors.at(row.at("index").get<std::size_t>());
        if (!vector.empty()) throw std::runtime_error("Duplicate embedding index");
        vector = row.at("embedding").get<std::vector<float>>();
    }
    return vectors;
}

std::string llama_presets(const std::filesystem::path& ollama_models, const Json& models, std::string_view embedding_model) {
    std::map<std::string, bool> requested;
    for (const auto* role : {"general", "code", "vision"}) requested[models.at(role).get<std::string>()] = false;
    if (!embedding_model.empty()) requested[std::string(embedding_model)] = true;

    std::string ini = "version = 1\n[*]\nctx-size = 8192\nbatch-size = 512\n";
    for (const auto& [model, embedding] : requested) {
        const auto entry = read_json(manifest(ollama_models, model));
        std::string weights, projector;
        for (const auto& layer : entry.at("layers")) {
            const auto type = layer.at("mediaType").get<std::string>();
            if (type == "application/vnd.ollama.image.model") weights = blob(ollama_models, layer);
            else if (type == "application/vnd.ollama.image.projector") projector = blob(ollama_models, layer);
        }
        if (weights.empty()) throw std::runtime_error("Ollama model manifest has no GGUF weight layer");
        ini += "\n[" + model + "]\nmodel = " + weights + "\n";
        if (!projector.empty()) ini += "mmproj = " + projector + "\n";
        if (embedding) ini += "embedding = true\n";
    }
    return ini;
}

} // namespace ark
