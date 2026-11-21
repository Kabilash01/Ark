#pragma once

#include "ark/message.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace ark {

// A synchronous Redis connection. Use a separate connection for subscriptions.
class Redis {
public:
    explicit Redis(int port = 6379, int database = 0);
    ~Redis();
    Redis(const Redis&) = delete;
    Redis& operator=(const Redis&) = delete;

    Json command(const std::vector<std::string>& args);
    Json next_message();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class WorkingMemory {
public:
    explicit WorkingMemory(Redis& redis) : redis_(redis) {}
    void set(std::string_view task_id, std::string_view key, const Json& value, int ttl = 3600);
    Json get(std::string_view task_id, std::string_view key);
    Json all(std::string_view task_id);
    bool exists(std::string_view task_id, std::string_view key);
    void erase(std::string_view task_id, std::string_view key);
    void clear(std::string_view task_id);

private:
    Redis& redis_;
};

class MessageBus {
public:
    explicit MessageBus(Redis& redis) : redis_(redis) {}
    int publish(std::string_view agent, const Json& message);
    int broadcast(const Json& message);
    int heartbeat(std::string_view agent, std::string_view status = "IDLE");
    void subscribe(std::string_view agent);
    void subscribe_heartbeat();
    Json next_message();

    void log(Json event);
    Json audit(int limit = 100);

private:
    Redis& redis_;
};

class AgentRegistry {
public:
    explicit AgentRegistry(Redis& redis) : redis_(redis) {}
    Json add(std::string_view name, const Json& capabilities, std::string_view model, std::string_view version);
    void remove(std::string_view name);
    Json get(std::string_view name);
    Json all();
    bool status(std::string_view name, std::string_view status);
    bool heartbeat(std::string_view name);
    bool record_quality(std::string_view name, double score);
    bool version(std::string_view name, std::string_view version, const Json& score, std::string_view changed_by);
    Json history(std::string_view name);
    std::vector<std::string> mark_stale(std::chrono::seconds after = std::chrono::seconds(90));

private:
    Redis& redis_;
};

class EpisodicMemory {
public:
    explicit EpisodicMemory(const std::filesystem::path& db);
    ~EpisodicMemory();
    EpisodicMemory(const EpisodicMemory&) = delete;
    EpisodicMemory& operator=(const EpisodicMemory&) = delete;

    std::int64_t log(const Json& episode);
    Json recent(int limit = 50);
    Json by_agent(std::string_view agent, int limit = 50);
    Json by_intent(std::string_view intent, int limit = 50);
    Json by_version(std::string_view agent, std::string_view version, int limit = 50);
    Json by_time(std::string_view start, std::string_view end, int limit = 50);
    Json search(std::string_view keyword, int limit = 50);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Json query(std::string_view predicate, const std::vector<std::string>& params, int limit);
};

} // namespace ark
