#include "ark/browser.hpp"

#include <httplib.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

using ark::Json;
using namespace std::chrono_literals;

void check(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

struct Site {
    httplib::Server server;
    std::jthread thread;

    std::string start(const char* address) {
        const auto port = server.bind_to_any_port(address);
        check(port > 0, "fixture server could not bind");
        thread = std::jthread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
        return "http://" + std::string(address) + ":" + std::to_string(port);
    }

    ~Site() { server.stop(); }
};

void browser_actions(const char* executable) {
    Site site;
    site.server.Get("/page", [](const auto&, httplib::Response& response) {
        response.set_content(R"html(
            <!doctype html>
            <h1>ARK browser fixture</h1>
            <a href="/read">Read more</a>
            <table id="rows"><tr><th>Name</th><th>Score</th></tr><tr><td>Alpha</td><td>7</td></tr></table>
            <form>
                <input id="name" aria-label="Name">
                <textarea id="message" aria-label="Message"></textarea>
                <select id="mode" aria-label="Mode"><option value="one">One</option><option value="two">Two</option></select>
                <label><input type="checkbox" id="enabled">Enabled</label>
                <button id="apply" type="button">Apply</button>
            </form>
            <output id="result" style="display:none"></output>
            <script>
                document.querySelector("#apply").onclick = () => {
                    setTimeout(() => {
                        const output = document.querySelector("#result");
                        const field = (name) => document.querySelector(name);
                        output.value = [field("#name").value, field("#message").value, field("#mode").value,
                                        field("#enabled").checked].join("|");
                        output.style.display = "block";
                    }, 60);
                };
            </script>)html", "text/html; charset=utf-8");
    });
    site.server.Get("/redirect", [](const auto&, httplib::Response& response) { response.set_redirect("/page"); });
    const auto url = site.start("127.0.0.1");
    ark::Browser browser({executable, 3000ms});
    browser.navigate(url + "/redirect");
    check(browser.get_page_text().find("ARK browser fixture") != std::string::npos, "page text missed heading");
    check(browser.get_page_html().find("id=\"apply\"") != std::string::npos, "page HTML missed button");
    check(browser.extract_links() == Json::array({{{"text", "Read more"}, {"url", url + "/read"}}}),
          "relative link was not resolved");
    check(browser.extract_table("#rows") == Json::array({Json::array({"Name", "Score"}), Json::array({"Alpha", "7"})}),
          "table extraction changed");
    browser.fill_form({{"#name", "Ada"}, {"#message", "Olá "}, {"#mode", "two"}, {"#enabled", true}});
    browser.type_text("#message", "世界");
    browser.click("#apply");
    check(browser.wait_for_element("#result"), "browser could not wait for a dynamic result");
    check(browser.get_page_text().find("Ada|Olá 世界|two|true") != std::string::npos, "form data or real click did not arrive");
    check(browser.screenshot().starts_with("iVBORw0KGgo"), "screenshot was not a PNG");
    check(!browser.wait_for_element("#missing"), "absent element was reported visible");
    browser.navigate(url + "/page#section");
    check(browser.get_page_text().find("ARK browser fixture") != std::string::npos, "same-document navigation failed");
    browser.close_browser();
    browser.close_browser();
    bool closed = false;
    try { (void)browser.get_page_text(); } catch (const std::logic_error&) { closed = true; }
    check(closed, "a closed browser accepted operations");
}

void restricts_browser_network(const char* executable) {
    Site outside;
    std::atomic<int> requests{0};
    outside.server.Get("/probe", [&](const auto&, httplib::Response& response) {
        ++requests;
        response.set_content("fixture reached", "text/plain");
    });
    const auto denied = outside.start("127.0.0.2") + "/probe";
    Site local;
    local.server.Get("/redirect", [&](const auto&, httplib::Response& response) { response.set_redirect(denied); });
    local.server.Get("/embed", [&](const auto&, httplib::Response& response) {
        response.set_content("<!doctype html><p id='status'>Awaiting</p><img src='" + denied
            + R"html(' onerror="document.querySelector('#status').textContent='Blocked'; document.body.id='ready'">)html",
            "text/html");
    });
    const auto allowed = local.start("127.0.0.1");
    const Json privacy{{"allowed_external", Json::array({"127.0.0.2"})}};
    {
        ark::Browser browser({executable, 3000ms, privacy, true, false});
        browser.navigate(denied);
        check(browser.get_page_text() == "fixture reached" && requests == 1, "private browser rejected its exact allowlist");
    }
    {
        ark::Browser browser({executable, 3000ms, privacy, false, true});
        bool blocked = false;
        try { browser.navigate(denied); } catch (const std::invalid_argument&) { blocked = true; }
        check(blocked, "airgap accepted direct navigation to an outside host");
        blocked = false;
        try { browser.navigate(allowed + "/redirect"); } catch (const std::runtime_error&) { blocked = true; }
        check(blocked, "airgap allowed a redirect outside the loopback policy");
        browser.navigate(allowed + "/embed");
        check(browser.wait_for_element("body#ready") && requests == 1, "airgap allowed an outside resource request");
        blocked = false;
        try { browser.navigate("file:///etc/hosts"); } catch (const std::invalid_argument&) { blocked = true; }
        check(blocked, "browser accepted a non-HTTP URL");
    }
}

} // namespace

int main() {
    const auto* executable = std::getenv("ARK_TEST_CHROMIUM");
    if (!executable) {
        std::cout << "Set ARK_TEST_CHROMIUM to enable the Chromium integration tests\n";
        return 77;
    }
    try {
        browser_actions(executable);
        restricts_browser_network(executable);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
