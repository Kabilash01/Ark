#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace ark {

using Json = nlohmann::json;

Json make_message(std::string_view sender, std::string_view receiver, std::string_view intent,
                  Json payload, std::string_view parent = "");
bool pending_for(const Json& message, std::string_view receiver);
std::string point_id(std::string_view url);
std::string random_uuid();
std::string utc_timestamp();

} // namespace ark
