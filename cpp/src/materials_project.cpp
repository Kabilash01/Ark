#include "ark/materials_project.hpp"

#include <algorithm>
#include <regex>
#include <stdexcept>

namespace ark {
namespace {

void validate_endpoint(const std::string& endpoint) {
    static const std::regex allowed(R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/?$|^http://(127\.0\.0\.1|localhost)(:[0-9]{1,5})?/?$)");
    if (!std::regex_match(endpoint, allowed)) throw std::invalid_argument("Materials Project endpoint must use HTTPS or HTTP loopback");
}

std::string join(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result.push_back(',');
        result += value;
    }
    return result;
}

void validate_values(const std::vector<std::string>& values, std::string_view name) {
    if (values.size() > 100) throw std::invalid_argument(std::string(name) + " accepts at most 100 values");
    for (const auto& value : values) {
        if (value.empty() || value.size() > 128 || value.find_first_of(",\r\n") != std::string::npos)
            throw std::invalid_argument(std::string(name) + " values must be non-empty and cannot contain commas or line breaks");
    }
}

} // namespace

MaterialsProjectClient::MaterialsProjectClient(Transport transport, std::string api_key, std::string endpoint)
    : transport_(std::move(transport)), endpoint_(std::move(endpoint)), api_key_(std::move(api_key)) {
    if (!transport_) throw std::invalid_argument("Materials Project client requires an HTTP transport");
    if (api_key_.empty() || api_key_.size() > 512 || api_key_.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Materials Project API key is missing or invalid");
    validate_endpoint(endpoint_);
    while (!endpoint_.empty() && endpoint_.back() == '/') endpoint_.pop_back();
}

Json MaterialsProjectClient::summaries(const std::vector<std::string>& material_ids,
                                       const std::vector<std::string>& fields, int limit) const {
    if (material_ids.empty()) throw std::invalid_argument("At least one Materials Project ID is required");
    validate_values(material_ids, "Material IDs");
    validate_values(fields, "Fields");
    if (limit < 1 || limit > 1000) throw std::invalid_argument("Materials Project limit must be between 1 and 1000");

    std::string url = endpoint_ + "/materials/summary/?material_ids=" + url_encode(join(material_ids))
        + "&_limit=" + std::to_string(limit);
    if (!fields.empty()) url += "&_fields=" + url_encode(join(fields));
    const auto response = transport_({url, "GET", {}, {"X-API-KEY: " + api_key_, "Accept: application/json"}, std::chrono::seconds(30)});
    response.require_success("Materials Project summary query");
    const auto body = Json::parse(response.body);
    if (!body.is_object() || !body.contains("data") || !body.at("data").is_array())
        throw std::runtime_error("Materials Project summary response has no data array");
    return body.at("data");
}

} // namespace ark
