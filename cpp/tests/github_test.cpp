#include "ark/github_agent.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, std::string_view detail) {
    if (!condition) throw std::runtime_error(std::string(detail));
}

void github_api() {
    int calls = 0;
    const ark::GitHubAgent agent([&](const ark::Request& request) -> ark::Response {
        ++calls;
        check(request.url.starts_with("https://api.github.com/repos/example/project/"), "bad GitHub endpoint");
        check(std::find(request.headers.begin(), request.headers.end(), "Authorization: Bearer fixture-token")
              != request.headers.end(), "missing authorization");
        if (request.method == "GET") {
            check(request.url.ends_with("/issues?state=open&per_page=100&page=2"), "invalid open issue query");
            return {200, R"([{"number":1,"title":"Issue","html_url":"https://example.test/issues/1"},
                            {"number":2,"pull_request":{}}])"};
        }
        check(request.method == "POST" && request.url.ends_with("/pulls"), "bad pull request operation");
        const auto body = ark::Json::parse(request.body);
        check(body.at("head") == "feature" && body.at("base") == "main"
              && body.at("title") == "Change" && body.at("draft") == true, "invalid PR request body");
        return {201, R"({"number":22,"html_url":"https://example.test/pull/22"})"};
    }, "fixture-token");
    const auto run = [&](ark::Json request) {
        return agent.run(ark::make_message("user", "github_agent", "GITHUB", std::move(request)));
    };
    const auto bad = run({{"action", "get_open_issues"}, {"repository", "../bad"}});
    check(bad.at("status") == "FAILED" && calls == 0, "invalid repository reached GitHub");
    const auto issues = run({{"action", "get_open_issues"}, {"repository", "example/project"}, {"page", 2}});
    check(issues.at("status") == "DONE" && issues.at("output").at("issues").size() == 1, issues.dump());
    const auto pr = run({{"action", "create_pull_request"}, {"repository", "example/project"}, {"head", "feature"},
                         {"base", "main"}, {"title", "Change"}, {"draft", true}});
    check(pr.at("status") == "DONE" && pr.at("output").at("number") == 22 && calls == 2, pr.dump());
}

} // namespace

int main() {
    try {
        github_api();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
