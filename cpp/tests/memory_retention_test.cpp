#include "ark/storage.hpp"
#include "ark/vector_memory.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

void sql(const std::filesystem::path& path, const char* query) {
    sqlite3* database = nullptr;
    const auto opened = sqlite3_open(path.c_str(), &database);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(database, sqlite3_close);
    check(opened == SQLITE_OK, "test database open failed");
    check(sqlite3_exec(database, query, nullptr, nullptr, nullptr) == SQLITE_OK, sqlite3_errmsg(database));
}

void exercise(const std::filesystem::path& folder, const char* port) {
    const auto episodes_path = folder / "episodes.db";
    ark::EpisodicMemory episodes(episodes_path);
    sql(episodes_path, R"(
        WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i<10000)
        INSERT INTO episodes(task_id,agent_name,agent_version,intent,summary,timestamp)
        SELECT 'old','fixture','1.0.0','TEST','fixture','2026-01-01' FROM n;
    )");
    const auto vector_path = folder / "vectors.db";
    const auto procedure_path = folder / "skills.db";
    ark::Embeddings encoder([](const ark::Request&) { return ark::Response{200, R"({"embeddings":[[1,0]]})"}; },
                            "http://localhost:11434", "retention-fixture");
    std::unique_ptr<ark::Redis> redis;
    if (port) redis = std::make_unique<ark::Redis>(std::stoi(port), 11);
    auto points = Json::array();
    auto used = Json::array();
    ark::ProceduralMemory skills(procedure_path, encoder);
    const std::string prefix = ark::random_uuid();
    for (int i = 0; i < 100; ++i) {
        const auto name = prefix + "-" + std::to_string(i);
        episodes.log({{"task_id", name}, {"agent_name", "fixture"}, {"agent_version", "1.0.0"},
                      {"intent", "TEST"}, {"summary", "retention exercise"}});
        if (redis) {
            ark::WorkingMemory working(*redis);
            working.set(name, "state", i, 30);
            working.finish(name, 1);
        }
        points.push_back({{"id", name}, {"payload", {{"text", "example"}}}, {"vector", {1.0, 0.0}}});
        if (i < 10) used.push_back({{"id", name}});
        const auto id = skills.add("TEST", name, {"Measure"}, "retention-fixture");
        skills.record_use(id, i < 20);
    }
    check(episodes.recent(10100).size() == 10000 && episodes.archived(10100).size() == 100, "episode limit or archive mismatch");
    check(skills.evict_low_success() == 80 && skills.all().size() == 20, "skill eviction mismatch");
    ark::SemanticIndex index(vector_path, encoder.model());
    index.store(points);
    index.used(used);
    sql(vector_path, "UPDATE semantic_points SET first_seen = unixepoch() - 2678400");
    int removed = 0;
    ark::SemanticMemory semantic([&](const ark::Request& request) -> ark::Response {
        if (request.url.ends_with("/points/delete?wait=true")) {
            const auto ids = Json::parse(request.body).at("points");
            for (const auto& hit : used) {
                check(std::find(ids.begin(), ids.end(), hit.at("id")) == ids.end(), "used point selected for eviction");
            }
            removed += static_cast<int>(ids.size());
        }
        return {200, "{}"};
    }, "http://localhost:6333", encoder, vector_path);
    check(semantic.evict_unused() == 90 && removed == 90 && semantic.evict_unused() == 0,
          "expired semantic points were not evicted exactly once");
    if (redis) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        for (int i = 0; i < 100; ++i) {
            check(ark::WorkingMemory(*redis).all(prefix + "-" + std::to_string(i)).empty(), "completed task work did not expire");
        }
    }
}

} // namespace

int main() {
    const auto folder = std::filesystem::temp_directory_path() / ("ark-retention-" + ark::random_uuid());
    struct Temporary {
        std::filesystem::path path;
        ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{folder};
    try {
        exercise(folder, std::getenv("ARK_TEST_REDIS_PORT"));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
