#include "ark/kernel.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

Json task(std::string id, Json dependencies = Json::array(), std::string agent = "research_agent") {
    return {{"task_id", id}, {"agent", agent}, {"intent", "RESEARCH"}, {"instruction", "Find a source"},
            {"depends_on", dependencies}, {"parallel_with", Json::array()}};
}

Json diamond() {
    return {{"tasks", {task("last", {"left", "right"}), task("left", {"root"}),
                       task("right", {"root"}), task("root")}}};
}

void check_invalid(const Json& plan, std::string_view expected) {
    try {
        (void)ark::order_tasks(plan);
    } catch (const std::invalid_argument& error) {
        check(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
        return;
    }
    throw std::runtime_error("Invalid plan was accepted");
}

void validate_graph() {
    const auto layers = ark::order_tasks(diamond());
    check(layers.size() == 3 && layers[0][0].id == "root" && layers[1].size() == 2
          && layers[2][0].id == "last", "diamond ordered incorrectly");
    check_invalid({{"tasks", {task("a", {"b"}), task("b", {"a"})}}}, "cycle");
    check_invalid({{"tasks", {task("a", {"missing"})}}}, "Unknown dependency");
    check_invalid({{"tasks", {task("a"), task("a")}}}, "Duplicate task");
    check_invalid({{"tasks", {task("a", {"a"})}}}, "itself");
    check_invalid({{"tasks", {task("a", Json::array(), "made_up_worker")}}}, "Unknown worker");
}

void route_in_parallel() {
    std::mutex mutex;
    std::condition_variable partners;
    int middle_tasks = 0;
    const auto results = ark::dispatch_plan(diamond(), [&](const ark::Task& task, const Json& prior) {
        if (task.id == "left" || task.id == "right") {
            check(prior.at("root").at("output") == "root", "dependency data is missing");
            std::unique_lock lock(mutex);
            ++middle_tasks;
            partners.notify_all();
            check(partners.wait_for(lock, std::chrono::seconds(3), [&] { return middle_tasks >= 2; }),
                  "independent tasks did not overlap");
        }
        if (task.id == "last") {
            check(prior.at("left").at("output") == "left" && prior.at("right").at("output") == "right",
                  "final task ran before dependencies");
        }
        return Json{{"status", "DONE"}, {"output", task.id}};
    }, [](std::string_view) { return true; }, 2);
    check(results.at("last").at("status") == "DONE", results.dump());
}

void skip_blocked_tasks() {
    const Json plan{{"tasks", {
        task("offline", Json::array(), "memory_agent"),
        task("blocked", {"offline"}), task("fails"), task("needs_failure", {"fails"}), task("independent"),
    }}};
    const auto result = ark::dispatch_plan(plan, [](const ark::Task& task, const Json&) {
        if (task.id == "fails") {
            throw std::runtime_error("worker error");
        }
        check(task.id == "independent", "blocked worker should not run");
        return Json{{"status", "DONE"}};
    }, [](std::string_view name) { return name != "memory_agent"; });
    for (const auto* skipped : {"offline", "blocked", "needs_failure"}) {
        check(result.at(skipped).at("status") == "SKIPPED", result.dump());
    }
    check(result.at("fails").at("status") == "FAILED", result.dump());
    check(result.at("independent").at("status") == "DONE", result.dump());
}

void model_plans_are_validated() {
    bool available = true;
    ark::Planner planner([&](const ark::Request& request) {
        check(request.url == "http://localhost:11434/api/chat", "unexpected model URL");
        const auto body = Json::parse(request.body);
        check(body.at("model") == "local-test", "wrong model configuration");
        const auto input = body.at("messages").at(1).at("content").get<std::string>();
        if (input != "goal") {
            check(body.at("format").at("properties").at("tasks").at("items").at("properties").at("agent").at("enum")
                      == Json::array({"research_agent"}), "planner must constrain models to available agents");
        }
        const Json response = input == "goal"
            ? Json{{"intent", "RESEARCH"}}
            : Json{{"tasks", {task("t1", Json::array(), available ? "research_agent" : "memory_agent")}}};
        return ark::Response{200, Json{{"message", {{"content", response.dump()}}}}.dump()};
    }, "http://localhost:11434/", "local-test");
    check(planner.classify("goal") == "RESEARCH", "incorrect intent");
    check(planner.plan("goal", {"research_agent"}).at("tasks").size() == 1, "plan not returned");

    available = false;
    try {
        (void)planner.plan("goal", {"research_agent"});
    } catch (const std::runtime_error& error) {
        check(std::string_view(error.what()).find("unavailable") != std::string_view::npos, error.what());
        return;
    }
    throw std::runtime_error("planner allowed an unavailable agent");
}

} // namespace

int main() {
    try {
        validate_graph();
        route_in_parallel();
        skip_blocked_tasks();
        model_plans_are_validated();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
