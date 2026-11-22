#pragma once

#include "ark/message.hpp"

#include <chrono>
#include <memory>

namespace ark {

struct BrowserOptions {
    std::string executable = "chromium";
    std::chrono::milliseconds timeout{10000};
    Json privacy = Json::object();
    bool private_mode = false;
    bool airgap = false;
};

class Browser {
public:
    explicit Browser(BrowserOptions options = {});
    ~Browser();
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    void navigate(std::string_view url);
    std::string get_page_text();
    std::string get_page_html();
    void click(std::string_view selector);
    void type_text(std::string_view selector, std::string_view text);
    std::string screenshot(); // PNG encoded as base64.
    void fill_form(const Json& fields);
    Json extract_links();
    Json extract_table(std::string_view selector);
    bool wait_for_element(std::string_view selector);
    void close_browser();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Impl& session();
};

} // namespace ark
