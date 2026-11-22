#pragma once

#include "ark/message.hpp"

#include <filesystem>
#include <functional>
#include <stop_token>

namespace ark {

using TaskEvents = std::function<void(std::string_view, const Json&)>;

struct ApiServices {
    std::filesystem::path privacy_file;
    std::function<Json(const Json&, const TaskEvents&)> run;
    std::function<Json()> agents;
    std::function<Json(std::string_view)> versions;
    std::function<Json(std::string_view, std::string_view, std::string_view, int)> memory;
    std::function<void(const Json&)> log_conflict;
    std::function<Json(std::string_view)> version_events;
    std::function<Json(std::string_view, std::string_view)> rollback;
};

bool serve_api(int port, ApiServices services, std::stop_token stop = {},
               const std::function<void(int)>& ready = {});

} // namespace ark
