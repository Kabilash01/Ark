#include "ark/storage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

void sqlite_round_trip() {
    const auto temp = std::filesystem::temp_directory_path()
        / ark::make_message("test", "memory_agent", "STORE", Json::object()).at("task_id").get<std::string>();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{temp};

    const auto path = temp / "episodes.db";
    const Json original{
        {"task_id", "root"}, {"agent_name", "research_agent"}, {"agent_version", "research_agent@1.0.0"},
        {"intent", "RESEARCH"}, {"summary", "Quoted text isn't a SQL command."}, {"quality_score", 0.85},
        {"timestamp", "2026-01-01T00:00:00+00:00"}, {"metadata", {{"paper", "1706.03762"}}},
    };
    {
        ark::EpisodicMemory episodes(path);
        check(episodes.log(original) > 0, "missing episode ID");
        auto another = original;
        another["task_id"] = "second";
        another["timestamp"] = "2026-01-02T00:00:00+00:00";
        another["quality_score"] = nullptr;
        episodes.log(another);
    }
    ark::EpisodicMemory reopened(path);
    check(reopened.recent(1).at(0).at("task_id") == "second", "episode order or limit changed");
    check(reopened.recent(1).at(0).at("quality_score").is_null(), "null quality changed");
    check(reopened.by_agent("research_agent").size() == 2, "episodes did not persist");
    const auto result = reopened.by_version("research_agent", "research_agent@1.0.0");
    check(result.at(1).at("metadata") == original.at("metadata"), "metadata changed");
    check(reopened.by_intent("CODE").empty(), "intent filter is incorrect");
    check(reopened.search("isn't").size() == 2, "quotation in the filter is incorrect");
    check(reopened.by_time("2026-01-02", "2026-01-03").size() == 1, "time filter is incorrect");
}

void redis_round_trip(int port) {
    ark::Redis data(port);
    ark::Redis subscriber(port);
    ark::WorkingMemory memory(data);
    ark::MessageBus bus(data);
    ark::MessageBus listener(subscriber);
    ark::AgentRegistry registry(data);
    const auto id = ark::make_message("test", "memory_agent", "STORE", Json::object())
        .at("task_id").get<std::string>();

    memory.set(id + "[other]", "key", Json("other"));
    memory.set(id, "key", Json{{"value", 42}});
    memory.set(id, "temporary", Json(1), 1);
    check(memory.all(id).at("key").at("value") == 42, "wrong working memory value");
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    check(!memory.exists(id, "temporary"), "working memory TTL did not expire");
    memory.clear(id);
    check(memory.all(id).empty(), "working memory clear did not work");
    check(memory.get(id + "[other]", "key") == "other", "clear crossed task boundaries");
    memory.clear(id + "[other]");

    listener.subscribe(id);
    check(bus.publish(id, Json{{"task_id", id}}) >= 1, "subscriber not reached");
    check(listener.next_message().at("data").at("task_id") == id, "pub/sub task changed");
    bus.broadcast(Json{{"event", id}});
    check(listener.next_message().at("channel") == "ark:broadcast", "broadcast not delivered");
    ark::Redis heartbeat_connection(port);
    ark::MessageBus heartbeat_listener(heartbeat_connection);
    heartbeat_listener.subscribe_heartbeat();
    bus.heartbeat(id);
    check(heartbeat_listener.next_message().at("data").at("agent_name") == id, "heartbeat not delivered");
    bus.log({{"event", id}});
    check(bus.audit(1).at(0).at("event") == id, "audit event not stored");

    registry.add(id, {"RESEARCH"}, "fixture-model", "1.0.0");
    check(registry.get(id).at("capabilities") == Json::array({"RESEARCH"}), "registry lost capabilities");
    registry.record_quality(id, 0.5);
    registry.record_quality(id, 0.75);
    check(registry.get(id).at("avg_quality_score") == 0.625, "registry average is wrong");
    check(registry.version(id, "1.1.0", 0.8, "test"), "could not change version");
    check(registry.history(id).at(0).at("previous_version") == "1.0.0", "version history is wrong");
    data.command({"HSET", "registry:" + id, "last_heartbeat", "2020-01-01T00:00:00.500000+00:00"});
    const auto stale = registry.mark_stale();
    check(std::find(stale.begin(), stale.end(), id) != stale.end(), "stale worker was not reported");
    check(registry.get(id).at("status") == "OFFLINE", "stale worker was not taken offline");
    registry.remove(id);
    check(registry.get(id).is_null() && !registry.status(id, "IDLE"), "removed worker was recreated");
    data.command({"DEL", "registry:version_history:" + id});
}

} // namespace

int main() {
    try {
        sqlite_round_trip();
        if (const auto* port = std::getenv("ARK_TEST_REDIS_PORT")) {
            redis_round_trip(std::stoi(port));
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
