#include "ark/vision_runtime.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

void reads_local_vision_capability_and_authenticates() {
    bool called = false;
    const auto result = ark::inspect_vision_runtime([&](const ark::Request& request) {
        called = true;
        check(request.url == "http://127.0.0.1:8080/props" && request.method == "GET", "wrong runtime metadata request");
        check(std::find(request.headers.begin(), request.headers.end(), "Authorization: Bearer test-key") != request.headers.end(), "API key was not sent as a bearer header");
        return ark::Response{200, R"({"model_alias":"qwen2.5vl-3b","modalities":{"vision":true,"audio":false},"media_marker":"<__media__>"})"};
    }, "http://127.0.0.1:8080", "test-key");
    check(called && result.at("vision_supported") == true && result.at("model") == "qwen2.5vl-3b"
        && result.at("media_marker") == "<__media__>", "vision runtime capability was not reported");
}

void rejects_remote_plain_http_and_malformed_metadata() {
    bool rejected = false;
    try { (void)ark::inspect_vision_runtime([](const ark::Request&) { return ark::Response{200, "{}"}; }, "http://example.com"); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "non-loopback runtime URL was accepted");
    rejected = false;
    try { (void)ark::inspect_vision_runtime([](const ark::Request&) { return ark::Response{200, R"({"modalities":{"vision":"yes"}})"}; }, "http://localhost:8080"); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "malformed vision metadata was accepted");
}
}

int main() {
    try { reads_local_vision_capability_and_authenticates(); rejects_remote_plain_http_and_malformed_metadata(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
