#pragma once

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ark {

struct Request {
    std::string url;
    std::string method = "GET";
    std::string body;
    std::vector<std::string> headers;
    std::chrono::seconds timeout{30};
    std::string unix_socket;
};

class TransientHttpError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class HttpTimeout : public TransientHttpError {
public:
    using TransientHttpError::TransientHttpError;
};

struct Response {
    long status;
    std::string body;

    void require_success(std::string_view action) const;
};

using Transport = std::function<Response(const Request&)>;

// Reuses connections. Use one client per worker thread.
class CurlTransport {
public:
    CurlTransport();
    ~CurlTransport();
    CurlTransport(const CurlTransport&) = delete;
    CurlTransport& operator=(const CurlTransport&) = delete;

    Response operator()(const Request& request);

private:
    void* handle_;
};

std::string url_encode(std::string_view input);
std::string local_base_url(std::string_view input);

} // namespace ark
