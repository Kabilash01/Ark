#include "ark/obsidian_export.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void writes_safe_stable_markdown_notes() {
    const auto root = std::filesystem::temp_directory_path() / ("ark-vault-" + ark::random_uuid());
    try {
        const ark::Json result{{"task_id", "../../task/one"}, {"goal", "Research <topic>"}, {"status", "DONE"},
            {"output", {{"summary", "Found a useful result"}, {"raw", "<script>alert(1)</script>"}}}};
        const auto note = ark::export_obsidian_note(root, result);
        check(note.parent_path() == std::filesystem::canonical(root), "export escaped the selected vault");
        check(note.filename().string().starts_with("ark-"), "export filename lacks stable ARK prefix");
        std::ifstream input(note);
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        check(text.find("## Summary") != std::string::npos && text.find("Found a useful result") != std::string::npos,
              "export omitted the ARK summary");
        check(text.find("&lt;script&gt;") != std::string::npos && text.find("<script>") == std::string::npos,
              "export did not escape structured JSON markup");
        const auto repeated = ark::export_obsidian_note(root, result);
        check(repeated == note, "repeat export did not replace the stable note");
        bool rejected = false;
        try { (void)ark::export_obsidian_note(root, {{"task_id", ""}}); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "export accepted an empty task ID");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}

void refuses_symlink_target() {
    const auto root = std::filesystem::temp_directory_path() / ("ark-vault-link-" + ark::random_uuid());
    std::filesystem::create_directories(root);
    const auto outside = root.parent_path() / ("ark-outside-" + ark::random_uuid());
    { std::ofstream out(outside); out << "untouched"; }
    const auto safe_id = std::string("61");
    std::filesystem::create_symlink(outside, root / ("ark-" + safe_id + ".md"));
    bool rejected = false;
    try { (void)ark::export_obsidian_note(root, {{"task_id", "a"}}); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "export followed an existing note symlink");
    std::ifstream input(outside);
    std::string contents;
    input >> contents;
    check(contents == "untouched", "symlink target contents changed");
    std::filesystem::remove_all(root);
    std::filesystem::remove(outside);
}

void sends_safe_notion_page_request() {
    bool called = false;
    const ark::Transport transport = [&](const ark::Request& request) {
        called = true;
        check(request.url == "https://api.notion.com/v1/pages" && request.method == "POST",
              "Notion export used an unexpected endpoint");
        check(request.headers.size() == 3 && request.headers[0] == "Authorization: Bearer test-token"
              && request.headers[1] == "Notion-Version: 2026-03-11",
              "Notion request authentication or version headers are incorrect");
        const auto body = ark::Json::parse(request.body);
        check(body.at("parent").at("page_id") == "11111111-1111-4111-8111-111111111111",
              "Notion export used the wrong destination page");
        check(body.at("properties").at("title").at("title").at(0).at("text").at("content") == "Example task",
              "Notion export omitted the page title");
        check(body.at("children").size() == 3, "Notion export omitted task metadata or summary");
        return ark::Response{200, R"({"id":"22222222-2222-4222-8222-222222222222","url":"https://www.notion.so/example"})"};
    };
    const auto page = ark::export_notion_page(transport, "test-token", "11111111-1111-4111-8111-111111111111",
        {{"task_id", "task-1"}, {"title", "Example task"}, {"status", "DONE"},
         {"output", {{"summary", "A short result"}}}});
    check(called && page.at("page_id") == "22222222-2222-4222-8222-222222222222",
          "Notion export did not return the created page");
    bool rejected = false;
    try { (void)ark::export_notion_page(transport, "", "bad-id", {{"task_id", "task"}}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Notion export accepted missing credentials or an invalid parent");
}
}

int main() {
    try { writes_safe_stable_markdown_notes(); refuses_symlink_target(); sends_safe_notion_page_request(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
