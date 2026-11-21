#include "ark/http.hpp"

#include <curl/curl.h>

#include <memory>
#include <regex>
#include <stdexcept>

namespace ark {
namespace {

constexpr std::size_t max_response_size = 8 * 1024 * 1024;

std::size_t receive(char* bytes, std::size_t size, std::size_t count, void* target) noexcept {
    auto& body = *static_cast<std::string*>(target);
    const auto length = size * count;
    if (length > max_response_size - body.size()) {
        return 0;
    }
    try {
        body.append(bytes, length);
        return length;
    } catch (...) {
        return 0;
    }
}

struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Could not initialize libcurl");
        }
    }
    ~CurlGlobal() { curl_global_cleanup(); }
};

} // namespace

void Response::require_success(std::string_view action) const {
    if (status < 200 || status >= 300) {
        throw std::runtime_error(std::string(action) + " returned HTTP " + std::to_string(status));
    }
}

CurlTransport::CurlTransport() {
    static const CurlGlobal global;
    handle_ = curl_easy_init();
    if (!handle_) {
        throw std::runtime_error("Could not create libcurl client");
    }
}

CurlTransport::~CurlTransport() { curl_easy_cleanup(handle_); }

Response CurlTransport::operator()(const Request& request) {
    curl_easy_reset(handle_);
    curl_easy_setopt(handle_, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(handle_, CURLOPT_PROTOCOLS_STR, "https,http");
    curl_easy_setopt(handle_, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle_, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle_, CURLOPT_TIMEOUT, static_cast<long>(request.timeout.count()));
    curl_easy_setopt(handle_, CURLOPT_PROXY, "");
    curl_easy_setopt(handle_, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle_, CURLOPT_USERAGENT, "ARK-OS/0.1");
    curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, receive);
    Response response{};
    curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &response.body);

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
    for (const auto& header : request.headers) {
        auto* list = curl_slist_append(headers.get(), header.c_str());
        if (!list) {
            throw std::bad_alloc();
        }
        headers.release();
        headers.reset(list);
    }
    curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, headers.get());
    if (request.method != "GET") {
        curl_easy_setopt(handle_, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(handle_, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    }

    const auto result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP transport failed: ") + curl_easy_strerror(result));
    }
    curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

std::string url_encode(std::string_view input) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char character : input) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '-' || character == '_'
            || character == '.' || character == '~') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(digits[character >> 4]);
            result.push_back(digits[character & 15]);
        }
    }
    return result;
}

std::string local_base_url(std::string_view input) {
    static const std::regex loopback(R"(^http://(127\.0\.0\.1|localhost|\[::1\])(:[0-9]{1,5})?/?$)");
    if (!std::regex_match(input.begin(), input.end(), loopback)) {
        throw std::invalid_argument("Model and memory service URLs must use an HTTP loopback address");
    }
    return std::string(input.ends_with('/') ? input.substr(0, input.size() - 1) : input);
}

} // namespace ark
