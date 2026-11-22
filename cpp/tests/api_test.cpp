#include "ark/api_server.hpp"
#include "ark/research.hpp"
#include "ark/runtime_files.hpp"

#include <httplib.h>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

using ark::Json;
using namespace std::chrono_literals;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

Json body(const httplib::Result& response, int expected = 200) {
    check(static_cast<bool>(response), "missing HTTP response");
    check(response->status == expected, response->body);
    return Json::parse(response->body);
}

void test_api() {
    const auto temp = std::filesystem::temp_directory_path() / ("ark-api-" + ark::random_uuid());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{temp};
    const auto privacy = temp / "privacy_config.json";
    ark::write_json(privacy, {{"privacy_mode", false}, {"airgap_mode", false},
                              {"allowed_external", {"export.arxiv.org"}}});

    std::promise<int> bound;
    std::promise<void> allow_completion;
    const auto gate = allow_completion.get_future().share();
    std::promise<Json> audit;
    auto logged = audit.get_future();
    auto port = bound.get_future();
    std::jthread server([&](std::stop_token stop) {
        try {
            ark::serve_api(0, {
                privacy,
                [&](const Json& task) -> Json {
                    gate.wait_for(3s);
                    if (task.at("payload").at("goal") == "Exercise escalation") {
                        return {{"status", "FAILED"}, {"escalations", {{{"plan_task_id", "t1"}, {"attempts", 3}}}}};
                    }
                    return {{"status", "DONE"}, {"quality_score", 0.9}, {"goal", task.at("payload").at("goal")}};
                },
                [] { return Json::array({{{"name", "research_agent"}}}); },
                [](std::string_view name) {
                    return name == "research_agent" ? Json::array() : Json(nullptr);
                },
                [](std::string_view tier, std::string_view query, std::string_view, int limit) {
                    return Json::array({{{"tier", tier}, {"query", query}, {"limit", limit}}});
                },
                [&](const Json& conflict) { audit.set_value(conflict); },
            }, stop, [&](int assigned) { bound.set_value(assigned); });
        } catch (...) {
            try { bound.set_exception(std::current_exception()); } catch (...) {}
        }
    });
    check(port.wait_for(3s) == std::future_status::ready, "API did not bind a loopback port");
    const auto assigned = port.get();
    httplib::Client api("127.0.0.1", assigned);
    api.set_read_timeout(3, 0);

    httplib::ws::WebSocketClient events("ws://127.0.0.1:" + std::to_string(assigned) + "/ws");
    check(static_cast<bool>(events.connect()), "WebSocket handshake failed");
    events.set_read_timeout(3, 0);

    const auto agents = body(api.Get("/agents"));
    check(agents.at(0).at("name") == "research_agent", "registry was not exposed");
    (void)body(api.Get("/agents/nonexistent/versions"), 404);
    const auto results = body(api.Post("/memory/search", R"({"query":"attention","top_k":9})", "application/json"));
    check(results.at("results").at(0).at("limit") == 9, "memory limit not forwarded");

    const auto task = body(api.Post("/task", R"({"goal":"Research attention","privacy":true,"test_files":{"test_fixture.py":"fixture"}})", "application/json"));
    const auto first_id = task.at("task_id").get<std::string>();
    check(task.at("status") == "PENDING" && task.at("payload").at("privacy") == true, "task response is invalid");
    check(task.at("payload").at("test_files").at("test_fixture.py") == "fixture", "test files were not retained");
    std::string event;
    check(static_cast<bool>(events.read(event)), "missing task creation event");
    check(Json::parse(event).at("event") == "task_created", "creation event was not emitted first");
    check(static_cast<bool>(events.read(event)) && Json::parse(event).at("event") == "task_started",
          "the first task must be running before queueing competitors");

    const auto queued = body(api.Post("/task", R"({"goal":"Research Fibonacci"})", "application/json"));
    const auto cancelled = body(api.Delete("/task/" + queued.at("task_id").get<std::string>()));
    check(cancelled.at("output").at("cancelled") == true, "queued task was not cancelled");
    const auto low = body(api.Post("/task", R"({"goal":"Low","priority":1})", "application/json"));
    const auto high = body(api.Post("/task", R"({"goal":"High","priority":10,"domain":"research"})", "application/json"));
    const auto high_id = high.at("task_id");
    const auto low_id = low.at("task_id");
    check(high.at("priority") == 10 && high.at("payload").at("domain") == "research", "priority request was dropped");
    (void)body(api.Post("/task", R"({"goal":"Invalid","priority":1.5})", "application/json"), 422);
    allow_completion.set_value();
    auto result = task;
    for (int i = 0; i < 50 && result.at("status") != "DONE"; ++i) {
        result = body(api.Get("/task/" + first_id));
        std::this_thread::sleep_for(10ms);
    }
    check(result.at("status") == "DONE" && result.at("quality_score") == 0.9, "job result was not published");
    check(body(api.Get("/tasks?limit=1")).size() == 1, "listing limit was ignored");
    check(logged.wait_for(3s) == std::future_status::ready && logged.get().at("task_id") == high_id,
          "conflict audit should select the higher-priority job");
    bool saw_conflict = false;
    auto order = Json::array();
    for (int i = 0; i < 12; ++i) {
        check(static_cast<bool>(events.read(event)), "missing queued task events");
        const auto notification = Json::parse(event);
        if (notification.at("event") == "conflict_detected") {
            saw_conflict = notification.at("report").at("waiting").at(0).at("task_id") == low_id;
        }
        if (notification.at("event") == "task_started") {
            order.push_back(notification.at("task_id"));
        }
        if (notification.at("event") == "task_finished" && notification.at("task_id") == low_id) {
            break;
        }
    }
    check(saw_conflict && order == Json::array({high_id, low_id}), "priority order or conflict event was incorrect");

    const auto failed = body(api.Post("/task", R"({"goal":"Exercise escalation"})", "application/json"));
    const auto failed_id = failed.at("task_id").get<std::string>();
    bool saw_report = false;
    for (int i = 0; i < 8; ++i) {
        check(static_cast<bool>(events.read(event)), "missing escalation events");
        const auto notification = Json::parse(event);
        if (notification.at("task_id") != failed_id) {
            continue;
        }
        if (notification.at("event") == "task_escalated") {
            saw_report = notification.at("report").at("attempts") == 3;
        }
        if (notification.at("event") == "task_finished") {
            break;
        }
    }
    check(saw_report && body(api.Get("/task/" + failed_id)).at("output").at("escalations").size() == 1,
          "escalation must be visible over WebSocket and REST");

    auto config = body(api.Post("/config/privacy", R"({"privacy_mode":true,"airgap_mode":true})", "application/json"));
    check(config.at("airgap_mode") == true && config.at("allowed_external").size() == 1
          && ark::read_json(privacy).at("privacy_mode") == true, "privacy config did not persist");
    const httplib::Headers foreign{{"Origin", "https://other.example"}};
    (void)body(api.Get("/tasks", foreign), 403);
    events.close();
    server.request_stop();
}

} // namespace

int main() {
    try {
        test_api();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
