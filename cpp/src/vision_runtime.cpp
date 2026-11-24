#include "ark/vision_runtime.hpp"

#include <stdexcept>

namespace ark {

Json inspect_vision_runtime(Transport transport, std::string_view base_url, std::string api_key) {
    if (!transport) throw std::invalid_argument("Vision runtime check requires an HTTP transport");
    const auto endpoint = local_base_url(base_url);
    if (api_key.find_first_of("\r\n") != std::string::npos) throw std::invalid_argument("Invalid llama.cpp API key");
    std::vector<std::string> headers{"Accept: application/json"};
    if (!api_key.empty()) headers.push_back("Authorization: Bearer " + api_key);
    const auto response = transport({endpoint + "/props", "GET", {}, std::move(headers), std::chrono::seconds(10)});
    response.require_success("llama.cpp runtime metadata");
    const auto props = Json::parse(response.body);
    if (!props.is_object() || !props.contains("modalities") || !props.at("modalities").is_object()
        || !props.at("modalities").contains("vision") || !props.at("modalities").at("vision").is_boolean())
        throw std::runtime_error("llama.cpp /props response does not report vision capability");
    return {{"provider", "llama.cpp"}, {"model", props.value("model_alias", "")},
        {"vision_supported", props.at("modalities").at("vision")},
        {"media_marker", props.value("media_marker", "")}};
}

} // namespace ark
