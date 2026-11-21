#include "ark/message.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <chrono>
#include <ctime>
#include <stdexcept>
#include <string>

namespace ark {
namespace {

std::string hex(const unsigned char* data, std::size_t count) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        result += digits[data[i] >> 4];
        result += digits[data[i] & 15];
    }
    return result;
}

std::string format_uuid(const unsigned char* bytes) {
    const auto data = hex(bytes, 16);
    return data.substr(0, 8) + "-" + data.substr(8, 4) + "-" + data.substr(12, 4)
           + "-" + data.substr(16, 4) + "-" + data.substr(20);
}

std::string utc(const char* format) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    std::array<char, 64> output{};
    if (!std::strftime(output.data(), output.size(), format, &parts)) {
        throw std::runtime_error("Could not format UTC timestamp");
    }
    return output.data();
}

} // namespace

std::string utc_timestamp() { return utc("%Y-%m-%dT%H:%M:%S+00:00"); }

Json make_message(std::string_view sender, std::string_view receiver, std::string_view intent,
                  Json payload, std::string_view parent) {
    std::array<unsigned char, 3> suffix{};
    if (RAND_bytes(suffix.data(), suffix.size()) != 1) {
        throw std::runtime_error("Could not generate task ID");
    }
    return {
        {"task_id", utc("ark_%Y%m%d_%H%M%S_") + hex(suffix.data(), suffix.size())},
        {"sender", sender}, {"receiver", receiver}, {"parent_task_id", parent}, {"intent", intent},
        {"payload", std::move(payload)}, {"memory_context", Json::array()}, {"priority", 5},
        {"status", "PENDING"}, {"retry_count", 0}, {"max_retries", 3},
        {"output", Json::object()}, {"quality_score", -1.0}, {"timestamp", utc_timestamp()},
        {"agent_version", ""},
    };
}

bool pending_for(const Json& message, std::string_view receiver) {
    if (!message.is_object()) {
        return false;
    }
    for (auto key : {"task_id", "sender", "receiver", "intent", "status", "timestamp"}) {
        if (!message.contains(key) || !message.at(key).is_string()) {
            return false;
        }
    }
    return message.contains("payload") && message.at("payload").is_object()
        && message.at("status") == "PENDING" && message.at("receiver") == receiver;
}

std::string point_id(std::string_view url) {
    // RFC UUID v5 with the standard URL namespace, shared with Python's uuid5.
    constexpr std::array<unsigned char, 16> url_namespace{
        0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1, 0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
    };
    std::string input(reinterpret_cast<const char*>(url_namespace.data()), url_namespace.size());
    input += url;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    if (EVP_Digest(input.data(), input.size(), digest.data(), nullptr, EVP_sha1(), nullptr) != 1) {
        throw std::runtime_error("Could not derive paper ID");
    }
    digest[6] = (digest[6] & 0x0f) | 0x50;
    digest[8] = (digest[8] & 0x3f) | 0x80;
    return format_uuid(digest.data());
}

std::string random_uuid() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("Could not generate UUID");
    }
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    return format_uuid(bytes.data());
}

} // namespace ark
