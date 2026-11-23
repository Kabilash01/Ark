#include "ark/browser_agent.hpp"

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

Json request(std::string_view instruction, bool airgap = false) {
    return ark::make_message("test", "browser_agent", "BROWSE", {{"instruction", instruction}, {"airgap_mode", airgap}});
}

ark::Response action_reply(const Json& action) {
    return {200, Json{{"message", {{"content", action.dump()}}}}.dump()};
}

void validates_before_running_a_model() {
    int calls = 0;
    const ark::BrowserAgent browser([&](const auto&) { ++calls; return ark::Response{503, "{}"}; },
        "http://localhost:11434", ark::browser_profile("fixture-model"));
    check(ark::browser_start_url("Visit https://example.test/page.") == "https://example.test/page", "URL punctuation was retained");
    check(ark::browser_start_url("Browse arXiv") == "https://arxiv.org/", "arXiv hostname not identified");
    const auto missing = browser.run(request("Open a page"));
    check(missing.at("status") == "FAILED" && calls == 0, "browser requested a model without a site");
    const auto private_result = browser.run(request("Open https://arxiv.org/", true));
    check(private_result.at("status") == "FAILED" && calls == 0, "airgap requested outside browsing");
    const auto temporary = browser.run(request("Open https://arxiv.org/"));
    check(temporary.at("status") == "RETRY" && calls == 1, "transient model failure was not retryable");
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

void observes_and_extracts_from_real_chromium(const char* executable) {
    Site other;
    std::atomic<int> outside_requests = 0;
    other.server.Get("/resource", [&](const auto&, httplib::Response& response) {
        ++outside_requests;
        response.set_content("outside fixture", "text/plain");
    });
    const auto blocked = other.start("127.0.0.2") + "/resource";
    Site site;
    site.server.Get("/papers", [&](const auto&, httplib::Response& response) {
        response.set_content(R"html(<!doctype html>
            <title>Fixture paper list</title>
            <h1>Published papers</h1>
            <p class="paper-title">First fixture paper</p>
            <p class="paper-title">Second fixture paper</p>
            <p class="paper-title">Third fixture paper</p>
            <input id="label" aria-label="Label">
            <button id="apply">Apply</button><output id="observed"></output>
            <script>document.querySelector("#apply").onclick = () => {
                document.querySelector("#observed").value = document.querySelector("#label").value;
            };</script><img src=")html" + blocked + "\">", "text/html");
    });
    const auto url = site.start("127.0.0.1") + "/papers";
    int step = 0;
    const ark::BrowserAgent agent([&](const ark::Request& call) {
        const auto body = Json::parse(call.body);
        check(call.url == "http://localhost:11434/api/chat" && body.at("model") == "fixture-browser", "unexpected browser model route");
        const auto state = Json::parse(body.at("messages").at(1).at("content").get<std::string>());
        check(state.at("start_url") == url && state.at("allowed_host") == "127.0.0.1", "wrong user site in model state");
        check(state.at("requested_count") == 2, "explicit count was not passed to the model");
        const auto& observation = state.at("observation");
        switch (step++) {
        case 0: return action_reply({{"action", "navigate"}, {"url", blocked}});
        case 1:
            check(observation.at("error").get<std::string>().find("outside") != std::string::npos, "scope denial was hidden");
            return action_reply({{"action", "navigate"}, {"url", url}});
        case 2:
            check(observation.at("page").at("title") == "Fixture paper list", "navigation result never reached the model");
            return action_reply({{"action", "done"}});
        case 3:
            check(observation.at("error").get<std::string>().find("Capture evidence") != std::string::npos, "premature completion was permitted");
            return action_reply({{"action", "type"}, {"selector", "#label"}, {"text", "fixture interaction"}});
        case 4: return action_reply({{"action", "click"}, {"selector", "#apply"}});
        case 5:
            check(observation.at("page").at("text").get<std::string>().find("fixture interaction") != std::string::npos,
                  "form interaction did not reach real page");
            check(observation.at("page").at("elements").dump().find("p.paper-title") != std::string::npos,
                  "page observation omitted matching paper selectors");
            return action_reply({{"action", "extract"}, {"kind", "text"}, {"selector", ".paper-title"}});
        case 6:
            check(observation.at("result").at("count") == 2, "extraction was not visible to the model");
            return action_reply({{"action", "screenshot"}});
        default:
            check(state.at("captures").at("screenshots") == 1, "screenshot state was not retained");
            check(observation.dump().find("iVBORw0") == std::string::npos, "screenshot bytes leaked into model context");
            return action_reply({{"action", "done"}});
        }
    }, "http://localhost:11434", ark::browser_profile("fixture-browser"), {.executable = executable, .timeout = 3000ms});
    auto activity = Json::array();
    const auto result = agent.run(request("Open " + url + " and extract the first two paper titles"), [&](auto action, auto status) {
        activity.push_back(Json::array({action, status}));
    });
    check(result.at("status") == "DONE" && result.at("agent_version") == "browser_agent@1.0.0",
          result.at("output").value("error_message", std::string("unexpected browser result")));
    const auto& output = result.at("output");
    const auto& extraction = output.at("extractions").at(0);
    check(extraction.at("items") == Json::array({"First fixture paper", "Second fixture paper"}) && extraction.at("url") == url,
          "browser agent did not return the exact evidence");
    check(output.at("screenshots").at(0).at("data").get_ref<const std::string&>().starts_with("iVBORw0KGgo"),
          "browser output did not contain the captured PNG");
    check(output.at("actions").size() == 8 && activity.size() == 16 && outside_requests == 0,
          "browser opened a resource outside the requested host");
}

} // namespace

int main() {
    try {
        validates_before_running_a_model();
        if (const auto* executable = std::getenv("ARK_TEST_CHROMIUM")) {
            observes_and_extracts_from_real_chromium(executable);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
