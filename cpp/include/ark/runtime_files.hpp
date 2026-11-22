#pragma once

#include "ark/message.hpp"

#include <filesystem>

namespace ark {

Json pick_models(const Json& library, double vram_gib, double ram_gib);
double detect_vram_gib();
double detect_ram_gib();
void write_json(const std::filesystem::path& path, const Json& data);
void write_state(const std::filesystem::path& path, std::string_view goal, std::string_view task_id,
                 std::string_view status, bool privacy, int completed, const Json& last_result,
                 const Json& done_condition = nullptr);

} // namespace ark
