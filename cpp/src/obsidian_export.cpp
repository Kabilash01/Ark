#include "ark/obsidian_export.hpp"

#include <fstream>
#include <stdexcept>

namespace ark {
namespace {

std::string markdown_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (const char c : value) {
        if (c == '\n' || c == '\r') escaped += "\n";
        else {
            if (std::string_view("\\`*_{}[]<>()#+-.!|>").find(c) != std::string_view::npos) escaped.push_back('\\');
            escaped.push_back(c);
        }
    }
    return escaped;
}

std::string hex_id(std::string_view id) {
    if (id.empty() || id.size() > 80) throw std::invalid_argument("Export task_id must contain 1 to 80 bytes");
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(id.size() * 2);
    for (const unsigned char byte : id) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::string yaml_scalar(const std::string& value) { return Json(value).dump(); }

bool valid_uuid(std::string_view id) {
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return false;
        } else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')
                     || (id[i] >= 'A' && id[i] <= 'F'))) return false;
    }
    return true;
}

} // namespace

std::filesystem::path export_obsidian_note(const std::filesystem::path& vault, const Json& result) {
    if (!result.is_object() || !result.contains("task_id") || !result.at("task_id").is_string())
        throw std::invalid_argument("Export requires a result object with a string task_id");
    const auto id = result.at("task_id").get<std::string>();
    const auto safe_id = hex_id(id);
    std::filesystem::create_directories(vault);
    const auto root = std::filesystem::canonical(vault);
    if (!std::filesystem::is_directory(root)) throw std::invalid_argument("Obsidian vault must be a directory");
    const auto target = root / ("ark-" + safe_id + ".md");
    const auto existing = std::filesystem::symlink_status(target);
    if (std::filesystem::is_symlink(existing) || (std::filesystem::exists(existing) && !std::filesystem::is_regular_file(existing)))
        throw std::runtime_error("Refusing to replace a non-regular Obsidian note");

    if (result.contains("title") && !result.at("title").is_string())
        throw std::invalid_argument("Export title must be a string");
    if (result.contains("status") && !result.at("status").is_string())
        throw std::invalid_argument("Export status must be a string");
    const auto title = result.value("title", result.value("goal", "ARK task " + id));
    const auto status = result.value("status", "UNKNOWN");
    const auto temporary = root / (".ark-export-" + random_uuid() + ".tmp");
    struct RemoveTemporary {
        const std::filesystem::path& path;
        ~RemoveTemporary() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << "---\n"
           << "ark_task_id: " << yaml_scalar(id) << "\n"
           << "ark_status: " << yaml_scalar(status) << "\n"
           << "title: " << yaml_scalar(title) << "\n"
           << "---\n\n# " << markdown_escape(title) << "\n\n"
           << "**Status:** " << markdown_escape(status) << "\n\n";
    if (result.contains("output") && result.at("output").is_object()) {
        const auto& value = result.at("output");
        const auto summary = value.value("summary", std::string{});
        if (!summary.empty()) output << "## Summary\n\n" << markdown_escape(summary) << "\n\n";
    }
    output << "## ARK result\n\n<details><summary>Structured result JSON</summary>\n\n<pre>";
    const auto serialized = result.dump(2, ' ', false, Json::error_handler_t::replace);
    for (const char c : serialized) {
        if (c == '&') output << "&amp;";
        else if (c == '<') output << "&lt;";
        else if (c == '>') output << "&gt;";
        else output.put(c);
    }
    output << "</pre>\n\n</details>\n";
    output.close();
    std::filesystem::rename(temporary, target);
    return target;
}

Json export_notion_page(const Transport& transport, std::string_view token,
                        std::string_view parent_page_id, const Json& result) {
    if (!transport) throw std::invalid_argument("Notion export requires an HTTP transport");
    if (token.empty() || token.size() > 4096) throw std::invalid_argument("Notion token is missing or too long");
    if (!valid_uuid(parent_page_id)) throw std::invalid_argument("Notion parent page ID must be a UUID");
    if (!result.is_object() || !result.contains("task_id") || !result.at("task_id").is_string())
        throw std::invalid_argument("Export requires a result object with a string task_id");
    const auto id = result.at("task_id").get<std::string>();
    if (id.empty() || id.size() > 80) throw std::invalid_argument("Export task_id must contain 1 to 80 bytes");
    if (result.contains("title") && !result.at("title").is_string())
        throw std::invalid_argument("Export title must be a string");
    if (result.contains("status") && !result.at("status").is_string())
        throw std::invalid_argument("Export status must be a string");
    const auto title = result.value("title", result.value("goal", "ARK task " + id));
    const auto status = result.value("status", "UNKNOWN");
    if (title.empty() || title.size() > 2000) throw std::invalid_argument("Notion page title must contain 1 to 2000 bytes");
    auto children = Json::array({
        {{"object", "block"}, {"type", "paragraph"}, {"paragraph", {"rich_text", Json::array({
            {{"type", "text"}, {"text", {{"content", "ARK task ID: " + id}}}}
        })}}},
        {{"object", "block"}, {"type", "paragraph"}, {"paragraph", {"rich_text", Json::array({
            {{"type", "text"}, {"text", {{"content", "Status: " + status}}}}
        })}}}
    });
    if (result.contains("output") && result.at("output").is_object()) {
        const auto summary = result.at("output").value("summary", std::string{});
        if (!summary.empty()) {
            if (summary.size() > 2000) throw std::invalid_argument("Notion summary exceeds 2000 UTF-8 bytes");
            children.push_back({{"object", "block"}, {"type", "paragraph"}, {"paragraph", {"rich_text", Json::array({
                {{"type", "text"}, {"text", {{"content", summary}}}}
            })}}});
        }
    }
    Json body = Json::object();
    body["parent"] = {{"type", "page_id"}, {"page_id", parent_page_id}};
    body["properties"] = Json::object();
    body["properties"]["title"] = {{"title", Json::array({
        {{"type", "text"}, {"text", {{"content", title}}}}
    })}};
    body["children"] = std::move(children);
    const auto response = transport({"https://api.notion.com/v1/pages", "POST", body.dump(),
        {"Authorization: Bearer " + std::string(token), "Notion-Version: 2026-03-11", "Content-Type: application/json"},
        std::chrono::seconds(30)});
    response.require_success("Notion page export");
    const auto page = Json::parse(response.body);
    if (!page.is_object() || !page.contains("id") || !page.at("id").is_string()
        || !page.contains("url") || !page.at("url").is_string())
        throw std::runtime_error("Notion returned an incomplete page response");
    return {{"page_id", page.at("id")}, {"url", page.at("url")}, {"title", title}};
}

} // namespace ark
