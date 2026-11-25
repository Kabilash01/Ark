#include "ark/kernel.hpp"

#include <chrono>
#include <atomic>
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

void native_terminal_tasks_require_explicit_opt_in() {
    auto terminal = task("exec", Json::array(), "terminal_agent");
    terminal["intent"] = "EXECUTE";
    terminal["instruction"] = "printf safe";
    const Json plan{{"tasks", {terminal}}};
    bool rejected = false;
    try { (void)ark::order_tasks(plan); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "native terminal task was accepted without opt-in");
    int calls = 0;
    const auto run = ark::supervise_plan(plan, [&](const ark::Task& work, const Json&, int, const Json&) {
        ++calls;
        check(work.agent == "terminal_agent" && work.intent == "EXECUTE" && work.instruction == "printf safe",
              "supervisor changed the terminal task contract");
        return Json{{"status", "DONE"}};
    }, [](std::string_view) { return true; }, 1, {}, true);
    check(calls == 1 && run.results.at("exec").at("status") == "DONE",
          "supervisor did not dispatch explicitly enabled terminal work");
    terminal["intent"] = "SECURITY";
    rejected = false;
    try { (void)ark::order_tasks({{"tasks", {terminal}}}, true); }
    catch (const std::invalid_argument& error) {
        rejected = std::string_view(error.what()).find("requires EXECUTE") != std::string_view::npos;
    }
    check(rejected, "terminal worker accepted a non-execution intent");
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

void retries_without_replaying_successful_tasks() {
    const Json plan{{"tasks", {task("retry"), task("after", {"retry"}), task("independent")}}};
    std::atomic<int> independent{0};
    const auto run = ark::supervise_plan(plan, [&](const ark::Task& t, const Json& prior, int attempt, const Json&) {
        if (t.id == "retry" && attempt < 3) {
            throw ark::HttpTimeout("Temporary timeout");
        }
        if (t.id == "after") {
            check(attempt == 1 && prior.at("retry").at("attempt") == 3, "dependent ran before recovery");
        }
        if (t.id == "independent") {
            ++independent;
        }
        return Json{{"status", "DONE"}, {"attempt", attempt}};
    }, [](std::string_view) { return true; });
    check(run.escalations.empty() && run.results.at("after").at("status") == "DONE"
          && independent == 1, run.results.dump());
}

void retries_the_graded_artifact_with_feedback() {
    auto evaluation = task("grade", {"code", "source"}, "evaluation_agent");
    evaluation["intent"] = "EVALUATE";
    const Json plan{{"tasks", {task("source"), task("code", {"source"}, "coding_agent"), evaluation}}};
    const auto run = ark::supervise_plan(plan, [&](const ark::Task& t, const Json& prior, int attempt, const Json& feedback) {
        if (t.id == "source") {
            check(attempt == 1, "source lookup should be retained");
        } else if (t.id == "code" && attempt == 2) {
            check(feedback.at("review") == "Make the output clearer" && feedback.at("tests") == Json{{"test.py", "fixed"}},
                  "revision did not receive evaluator feedback and fixed tests");
        } else if (t.id == "grade" && attempt == 1) {
            return Json{{"status", "RETRY"}, {"output", {{"evaluated_task", "code"}, {"summary", "Make the output clearer"}}}};
        } else if (t.id == "grade") {
            check(prior.at("code").at("attempt") == 2, "evaluation repeated on the old artifact");
        }
        return Json{{"status", "DONE"}, {"attempt", attempt}, {"output", {{"tests", {{"test.py", "fixed"}}}}}};
    }, [](std::string_view) { return true; });
    check(run.results.at("grade").at("status") == "DONE" && run.escalations.empty(), run.results.dump());
}

void escalates_exhaustion_and_permanent_errors() {
    const Json plan{{"tasks", {task("exhausted"), task("blocked", {"exhausted"}), task("permanent")}}};
    const auto run = ark::supervise_plan(plan, [&](const ark::Task& t, const Json&, int attempt, const Json&) {
        if (t.id == "permanent") {
            check(attempt == 1, "permanent failure should not be retried");
            return Json{{"status", "FAILED"}, {"reason", "Airgap blocks request"}};
        }
        check(t.id == "exhausted" && attempt <= 3, "unexpected attempt");
        return Json{{"status", "RETRY"}, {"reason", "Unavailable"}};
    }, [](std::string_view) { return true; });
    check(run.escalations.size() == 2 && run.escalations.at(0).at("attempts") == 3
          && run.escalations.at(1).at("attempts") == 1, run.escalations.dump());
    check(run.results.at("exhausted").at("status") == "FAILED"
          && run.results.at("blocked").at("status") == "SKIPPED", run.results.dump());
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

void planner_can_assign_an_isolated_terminal_command() {
    ark::Planner planner([](const ark::Request& request) {
        const auto body = Json::parse(request.body);
        const auto schema = body.at("format");
        const auto& task_schema = schema.at("properties").at("tasks").at("items");
        check(task_schema.at("properties").at("agent").at("enum") == Json::array({"terminal_agent"}),
              "planner terminal task schema did not constrain its worker");
        check(task_schema.at("properties").at("intent").at("enum") == Json::array({"EXECUTE"}),
              "planner terminal task schema did not constrain its intent");
        auto terminal = task("run", Json::array(), "terminal_agent");
        terminal["intent"] = "EXECUTE";
        terminal["instruction"] = "printf safe";
        return ark::Response{200, Json{{"message", {{"content", Json{{"tasks", {terminal}}}.dump()}}}}.dump()};
    }, "http://localhost:11434", "local-test");
    const auto plan = planner.plan("run the requested command", {"terminal_agent"}, Json::array(), true);
    check(plan.at("tasks").at(0).at("agent") == "terminal_agent"
          && plan.at("tasks").at(0).at("instruction") == "printf safe",
          "planner did not preserve the sandboxed command task");
}

void reconciles_output_conflicts_with_both_workers() {
    auto code = task("code", {"research"}, "coding_agent");
    code["intent"] = "CODE";
    const Json plan{{"tasks", {task("research"), code, task("dependent", {"code"}), task("unrelated")}}};
    for (const bool can_resolve : {true, false}) {
        int verdicts = 0;
        int unrelated = 0;
        const auto run = ark::supervise_plan(plan, [&](const ark::Task& work, const Json&, int attempt, const Json& feedback) {
            if (work.id == "unrelated") ++unrelated;
            if (attempt > 1 && (work.id == "research" || work.id == "code")) {
                const auto& comparison = feedback.at("output_conflicts").at(0);
                check(comparison.at("peer_task") == (work.id == "research" ? "code" : "research"), comparison.dump());
                if (work.id == "code") check(feedback.at("tests").at("test_contract.py") == "fixed", "tests changed");
            }
            return Json{{"status", "DONE"}, {"output", {{"summary", work.id}, {"tests", {{"test_contract.py", "fixed"}}}}}};
        }, [](std::string_view) { return true; }, 1, [&](const Json& research, const Json& coding) {
            check(research.at("summary") == "research" && coding.at("summary") == "code", "wrong compared pair");
            ++verdicts;
            return Json{{"conflict", !can_resolve || verdicts == 1}, {"reason", "different observed outcome"}};
        });
        check(unrelated == 1 && verdicts == (can_resolve ? 2 : 3), "changed reconciliation limit or unrelated work replayed");
        check(run.conflicts.back().at("status") == (can_resolve ? "RESOLVED" : "HUMAN_REVIEW"), run.conflicts.dump());
        check(run.results.at("dependent").at("status") == (can_resolve ? "DONE" : "SKIPPED"), run.results.dump());
        if (!can_resolve) check(run.results.at("code").at("output").at("human_review_required") == true, run.results.dump());
    }
}

void optional_cloud_failure_falls_back_to_local() {
    int cloud = 0;
    int local = 0;
    ark::Planner model([&](const ark::Request& request) -> ark::Response {
        if (request.url.starts_with("https://api.groq.com/")) {
            ++cloud;
            check(request.body.find("private fixture") == std::string::npos, "cloud received local memory context");
            return {503, ""};
        }
        ++local;
        if (local == 2) check(request.body.find("private fixture") != std::string::npos, "fallback omitted local memory");
        const auto answer = local == 1 ? Json{{"intent", "RESEARCH"}} : Json{{"tasks", {task("t1")}}};
        return {200, Json{{"message", {{"content", answer.dump()}}}}.dump()};
    }, "http://localhost:11434", ark::supervisor_profile("fixture-local"), ark::planner_profile("fixture-local"),
        "fixture-cloud", "fixture-key");
    check(model.classify("goal") == "RESEARCH", "unavailable cloud blocked classification");
    check(model.plan("goal", {"research_agent"}, {"private fixture"}).at("tasks").size() == 1 && cloud == 2 && local == 2,
          "planner failed to use the local fallback");
}

} // namespace

int main() {
    try {
        validate_graph();
        native_terminal_tasks_require_explicit_opt_in();
        route_in_parallel();
        skip_blocked_tasks();
        retries_without_replaying_successful_tasks();
        retries_the_graded_artifact_with_feedback();
        escalates_exhaustion_and_permanent_errors();
        model_plans_are_validated();
        planner_can_assign_an_isolated_terminal_command();
        reconciles_output_conflicts_with_both_workers();
        optional_cloud_failure_falls_back_to_local();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
