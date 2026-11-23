#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

#include <filesystem>

namespace ark {

enum class InferenceProvider { ollama, llama_cpp };

struct Generation {
    std::string text;
    std::string finish_reason;
    int tokens = 0;
    double tokens_per_second = 0;
};

using TextStream = std::function<void(std::string_view)>;

class LocalInference {
public:
    LocalInference(Transport transport, std::string_view url, InferenceProvider provider);

    Generation generate(std::string_view model, const Json& messages, const Json& schema = nullptr,
                        int max_tokens = 2400, double temperature = 0.0, const TextStream& stream = {}) const;
    std::vector<std::vector<float>> embed(std::string_view model, const std::vector<std::string>& texts) const;

private:
    Response send(const Request& request) const;
    Transport transport_;
    std::string url_;
    InferenceProvider provider_;
    std::vector<std::string> headers_;
};

std::string configured_llama_url();
std::string llama_presets(const std::filesystem::path& ollama_models, const Json& models, std::string_view embedding_model);

} // namespace ark
