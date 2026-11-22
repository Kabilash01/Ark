#include "ark/file_system.hpp"
#include "ark/storage.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void workspace_access_is_scoped_and_logged() {
    const auto path = std::filesystem::temp_directory_path() / ark::random_uuid();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{path};
    const auto root = path / "root";
    std::filesystem::create_directories(root / "nested");
    std::ofstream(path / "outside.txt") << "outside";
    std::filesystem::create_symlink(path / "outside.txt", root / "link.txt");
    std::filesystem::create_directory_symlink(root / "nested", root / "link-dir");
    ark::EpisodicMemory episodes(path / "episodes.db");
    const ark::FileSystemAgent agent({{"workspace", root.string()}});
    const auto run = [&](Json payload) {
        payload["root"] = "workspace";
        return agent.run(ark::make_message("test", "file_system_agent", "FILES", std::move(payload)), episodes);
    };
    const Json write{{"operation", "write"}, {"path", "nested/note.txt"}, {"content", "new text"}};
    const auto written = run(write);
    check(written.at("status") == "DONE", written.dump());
    const auto read = run({{"operation", "read"}, {"path", "nested/note.txt"}});
    check(read.at("output").at("content") == "new text", read.dump());
    const auto list = run({{"operation", "list"}, {"path", "nested"}});
    check(list.at("output").at("entries") == Json::array({"note.txt"}), "workspace listing differs");
    check(run(write).at("status") == "FAILED", "create must preserve existing files");
    auto replace = write;
    replace["overwrite"] = true;
    replace["content"] = "replacement";
    check(run(replace).at("status") == "DONE", "explicit overwrite should replace a regular file");
    check(run({{"operation", "read"}, {"path", "nested/note.txt"}}).at("output").at("content") == "replacement",
          "explicit overwrite did not persist");
    for (const auto* target : {"../outside.txt", "/outside.txt", "link.txt", "link-dir/note.txt"}) {
        check(run({{"operation", "read"}, {"path", target}}).at("status") == "FAILED", "unsafe path was read");
    }
    replace["path"] = "link.txt";
    check(run(replace).at("status") == "FAILED", "overwrite followed a symlink");
    std::ifstream outside(path / "outside.txt");
    std::string outside_content;
    outside >> outside_content;
    check(outside_content == "outside", "operation wrote outside the workspace");

    bool saw_write = false;
    for (const auto& row : episodes.by_agent("file_system_agent")) {
        if (row.at("task_id") == written.at("task_id") && row.at("metadata").at("event") == "file_write") {
            saw_write = row.at("metadata").at("path") == "nested/note.txt";
        }
    }
    check(saw_write, "workspace write was not recorded in episodic memory");
}

} // namespace

int main() {
    try {
        workspace_access_is_scoped_and_logged();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
