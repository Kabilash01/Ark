#include "ark/versions.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {

using Prompts = decltype(AgentProfile::prompts);

std::string agent_key(std::string_view agent) {
    if (agent.empty() || agent.find(':') != std::string_view::npos) {
        throw std::invalid_argument("Invalid agent name");
    }
    return "registry:" + std::string(agent);
}

std::string revisions_key(std::string_view agent) { return "ark:agent_revisions:" + std::string(agent); }
std::string pointers_key(std::string_view agent) { return "ark:agent_version_refs:" + std::string(agent); }
std::string history_key(std::string_view agent) { return "registry:version_history:" + std::string(agent); }

std::string sha256(std::string_view text) {
    std::array<unsigned char, 32> hash{};
    unsigned int length = 0;
    if (EVP_Digest(text.data(), text.size(), hash.data(), &length, EVP_sha256(), nullptr) != 1 || length != hash.size()) {
        throw std::runtime_error("Could not hash agent prompt");
    }
    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (const auto byte : hash) {
        hex << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return hex.str();
}

std::array<unsigned int, 3> parse_version(std::string_view version) {
    std::array<unsigned int, 3> parts{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto dot = version.find('.');
        if ((i == 2) != (dot == std::string_view::npos)) {
            throw std::invalid_argument("Expected version MAJOR.MINOR.PATCH");
        }
        const auto component = version.substr(0, dot);
        const auto parsed = std::from_chars(component.data(), component.data() + component.size(), parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != component.data() + component.size()
            || (component.size() > 1 && component[0] == '0')) {
            throw std::invalid_argument("Invalid version component");
        }
        if (i < 2) {
            version.remove_prefix(dot + 1);
        }
    }
    return parts;
}

std::string increment(std::string_view version, std::string_view change_type) {
    const int index = change_type == "major" ? 0 : change_type == "minor" ? 1 : change_type == "patch" ? 2 : -1;
    if (index < 0) {
        throw std::invalid_argument("Version change must be major, minor, or patch");
    }
    auto parts = parse_version(version);
    if (parts[index] == std::numeric_limits<unsigned int>::max()) {
        throw std::invalid_argument("Version component exhausted");
    }
    ++parts[index];
    std::fill(parts.begin() + index + 1, parts.end(), 0);
    return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]);
}

AgentProfile profile(const Json& record) {
    return {record.at("version").get<std::string>(), record.at("model").get<std::string>(),
            record.at("prompts").get<Prompts>()};
}

Json descriptor(Json revision) {
    revision.erase("prompts");
    return revision;
}

Json make_revision(std::string_view agent, const AgentProfile& agent_profile, const Json& score,
                   std::string_view actor, std::string_view summary, std::string_view previous) {
    if (!score.is_null() && (!score.is_number() || !std::isfinite(score.get<double>())
                            || score.get<double>() < 0 || score.get<double>() > 1)) {
        throw std::invalid_argument("Benchmark score must be null or between 0 and 1");
    }
    if (agent_profile.model.empty() || actor.empty() || summary.empty() || agent_profile.prompts.empty()) {
        throw std::invalid_argument("Version requires a model, prompts, changed_by, and change_summary");
    }
    const Json prompts = agent_profile.prompts;
    // For an agent with multiple prompts, hash their canonical named bundle.
    const auto prompt_bytes = agent_profile.prompts.size() == 1 ? agent_profile.prompts.begin()->second : prompts.dump();
    const auto now = utc_timestamp();
    return {{"agent_name", agent}, {"version", agent_profile.version}, {"model", agent_profile.model},
            {"prompt_hash", sha256(prompt_bytes)}, {"prompts", prompts}, {"benchmark_score", score},
            {"changed_by", actor}, {"created_at", now}, {"timestamp", now}, {"change_summary", summary},
            {"previous_version", previous}};
}

} // namespace

bool AgentVersioner::initialized(std::string_view agent) {
    agent_key(agent);
    return redis_.command({"HEXISTS", pointers_key(agent), "active"}) == 1;
}

AgentProfile AgentVersioner::ensure(std::string_view agent, const Json& capabilities, const AgentProfile& defaults) {
    const auto registry = agent_key(agent);
    auto initial = defaults;
    std::string previous;
    if (!initialized(agent)) {
        const auto default_version = parse_version(initial.version);
        std::string highest;
        const auto consider = [&highest](const std::string& candidate) {
            try {
                const auto parsed = parse_version(candidate);
                if (highest.empty() || parsed > parse_version(highest)) {
                    highest = candidate;
                }
            } catch (const std::invalid_argument&) {
                // Legacy metadata may have arbitrary labels; it remains visible in history.
            }
        };
        const auto current = redis_.command({"HGET", registry, "version"});
        if (current.is_string()) {
            previous = current.get<std::string>();
            if (previous != defaults.version) {
                consider(previous);
            }
        }
        for (const auto& entry : history(agent)) {
            consider(entry.value("version", ""));
        }
        if (!highest.empty() && parse_version(highest) >= default_version) {
            initial.version = increment(highest, "patch");
        }
        if (previous == initial.version) {
            previous.clear();
        }
    }
    const auto baseline = make_revision(agent, initial, nullptr, "initial", "Built-in prompts", previous);
    constexpr auto script = R"lua(
        local active = redis.call('HGET', KEYS[1], 'active')
        if not active then
            active = ARGV[1]
            redis.call('HSET', KEYS[2], active, ARGV[2])
            redis.call('HSET', KEYS[1], 'active', active, 'latest', active)
            redis.call('RPUSH', KEYS[3], ARGV[3])
        end
        local snapshot = redis.call('HGET', KEYS[2], active)
        local model = cjson.decode(snapshot).model
        if redis.call('EXISTS', KEYS[4]) == 0 then
            redis.call('HSET', KEYS[4], 'name', ARGV[4], 'capabilities', ARGV[5], 'status', 'IDLE',
                       'current_task', '', 'last_heartbeat', ARGV[6], 'load_count', '0', 'avg_quality_score', '0.0')
        end
        redis.call('HSET', KEYS[4], 'model', model, 'version', active)
        return snapshot
    )lua";
    const auto stored = redis_.command({"EVAL", script, "4", pointers_key(agent), revisions_key(agent), history_key(agent),
        registry, initial.version, baseline.dump(), descriptor(baseline).dump(), std::string(agent),
        capabilities.dump(), utc_timestamp()});
    return profile(Json::parse(stored.get<std::string>()));
}

Json AgentVersioner::revision(std::string_view agent, std::string_view version) {
    agent_key(agent);
    const auto stored = redis_.command({"HGET", revisions_key(agent), std::string(version)});
    if (stored.is_null()) {
        throw std::invalid_argument("No prompt snapshot for " + std::string(agent) + "@" + std::string(version));
    }
    return Json::parse(stored.get<std::string>());
}

Json AgentVersioner::bump(std::string_view agent, std::string_view change_type, const Json& change) {
    const auto registry = agent_key(agent);
    const auto versions = redis_.command({"HMGET", pointers_key(agent), "active", "latest"});
    if (versions.at(0).is_null()) {
        throw std::invalid_argument("Agent has no baseline prompt snapshot");
    }
    const auto active = versions.at(0).get<std::string>();
    const auto latest = versions.at(1).get<std::string>();
    auto next = profile(revision(agent, active));
    next.version = increment(latest, change_type);
    next.model = change.value("model", next.model);
    const auto patch = change.value("prompts", Json::object());
    if (!patch.is_object()) {
        throw std::invalid_argument("prompts must map prompt names to text");
    }
    for (const auto& [name, value] : patch.items()) {
        if (!next.prompts.contains(name) || !value.is_string()
            || value.get_ref<const std::string&>().find_first_not_of(" \t\r\n") == std::string::npos) {
            throw std::invalid_argument("Invalid prompt update for " + name);
        }
        next.prompts.at(name) = value.get<std::string>();
    }
    const auto created = make_revision(agent, next, change.value("benchmark_score", Json(nullptr)),
        change.value("changed_by", "manual"), change.at("change_summary").get<std::string>(), active);
    const auto output = descriptor(created);
    constexpr auto script = R"lua(
        if redis.call('HGET', KEYS[1], 'active') ~= ARGV[1]
            or redis.call('HGET', KEYS[1], 'latest') ~= ARGV[2]
            or redis.call('HEXISTS', KEYS[2], ARGV[3]) ~= 0 then return 0 end
        if redis.call('EXISTS', KEYS[4]) == 0 then return 0 end
        redis.call('HSET', KEYS[2], ARGV[3], ARGV[4])
        redis.call('RPUSH', KEYS[3], ARGV[5])
        redis.call('HSET', KEYS[1], 'active', ARGV[3], 'latest', ARGV[3])
        redis.call('HSET', KEYS[4], 'version', ARGV[3], 'model', ARGV[6])
        redis.call('RPUSH', KEYS[5], ARGV[7])
        redis.call('XADD', KEYS[6], 'MAXLEN', '~', '256', '*', 'report', ARGV[7])
        return 1
    )lua";
    const Json event{{"event", "version_updated"}, {"agent", agent}, {"version", next.version},
                     {"previous_version", active}, {"timestamp", utc_timestamp()}};
    if (redis_.command({"EVAL", script, "6", pointers_key(agent), revisions_key(agent), history_key(agent), registry,
        "ark:audit_log", "ark:version_events", active, latest, next.version, created.dump(), output.dump(), next.model,
        event.dump()}) != 1) {
        throw std::runtime_error("Agent changed while bumping version; reload its history and retry");
    }
    return output;
}

Json AgentVersioner::history(std::string_view agent) {
    agent_key(agent);
    return AgentRegistry(redis_).history(agent);
}

Json AgentVersioner::history_with_rollback_availability(std::string_view agent) {
    auto entries = history(agent);
    if (!entries.empty()) {
        const auto versions = redis_.command({"HKEYS", revisions_key(agent)}).get<std::set<std::string>>();
        for (auto& entry : entries) {
            entry["rollback_available"] = versions.contains(entry.at("version").get<std::string>());
        }
    }
    return entries;
}

Json AgentVersioner::rollback(std::string_view agent, std::string_view version) {
    const auto registry = agent_key(agent);
    const auto target = revision(agent, version);
    constexpr auto script = R"lua(
        local previous = redis.call('HGET', KEYS[1], 'active')
        if not previous or redis.call('EXISTS', KEYS[2]) == 0 then return 0 end
        if previous == ARGV[1] then return 1 end
        local event = cjson.decode(ARGV[3])
        event.previous_version = previous
        redis.call('HSET', KEYS[1], 'active', ARGV[1])
        redis.call('HSET', KEYS[2], 'version', ARGV[1], 'model', ARGV[2])
        local report = cjson.encode(event)
        redis.call('RPUSH', KEYS[3], report)
        redis.call('XADD', KEYS[4], 'MAXLEN', '~', '256', '*', 'report', report)
        return 1
    )lua";
    const Json event{{"event", "agent_version_rollback"}, {"agent", agent}, {"version", version},
                     {"timestamp", utc_timestamp()}};
    if (redis_.command({"EVAL", script, "4", pointers_key(agent), registry, "ark:audit_log", "ark:version_events",
        std::string(version), target.at("model").get<std::string>(), event.dump()}) != 1) {
        throw std::runtime_error("Could not activate prompt snapshot");
    }
    return descriptor(target);
}

Json AgentVersioner::compare(std::string_view agent, std::string_view from, std::string_view to) {
    const auto earlier = descriptor(revision(agent, from));
    const auto later = descriptor(revision(agent, to));
    Json delta = nullptr;
    if (earlier.at("benchmark_score").is_number() && later.at("benchmark_score").is_number()) {
        delta = later.at("benchmark_score").get<double>() - earlier.at("benchmark_score").get<double>();
    }
    return {{"from", earlier}, {"to", later}, {"benchmark_score_delta", delta}};
}

} // namespace ark
