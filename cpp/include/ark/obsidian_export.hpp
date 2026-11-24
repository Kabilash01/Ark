#pragma once

#include "ark/message.hpp"
#include "ark/http.hpp"

#include <filesystem>

namespace ark {

std::filesystem::path export_obsidian_note(const std::filesystem::path& vault, const Json& result);
Json export_notion_page(const Transport& transport, std::string_view token,
                        std::string_view parent_page_id, const Json& result);

} // namespace ark
