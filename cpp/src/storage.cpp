#include "ark/storage.hpp"
#include "ark/vector_memory.hpp"

#include <hiredis/hiredis.h>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {

Json redis_value(const redisReply& reply) {
    switch (reply.type) {
    case REDIS_REPLY_NIL:
        return nullptr;
    case REDIS_REPLY_INTEGER:
        return reply.integer;
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_STRING:
        return std::string(reply.str, reply.len);
    case REDIS_REPLY_ARRAY: {
        auto values = Json::array();
        for (std::size_t i = 0; i < reply.elements; ++i) {
            values.push_back(redis_value(*reply.element[i]));
        }
        return values;
    }
    case REDIS_REPLY_ERROR:
        throw std::runtime_error(std::string("Redis error: ") + std::string(reply.str, reply.len));
    default:
        throw std::runtime_error("Unexpected Redis reply");
    }
}

std::string glob_escape(std::string_view value) {
    std::string escaped;
    for (char character : value) {
        if (std::string_view("*?[]\\").find(character) != std::string_view::npos) {
            escaped += '\\';
        }
        escaped += character;
    }
    return escaped;
}

std::vector<std::string> scan(Redis& redis, const std::string& pattern) {
    std::vector<std::string> keys;
    std::string cursor = "0";
    do {
        const auto response = redis.command({"SCAN", cursor, "MATCH", pattern, "COUNT", "256"});
        cursor = response.at(0).get<std::string>();
        for (const auto& key : response.at(1)) {
            keys.push_back(key.get<std::string>());
        }
    } while (cursor != "0");
    return keys;
}

std::string working_key(std::string_view task_id, std::string_view key) {
    return "working:" + std::string(task_id) + ":" + std::string(key);
}

Json parse_list(const Json& items) {
    auto results = Json::array();
    for (const auto& item : items) {
        results.push_back(Json::parse(item.get_ref<const std::string&>()));
    }
    return results;
}

std::string agent_key(std::string_view agent) { return "registry:" + std::string(agent); }
std::string agent_channel(std::string_view agent) { return "agent:" + std::string(agent); }
std::string version_key(std::string_view agent) { return "registry:version_history:" + std::string(agent); }

constexpr std::string_view set_registered_field = R"lua(
    if redis.call('EXISTS', KEYS[1]) == 0 then return 0 end
    redis.call('HSET', KEYS[1], ARGV[1], ARGV[2])
    return 1
)lua";

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void sqlite_check(int result, sqlite3* connection) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(connection));
    }
}

Statement prepare(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
    Statement prepared(statement, sqlite3_finalize);
    sqlite_check(result, db);
    return prepared;
}

void bind_text(sqlite3* db, sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite_check(sqlite3_bind_text64(statement, index, value.c_str(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8), db);
}

std::string sqlite_text(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    return text ? std::string(reinterpret_cast<const char*>(text), sqlite3_column_bytes(statement, column)) : "";
}

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

Database open_database(const std::filesystem::path& db) {
    if (!db.parent_path().empty()) {
        std::filesystem::create_directories(db.parent_path());
    }
    sqlite3* connection = nullptr;
    const int result = sqlite3_open(db.string().c_str(), &connection);
    Database result_connection(connection, sqlite3_close);
    if (!connection) {
        throw std::runtime_error("SQLite failed to allocate a connection");
    }
    sqlite_check(result, connection);
    sqlite_check(sqlite3_busy_timeout(connection, 5000), connection);
    return result_connection;
}

} // namespace

struct Redis::Impl {
    std::unique_ptr<redisContext, decltype(&redisFree)> context{nullptr, redisFree};
};

Redis::Redis(int port, int database) : impl_(std::make_unique<Impl>()) {
    if (port < 1 || port > 65535 || database < 0) {
        throw std::invalid_argument("Invalid Redis port or database");
    }
    const timeval timeout{5, 0};
    impl_->context.reset(redisConnectWithTimeout("127.0.0.1", port, timeout));
    if (!impl_->context || impl_->context->err || redisSetTimeout(impl_->context.get(), timeout) != REDIS_OK) {
        throw std::runtime_error("Unable to connect to loopback Redis");
    }
    if (database != 0) {
        command({"SELECT", std::to_string(database)});
    }
}

Redis::~Redis() = default;

Json Redis::command(const std::vector<std::string>& args) {
    if (args.empty()) {
        throw std::invalid_argument("Redis command must include a verb");
    }
    std::vector<const char*> values;
    std::vector<std::size_t> lengths;
    for (const auto& arg : args) {
        values.push_back(arg.data());
        lengths.push_back(arg.size());
    }
    const std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommandArgv(impl_->context.get(), static_cast<int>(values.size()),
                                                 values.data(), lengths.data())), freeReplyObject);
    if (!reply) {
        throw std::runtime_error(std::string("Redis command failed: ") + impl_->context->errstr);
    }
    return redis_value(*reply);
}

Json Redis::next_message() {
    void* raw = nullptr;
    const auto result = redisGetReply(impl_->context.get(), &raw);
    const std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(static_cast<redisReply*>(raw), freeReplyObject);
    if (result != REDIS_OK || !reply) {
        throw std::runtime_error("Redis subscription receive failed");
    }
    return redis_value(*reply);
}

void WorkingMemory::set(std::string_view task_id, std::string_view key, const Json& value, int ttl) {
    if (ttl <= 0) {
        throw std::invalid_argument("Working memory TTL must be positive");
    }
    redis_.command({"SET", working_key(task_id, key), value.dump(), "EX", std::to_string(ttl)});
}

Json WorkingMemory::get(std::string_view task_id, std::string_view key) {
    const auto value = redis_.command({"GET", working_key(task_id, key)});
    return value.is_null() ? Json(nullptr) : Json::parse(value.get_ref<const std::string&>());
}

bool WorkingMemory::exists(std::string_view task_id, std::string_view key) {
    return redis_.command({"EXISTS", working_key(task_id, key)}) == 1;
}

Json WorkingMemory::all(std::string_view task_id) {
    const auto prefix = working_key(task_id, "");
    auto output = Json::object();
    for (const auto& key : scan(redis_, glob_escape(prefix) + "*")) {
        const auto value = redis_.command({"GET", key});
        if (!value.is_null()) {
            output[key.substr(prefix.size())] = Json::parse(value.get_ref<const std::string&>());
        }
    }
    return output;
}

void WorkingMemory::erase(std::string_view task_id, std::string_view key) {
    redis_.command({"DEL", working_key(task_id, key)});
}

void WorkingMemory::finish(std::string_view task_id, int ttl) {
    if (ttl <= 0) throw std::invalid_argument("Working memory TTL must be positive");
    for (const auto& key : scan(redis_, glob_escape(working_key(task_id, "")) + "*")) {
        redis_.command({"EXPIRE", key, std::to_string(ttl), "LT"});
    }
}

void WorkingMemory::clear(std::string_view task_id) {
    const auto keys = scan(redis_, glob_escape(working_key(task_id, "")) + "*");
    if (!keys.empty()) {
        std::vector<std::string> command{"DEL"};
        command.insert(command.end(), keys.begin(), keys.end());
        redis_.command(command);
    }
}

int MessageBus::publish(std::string_view agent, const Json& message) {
    return redis_.command({"PUBLISH", agent_channel(agent), message.dump()}).get<int>();
}

int MessageBus::broadcast(const Json& message) {
    return redis_.command({"PUBLISH", "ark:broadcast", message.dump()}).get<int>();
}

int MessageBus::heartbeat(std::string_view agent, std::string_view status) {
    return redis_.command({"PUBLISH", "ark:heartbeat", Json{
        {"agent_name", agent}, {"status", status}, {"timestamp", utc_timestamp()}
    }.dump()}).get<int>();
}

void MessageBus::subscribe(std::string_view agent) {
    redis_.command({"SUBSCRIBE", agent_channel(agent), "ark:broadcast"});
    redis_.next_message(); // The second SUBSCRIBE acknowledgement.
}

void MessageBus::subscribe_heartbeat() { redis_.command({"SUBSCRIBE", "ark:heartbeat"}); }

Json MessageBus::next_message() {
    const auto raw = redis_.next_message();
    if (!raw.is_array() || raw.size() != 3 || raw.at(0) != "message") {
        throw std::runtime_error("Unexpected Redis pub/sub frame");
    }
    return {{"channel", raw.at(1)}, {"data", Json::parse(raw.at(2).get_ref<const std::string&>())}};
}

void MessageBus::log(Json event) {
    if (!event.is_object()) {
        throw std::invalid_argument("Audit event must be a JSON object");
    }
    if (event.value("timestamp", std::string{}).empty()) {
        event["timestamp"] = utc_timestamp();
    }
    redis_.command({"RPUSH", "ark:audit_log", event.dump()});
}

Json MessageBus::audit(int limit) {
    if (limit <= 0) {
        throw std::invalid_argument("Audit limit must be positive");
    }
    return parse_list(redis_.command({"LRANGE", "ark:audit_log", std::to_string(-limit), "-1"}));
}

Json AgentRegistry::add(std::string_view name, const Json& capabilities, std::string_view model,
                        std::string_view version) {
    if (name.empty() || name.find(':') != std::string_view::npos || !capabilities.is_array()) {
        throw std::invalid_argument("Expected an agent name without colons and a capabilities array");
    }
    redis_.command({"HSET", agent_key(name), "name", std::string(name), "capabilities", capabilities.dump(),
                    "model", std::string(model), "version", std::string(version), "status", "IDLE",
                    "current_task", "", "last_heartbeat", utc_timestamp(), "load_count", "0", "avg_quality_score", "0.0"});
    return get(name);
}

void AgentRegistry::remove(std::string_view name) { redis_.command({"DEL", agent_key(name)}); }

Json AgentRegistry::get(std::string_view name) {
    const auto fields = redis_.command({"HGETALL", agent_key(name)});
    if (fields.empty()) {
        return nullptr;
    }
    auto record = Json::object();
    for (std::size_t i = 0; i < fields.size(); i += 2) {
        record[fields[i].get<std::string>()] = fields[i + 1];
    }
    record["capabilities"] = Json::parse(record.at("capabilities").get_ref<const std::string&>());
    record["load_count"] = std::stoll(record.at("load_count").get_ref<const std::string&>());
    record["avg_quality_score"] = std::stod(record.at("avg_quality_score").get_ref<const std::string&>());
    return record;
}

Json AgentRegistry::all() {
    auto result = Json::array();
    for (const auto& key : scan(redis_, "registry:*")) {
        if (!key.starts_with("registry:version_history:")) {
            auto record = get(key.substr(9));
            if (!record.is_null()) {
                result.push_back(std::move(record));
            }
        }
    }
    return result;
}

bool AgentRegistry::status(std::string_view name, std::string_view status) {
    return redis_.command({"EVAL", std::string(set_registered_field), "1", agent_key(name), "status",
                           std::string(status)}) == 1;
}

bool AgentRegistry::heartbeat(std::string_view name) {
    return redis_.command({"EVAL", std::string(set_registered_field), "1", agent_key(name), "last_heartbeat",
                           utc_timestamp()}) == 1;
}

bool AgentRegistry::record_quality(std::string_view name, double score) {
    if (!std::isfinite(score)) {
        throw std::invalid_argument("Quality score must be finite");
    }
    constexpr auto script = R"lua(
        local count = redis.call('HGET', KEYS[1], 'load_count')
        if not count then return 0 end
        local avg = redis.call('HGET', KEYS[1], 'avg_quality_score')
        local n = tonumber(count)
        local next_avg = (tonumber(avg) * n + tonumber(ARGV[1])) / (n + 1)
        redis.call('HSET', KEYS[1], 'load_count', n + 1, 'avg_quality_score', next_avg)
        return 1
    )lua";
    return redis_.command({"EVAL", script, "1", agent_key(name), Json(score).dump()}) == 1;
}

bool AgentRegistry::version(std::string_view name, std::string_view version, const Json& score,
                            std::string_view changed_by) {
    constexpr auto script = R"lua(
        local previous = redis.call('HGET', KEYS[1], 'version')
        if not previous then return 0 end
        local entry = cjson.decode(ARGV[1])
        entry.previous_version = previous
        redis.call('HSET', KEYS[1], 'version', entry.version)
        redis.call('RPUSH', KEYS[2], cjson.encode(entry))
        return 1
    )lua";
    const Json entry{{"version", version}, {"timestamp", utc_timestamp()},
                     {"benchmark_score", score}, {"changed_by", changed_by}};
    return redis_.command({"EVAL", script, "2", agent_key(name), version_key(name), entry.dump()}) == 1;
}

Json AgentRegistry::history(std::string_view name) {
    return parse_list(redis_.command({"LRANGE", version_key(name), "0", "-1"}));
}

std::vector<std::string> AgentRegistry::mark_stale(std::chrono::seconds after) {
    if (after < std::chrono::seconds::zero()) {
        throw std::invalid_argument("Heartbeat timeout cannot be negative");
    }
    const auto cutoff = std::chrono::system_clock::now() - after;
    std::vector<std::string> stale;
    for (const auto& record : all()) {
        if (record.at("status") == "OFFLINE") {
            continue;
        }
        const auto heartbeat = record.at("last_heartbeat").get<std::string>();
        std::tm parts{};
        std::istringstream stream(heartbeat);
        stream >> std::get_time(&parts, "%Y-%m-%dT%H:%M:%S");
#ifdef _WIN32
        const auto seconds = _mkgmtime(&parts);
#else
        const auto seconds = timegm(&parts);
#endif
        if (!stream.fail() && std::chrono::system_clock::from_time_t(seconds) >= cutoff) {
            continue;
        }
        const auto name = record.at("name").get<std::string>();
        // Compare the timestamp atomically so a racing heartbeat remains online.
        constexpr auto mark_if_unchanged = R"lua(
            if redis.call('HGET', KEYS[1], 'last_heartbeat') ~= ARGV[1]
               or redis.call('HGET', KEYS[1], 'status') == 'OFFLINE' then return 0 end
            redis.call('HSET', KEYS[1], 'status', 'OFFLINE')
            return 1
        )lua";
        if (redis_.command({"EVAL", mark_if_unchanged, "1", agent_key(name), heartbeat}) == 1) {
            stale.push_back(name);
        }
    }
    return stale;
}

struct EpisodicMemory::Impl {
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection{nullptr, sqlite3_close};
    std::mutex mutex;
};

EpisodicMemory::EpisodicMemory(const std::filesystem::path& db) : impl_(std::make_unique<Impl>()) {
    impl_->connection = open_database(db);
    auto* connection = impl_->connection.get();
    constexpr auto sql = R"sql(
        CREATE TABLE IF NOT EXISTS episodes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            task_id TEXT NOT NULL, agent_name TEXT NOT NULL, agent_version TEXT NOT NULL,
            intent TEXT NOT NULL, summary TEXT NOT NULL, quality_score REAL,
            timestamp TEXT NOT NULL, metadata TEXT
        );
        CREATE TABLE IF NOT EXISTS episodes_archive (
            id INTEGER PRIMARY KEY, task_id TEXT NOT NULL, agent_name TEXT NOT NULL,
            agent_version TEXT NOT NULL, intent TEXT NOT NULL, summary TEXT NOT NULL,
            quality_score REAL, timestamp TEXT NOT NULL, metadata TEXT
        );
        CREATE TRIGGER IF NOT EXISTS episodes_retention AFTER INSERT ON episodes
        WHEN new.id - (SELECT min(id) FROM episodes) >= 10000 BEGIN
            INSERT INTO episodes_archive SELECT * FROM episodes WHERE id <= (
                SELECT id FROM episodes ORDER BY id DESC LIMIT 1 OFFSET 10000
            );
            DELETE FROM episodes WHERE id <= (SELECT max(id) FROM episodes_archive);
        END;
    )sql";
    sqlite_check(sqlite3_exec(connection, sql, nullptr, nullptr, nullptr), connection);
}

EpisodicMemory::~EpisodicMemory() = default;

std::int64_t EpisodicMemory::log(const Json& episode) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->connection.get();
    auto statement = prepare(db, R"sql(
        INSERT INTO episodes (task_id, agent_name, agent_version, intent, summary, quality_score, timestamp, metadata)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )sql");
    int index = 1;
    for (const auto* field : {"task_id", "agent_name", "agent_version", "intent", "summary"}) {
        bind_text(db, statement.get(), index++, episode.at(field).get_ref<const std::string&>());
    }
    if (episode.contains("quality_score") && !episode.at("quality_score").is_null()) {
        sqlite_check(sqlite3_bind_double(statement.get(), 6, episode.at("quality_score").get<double>()), db);
    }
    bind_text(db, statement.get(), 7, episode.value("timestamp", utc_timestamp()));
    bind_text(db, statement.get(), 8, episode.value("metadata", Json::object()).dump());
    sqlite_check(sqlite3_step(statement.get()), db);
    return sqlite3_last_insert_rowid(db);
}

Json EpisodicMemory::query(std::string_view predicate, const std::vector<std::string>& params, int limit, bool archive) {
    if (limit < 0) {
        throw std::invalid_argument("Episode query limit cannot be negative");
    }
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->connection.get();
    const auto table = std::string(archive ? "episodes_archive" : "episodes");
    auto statement = prepare(db, "SELECT id,task_id,agent_name,agent_version,intent,summary,quality_score,"
                                 "timestamp,metadata FROM " + table + " WHERE " + std::string(predicate)
                                 + " ORDER BY timestamp DESC, id DESC LIMIT ?");
    int index = 1;
    for (const auto& param : params) {
        bind_text(db, statement.get(), index++, param);
    }
    sqlite_check(sqlite3_bind_int(statement.get(), index, limit), db);
    auto output = Json::array();
    for (int step; (step = sqlite3_step(statement.get())) != SQLITE_DONE;) {
        sqlite_check(step, db);
        const auto metadata = sqlite_text(statement.get(), 8);
        output.push_back({
            {"id", sqlite3_column_int64(statement.get(), 0)}, {"task_id", sqlite_text(statement.get(), 1)},
            {"agent_name", sqlite_text(statement.get(), 2)}, {"agent_version", sqlite_text(statement.get(), 3)},
            {"intent", sqlite_text(statement.get(), 4)}, {"summary", sqlite_text(statement.get(), 5)},
            {"quality_score", sqlite3_column_type(statement.get(), 6) == SQLITE_NULL
                ? Json(nullptr) : Json(sqlite3_column_double(statement.get(), 6))},
            {"timestamp", sqlite_text(statement.get(), 7)},
            {"metadata", metadata.empty() ? Json::object() : Json::parse(metadata)},
        });
    }
    return output;
}

Json EpisodicMemory::recent(int limit) { return query("1", {}, limit); }
Json EpisodicMemory::archived(int limit) { return query("1", {}, limit, true); }
Json EpisodicMemory::by_agent(std::string_view agent, int limit) {
    return query("agent_name = ?", {std::string(agent)}, limit);
}
Json EpisodicMemory::by_intent(std::string_view intent, int limit) {
    return query("intent = ?", {std::string(intent)}, limit);
}
Json EpisodicMemory::by_version(std::string_view agent, std::string_view version, int limit) {
    return query("agent_name = ? AND agent_version = ?", {std::string(agent), std::string(version)}, limit);
}
Json EpisodicMemory::by_time(std::string_view start, std::string_view end, int limit) {
    return query("timestamp >= ? AND timestamp <= ?", {std::string(start), std::string(end)}, limit);
}
Json EpisodicMemory::search(std::string_view keyword, int limit) {
    return query("summary LIKE ?", {"%" + std::string(keyword) + "%"}, limit);
}

Json EpisodicMemory::pending_improvements(int limit) {
    return query("agent_name = 'reflection_agent' AND json_extract(metadata, '$.self_improvement_status') = 'pending'",
                 {}, limit);
}

namespace {

constexpr auto skill_fields = "id,intent,description,steps,success_rate,use_count,last_used,source";

Json skill(sqlite3_stmt* statement) {
    return {
        {"skill_id", sqlite_text(statement, 0)}, {"intent", sqlite_text(statement, 1)},
        {"description", sqlite_text(statement, 2)}, {"steps", Json::parse(sqlite_text(statement, 3))},
        {"success_rate", sqlite3_column_double(statement, 4)}, {"use_count", sqlite3_column_int64(statement, 5)},
        {"last_used", sqlite3_column_type(statement, 6) == SQLITE_NULL ? Json(nullptr) : Json(sqlite_text(statement, 6))},
        {"source", sqlite_text(statement, 7)},
    };
}

std::vector<float> normalized(const Embeddings& embeddings, std::string_view text) {
    auto vector = embeddings.encode({std::string(text)}).at(0);
    double squares = 0.0;
    for (const float value : vector) {
        squares += static_cast<double>(value) * value;
    }
    if (squares <= 0.0 || !std::isfinite(squares)) {
        throw std::runtime_error("Skill embedding has invalid magnitude");
    }
    const auto length = std::sqrt(squares);
    for (auto& value : vector) {
        value = static_cast<float>(value / length);
    }
    return vector;
}

} // namespace

struct SemanticIndex::Impl {
    explicit Impl(const std::filesystem::path& path) : db(open_database(path)) {}
    Database db;
    std::mutex mutex;
};

SemanticIndex::SemanticIndex(const std::filesystem::path& path, std::string model)
    : impl_(std::make_unique<Impl>(path)), model_(std::move(model)) {
    constexpr auto schema = R"sql(
        CREATE TABLE IF NOT EXISTS semantic_points (
            model TEXT NOT NULL, id TEXT NOT NULL, payload TEXT NOT NULL, vector BLOB NOT NULL,
            first_seen INTEGER NOT NULL, use_count INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (model, id)
        );
        CREATE INDEX IF NOT EXISTS semantic_retention ON semantic_points (model, use_count, first_seen);
    )sql";
    auto* db = impl_->db.get();
    sqlite_check(sqlite3_exec(db, schema, nullptr, nullptr, nullptr), db);
}

SemanticIndex::~SemanticIndex() = default;

void SemanticIndex::store(const Json& points) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    const auto statement = prepare(db, R"sql(
        INSERT INTO semantic_points(model,id,payload,vector,first_seen) VALUES(?,?,?,?,unixepoch())
        ON CONFLICT(model,id) DO UPDATE SET payload=excluded.payload, vector=excluded.vector
    )sql");
    bind_text(db, statement.get(), 1, model_);
    sqlite_check(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr), db);
    try {
        for (const auto& point : points) {
            const auto vector = point.at("vector").get<std::vector<float>>();
            bind_text(db, statement.get(), 2, point.at("id").get<std::string>());
            bind_text(db, statement.get(), 3, point.at("payload").dump());
            sqlite_check(sqlite3_bind_blob64(statement.get(), 4, vector.data(), vector.size() * sizeof(float), SQLITE_TRANSIENT), db);
            sqlite_check(sqlite3_step(statement.get()), db);
            sqlite_check(sqlite3_reset(statement.get()), db);
        }
        sqlite_check(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr), db);
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

void SemanticIndex::used(const Json& rows) {
    auto ids = Json::array();
    for (const auto& row : rows) ids.push_back(row.at("id"));
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    const auto statement = prepare(db, R"sql(
        UPDATE semantic_points SET use_count=1 WHERE model=? AND use_count=0 AND id IN (SELECT value FROM json_each(?))
    )sql");
    bind_text(db, statement.get(), 1, model_);
    bind_text(db, statement.get(), 2, ids.dump());
    sqlite_check(sqlite3_step(statement.get()), db);
}

std::vector<std::string> SemanticIndex::unused(std::int64_t cutoff) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    const auto statement = prepare(db, "SELECT id FROM semantic_points WHERE model=? AND use_count=0 AND first_seen <= ? LIMIT 1000");
    bind_text(db, statement.get(), 1, model_);
    sqlite_check(sqlite3_bind_int64(statement.get(), 2, cutoff), db);
    std::vector<std::string> ids;
    for (int step; (step = sqlite3_step(statement.get())) != SQLITE_DONE;) {
        sqlite_check(step, db);
        ids.push_back(sqlite_text(statement.get(), 0));
    }
    return ids;
}

void SemanticIndex::remove(const std::vector<std::string>& ids) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    const auto statement = prepare(db, "DELETE FROM semantic_points WHERE model=? AND id IN (SELECT value FROM json_each(?))");
    bind_text(db, statement.get(), 1, model_);
    bind_text(db, statement.get(), 2, Json(ids).dump());
    sqlite_check(sqlite3_step(statement.get()), db);
}

struct ProceduralMemory::Impl {
    explicit Impl(const std::filesystem::path& path) : db(open_database(path)) {}
    Database db;
    std::mutex mutex;
};

ProceduralMemory::ProceduralMemory(const std::filesystem::path& path, Embeddings encoder)
    : impl_(std::make_unique<Impl>(path)), encoder_(std::move(encoder)) {
    constexpr auto schema = R"sql(
        CREATE TABLE IF NOT EXISTS skills (
            id TEXT PRIMARY KEY, model TEXT NOT NULL, intent TEXT NOT NULL, description TEXT NOT NULL,
            steps TEXT NOT NULL, success_rate REAL NOT NULL DEFAULT 0.5, use_count INTEGER NOT NULL DEFAULT 0,
            last_used TEXT, source TEXT NOT NULL, vector BLOB NOT NULL
        );
        CREATE INDEX IF NOT EXISTS skills_model ON skills (model);
        CREATE INDEX IF NOT EXISTS skills_group ON skills (model, intent, lower(trim(description)));
        CREATE TABLE IF NOT EXISTS skill_conflicts (
            id INTEGER PRIMARY KEY, previous_id TEXT NOT NULL REFERENCES skills(id),
            new_id TEXT NOT NULL REFERENCES skills(id), created_at TEXT NOT NULL, reviewed_at TEXT
        );
        CREATE TRIGGER IF NOT EXISTS skills_conflict_insert AFTER INSERT ON skills BEGIN
            INSERT INTO skill_conflicts(previous_id, new_id, created_at)
            SELECT prior.id, new.id, strftime('%Y-%m-%dT%H:%M:%SZ', 'now') FROM skills AS prior
            WHERE prior.model = new.model AND prior.intent = new.intent
              AND lower(trim(prior.description)) = lower(trim(new.description)) AND prior.steps != new.steps
            ORDER BY prior.success_rate DESC, prior.use_count DESC, prior.rowid LIMIT 1;
        END;
    )sql";
    auto* db = impl_->db.get();
    sqlite_check(sqlite3_exec(db, schema, nullptr, nullptr, nullptr), db);
}

ProceduralMemory::~ProceduralMemory() = default;

std::string ProceduralMemory::add(std::string_view intent, std::string_view description, const Json& steps,
                                  std::string_view source) {
    if (intent.empty() || description.empty() || !steps.is_array()
        || !std::all_of(steps.begin(), steps.end(), [](const Json& step) { return step.is_string(); })) {
        throw std::invalid_argument("Skill requires intent, description, and a list of step strings");
    }
    const auto vector = normalized(encoder_, description);
    const auto id = random_uuid();
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    auto statement = prepare(db, "INSERT INTO skills (id,model,intent,description,steps,source,vector) "
                                 "VALUES (?,?,?,?,?,?,?)");
    bind_text(db, statement.get(), 1, id);
    bind_text(db, statement.get(), 2, encoder_.model());
    bind_text(db, statement.get(), 3, std::string(intent));
    bind_text(db, statement.get(), 4, std::string(description));
    bind_text(db, statement.get(), 5, steps.dump());
    bind_text(db, statement.get(), 6, std::string(source));
    sqlite_check(sqlite3_bind_blob64(statement.get(), 7, vector.data(), vector.size() * sizeof(float), SQLITE_TRANSIENT), db);
    sqlite_check(sqlite3_step(statement.get()), db);
    return id;
}

Json ProceduralMemory::get(std::string_view id) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    auto statement = prepare(db, std::string("SELECT ") + skill_fields + " FROM skills WHERE id = ? AND model = ?");
    bind_text(db, statement.get(), 1, std::string(id));
    bind_text(db, statement.get(), 2, encoder_.model());
    const auto result = sqlite3_step(statement.get());
    sqlite_check(result, db);
    return result == SQLITE_ROW ? skill(statement.get()) : Json(nullptr);
}

Json ProceduralMemory::all(int limit) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    auto statement = prepare(db, std::string("SELECT ") + skill_fields + " FROM skills WHERE model = ? ORDER BY rowid LIMIT ?");
    bind_text(db, statement.get(), 1, encoder_.model());
    sqlite_check(sqlite3_bind_int(statement.get(), 2, limit), db);
    auto entries = Json::array();
    for (int result; (result = sqlite3_step(statement.get())) != SQLITE_DONE;) {
        sqlite_check(result, db);
        entries.push_back(skill(statement.get()));
    }
    return entries;
}

Json ProceduralMemory::pending_conflicts(int limit) {
    if (limit < 1 || limit > 1000) throw std::invalid_argument("Conflict limit must be between 1 and 1000");
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    auto statement = prepare(db, R"sql(
        SELECT c.id,c.created_at,n.intent,n.description,p.id,p.source,p.success_rate,n.id,n.source,n.success_rate
        FROM skill_conflicts AS c JOIN skills AS p ON c.previous_id=p.id JOIN skills AS n ON c.new_id=n.id
        WHERE c.reviewed_at IS NULL AND n.model=? ORDER BY c.id LIMIT ?
    )sql");
    bind_text(db, statement.get(), 1, encoder_.model());
    sqlite_check(sqlite3_bind_int(statement.get(), 2, limit), db);
    auto rows = Json::array();
    for (int result; (result = sqlite3_step(statement.get())) != SQLITE_DONE;) {
        sqlite_check(result, db);
        const auto version = [&](int i) {
            return Json{{"skill_id", sqlite_text(statement.get(), i)}, {"source", sqlite_text(statement.get(), i + 1)},
                        {"success_rate", sqlite3_column_double(statement.get(), i + 2)}};
        };
        rows.push_back({{"conflict_id", sqlite3_column_int64(statement.get(), 0)},
            {"created_at", sqlite_text(statement.get(), 1)}, {"intent", sqlite_text(statement.get(), 2)},
            {"description", sqlite_text(statement.get(), 3)}, {"previous", version(4)}, {"proposed", version(7)}});
    }
    return rows;
}

bool ProceduralMemory::review_conflict(std::int64_t id) {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    auto statement = prepare(db, R"sql(
        UPDATE skill_conflicts SET reviewed_at=? WHERE id=? AND reviewed_at IS NULL
        AND new_id IN (SELECT id FROM skills WHERE model=?)
    )sql");
    bind_text(db, statement.get(), 1, utc_timestamp());
    sqlite_check(sqlite3_bind_int64(statement.get(), 2, id), db);
    bind_text(db, statement.get(), 3, encoder_.model());
    sqlite_check(sqlite3_step(statement.get()), db);
    return sqlite3_changes(db) == 1;
}

std::size_t ProceduralMemory::evict_low_success() {
    const std::lock_guard lock(impl_->mutex);
    auto* db = impl_->db.get();
    const auto statement = prepare(db, R"sql(
        DELETE FROM skills WHERE model=? AND use_count>0 AND success_rate < 0.30
        AND NOT EXISTS (SELECT 1 FROM skill_conflicts WHERE previous_id=skills.id OR new_id=skills.id)
    )sql");
    bind_text(db, statement.get(), 1, encoder_.model());
    sqlite_check(sqlite3_step(statement.get()), db);
    return static_cast<std::size_t>(sqlite3_changes64(db));
}

Json ProceduralMemory::retrieve(std::string_view query, std::size_t top_k, std::size_t candidate_pool) {
    if (!top_k || !candidate_pool) {
        return Json::array();
    }
    auto* db = impl_->db.get();
    {
        const std::lock_guard lock(impl_->mutex);
        auto exists = prepare(db, "SELECT 1 FROM skills WHERE model = ? LIMIT 1");
        bind_text(db, exists.get(), 1, encoder_.model());
        const auto row = sqlite3_step(exists.get());
        sqlite_check(row, db);
        if (row == SQLITE_DONE) {
            return Json::array();
        }
    }
    const auto query_vector = normalized(encoder_, query);
    std::vector<float> candidate(query_vector.size());

    struct Match { double cosine; Json entry; };
    const auto higher = [](const Match& left, const Match& right) { return left.cosine > right.cosine; };
    std::vector<Match> matches;
    {
        const std::lock_guard lock(impl_->mutex);
        auto statement = prepare(db, std::string("SELECT ") + skill_fields + R"sql(,vector FROM (
            SELECT *, row_number() OVER (PARTITION BY intent, lower(trim(description))
                ORDER BY success_rate DESC, use_count DESC, rowid) AS rank
            FROM skills WHERE model = ?
        ) WHERE rank = 1)sql");
        bind_text(db, statement.get(), 1, encoder_.model());
        for (int result; (result = sqlite3_step(statement.get())) != SQLITE_DONE;) {
            sqlite_check(result, db);
            const auto bytes = candidate.size() * sizeof(float);
            if (static_cast<std::size_t>(sqlite3_column_bytes(statement.get(), 8)) != bytes) {
                throw std::runtime_error("Procedural memory embedding dimension changed");
            }
            std::memcpy(candidate.data(), sqlite3_column_blob(statement.get(), 8), bytes);
            double cosine = 0.0;
            for (std::size_t i = 0; i < candidate.size(); ++i) {
                cosine += static_cast<double>(query_vector[i]) * candidate[i];
            }
            if (matches.size() < candidate_pool) {
                matches.push_back({cosine, skill(statement.get())});
                std::push_heap(matches.begin(), matches.end(), higher);
            } else if (cosine > matches.front().cosine) {
                std::pop_heap(matches.begin(), matches.end(), higher);
                matches.back() = {cosine, skill(statement.get())};
                std::push_heap(matches.begin(), matches.end(), higher);
            }
        }
    }
    for (auto& match : matches) {
        match.cosine *= match.entry.at("success_rate").get<double>();
    }
    std::sort(matches.begin(), matches.end(), higher);
    auto results = Json::array();
    for (std::size_t i = 0; i < std::min(top_k, matches.size()); ++i) {
        results.push_back(std::move(matches[i].entry));
    }
    return results;
}

Json ProceduralMemory::record_use(std::string_view id, bool success) {
    {
        const std::lock_guard lock(impl_->mutex);
        auto* db = impl_->db.get();
        auto statement = prepare(db, R"sql(
            UPDATE skills SET success_rate = (success_rate * use_count + ?) / (use_count + 1),
            use_count = use_count + 1, last_used = ? WHERE id = ? AND model = ?
        )sql");
        sqlite_check(sqlite3_bind_double(statement.get(), 1, success ? 1.0 : 0.0), db);
        bind_text(db, statement.get(), 2, utc_timestamp());
        bind_text(db, statement.get(), 3, std::string(id));
        bind_text(db, statement.get(), 4, encoder_.model());
        sqlite_check(sqlite3_step(statement.get()), db);
    }
    return get(id);
}

} // namespace ark
