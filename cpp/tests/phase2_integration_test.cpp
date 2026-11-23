#include "ark/browser_agent.hpp"
#include "ark/coding.hpp"
#include "ark/document_agent.hpp"
#include "ark/evaluation.hpp"
#include "ark/file_system.hpp"
#include "ark/github_agent.hpp"
#include "ark/reflection.hpp"
#include "ark/research.hpp"
#include "ark/security_agent.hpp"
#include "ark/storage.hpp"
#include "ark/terminal.hpp"
#include "ark/vector_memory.hpp"
#include "ark/web_search.hpp"

#include <httplib.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>

namespace {

using ark::Json;
namespace fs = std::filesystem;
constexpr auto model_url = "http://127.0.0.1:11434";
constexpr auto goal = "Review the evidence for SQL parameter binding, verify the patched example, and write a security audit.";
constexpr auto summary = "The fixture evidence recommends binding SQL values as parameters.";
constexpr auto paper_id = "9912.99999v1";
constexpr auto patched = R"py(def find_label(connection, name):
    return connection.execute("SELECT name FROM records WHERE name = ?", (name,)).fetchone()
)py";
constexpr auto tests = R"py(import sqlite3
import unittest
from patched_example import find_label

class Contract(unittest.TestCase):
    def test_exact_match(self):
        with sqlite3.connect(":memory:") as connection:
            connection.execute("CREATE TABLE records (name TEXT)")
            connection.executemany("INSERT INTO records (name) VALUES (?)", [("Oak",), ("O'Brien",)])
            self.assertEqual(find_label(connection, "O'Brien"), ("O'Brien",))
            self.assertIsNone(find_label(connection, "missing"))
)py";

void check(bool condition, std::string_view detail) {
    if (!condition) throw std::runtime_error(std::string(detail));
}

Json task(std::string_view id, std::string_view agent, std::string_view intent,
          std::string_view instruction, Json dependencies) {
    return {{"task_id", id}, {"agent", agent}, {"intent", intent}, {"instruction", instruction},
            {"depends_on", dependencies}, {"parallel_with", Json::array()}};
}

Json message(std::string_view actor, std::string_view intent, Json payload) {
    return ark::make_message("integration", actor, intent, std::move(payload));
}

ark::Response response(Json value) { return {200, value.dump()}; }

struct Site {
    httplib::Server server;
    std::jthread worker;
    std::string url;

    Site() {
        server.Get("/", [](const auto&, httplib::Response& reply) {
            reply.set_content("<title>Fixture evidence</title><h1 id=\"evidence\">Bind SQL parameters</h1>", "text/html");
        });
        const int port = server.bind_to_any_port("127.0.0.1");
        check(port > 0, "Could not bind browser fixture");
        url = "http://127.0.0.1:" + std::to_string(port) + "/";
        worker = std::jthread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~Site() { server.stop(); }
};

void run(const fs::path& artifacts, const char* chromium, const char* docker, int redis_port) {
    Site site;
    int browser_step = 0;
    bool research_persisted = false;
    const Json dag{{"tasks", Json::array({
        task("research", "research_agent", "RESEARCH", std::string("Review fixture arXiv:") + paper_id, Json::array()),
        task("code", "coding_agent", "CODE", "Implement parameterized lookup", {"research"}),
        task("evaluate", "evaluation_agent", "EVALUATE", "Verify the contract", {"code"}),
    })}};
    const ark::Transport provider = [&](const ark::Request& request) -> ark::Response {
        if (request.url.starts_with("https://export.arxiv.org/api/query")) {
            check(request.url.ends_with(paper_id), "Research requested the wrong fixture ID");
            return {200, R"xml(<feed xmlns="http://www.w3.org/2005/Atom"><entry>
                <id>https://arxiv.org/abs/9912.99999v1</id><title>Parameterized queries (integration fixture)</title>
                <summary>Bind values as SQL parameters in data access code.</summary>
                <author><name>Integration Fixture</name></author><published>2000-01-01</published></entry></feed>)xml"};
        }
        if (request.url == "https://api.tavily.com/search") {
            return response({{"results", Json::array({{{"url", "https://example.test/binding"},
                {"title", "Binding fixture"}, {"content", "Bind input as SQL values."}}})}});
        }
        if (request.url.starts_with("https://api.github.com/repos/example/project/issues")) {
            return response(Json::array({{{"number", 1}, {"title", "Verify parameter binding"},
                                          {"html_url", "https://example.test/project/issues/1"}}}));
        }
        if (request.url.starts_with("http://127.0.0.1:6333/collections/")) {
            if (request.url.ends_with("/points?wait=true") && request.method == "PUT") {
                const auto body = Json::parse(request.body);
                research_persisted = body.at("points").at(0).at("payload").at("arxiv_id") == paper_id;
            }
            return response(Json::object());
        }
        if (request.url.ends_with("/api/embed")) return response({{"embeddings", {{0.6, 0.8}}}});
        check(request.url.ends_with("/api/chat"), "Unrecognized integration fixture route");
        const auto call = Json::parse(request.body);
        const auto model = call.at("model").get<std::string>();
        Json output;
        if (model == "fixture-supervisor") output = {{"intent", "RESEARCH"}};
        else if (model == "fixture-planner") output = dag;
        else if (model == "fixture-research") {
            output = {{"summary", summary}, {"findings", Json::array({{{"text", "Bind parameters"}, {"arxiv_ids", {paper_id}}}})}};
        } else if (model == "fixture-search") {
            output = {{"summary", "The fixture search result corroborates binding."}, {"source_ids", {1}}};
        } else if (model == "fixture-code") {
            const auto input = Json::parse(call.at("messages").at(1).at("content").get<std::string>());
            check(input.at("locked_tests").at("test_lookup.py") == tests
                  && input.at("evidence").dump().find("Bind parameters") != std::string::npos,
                  "Code agent was missing fixed tests or research evidence");
            output = {{"filename", "patched_example.py"}, {"source", patched}};
        } else if (model == "fixture-evaluation") {
            output = {{"scores", {{"style", 1.0}, {"security", 1.0}}}, {"feedback", "The locked tests passed."}};
        } else if (model == "fixture-browser") {
            if (browser_step++ == 0) output = {{"action", "navigate"}, {"url", site.url}};
            else if (browser_step == 2) output = {{"action", "extract"}, {"kind", "text"}, {"selector", "#evidence"}};
            else output = {{"action", "done"}};
        } else if (model == "fixture-reflection") {
            output = {{"what_worked", "The patched example passed locked tests and static review."}, {"what_failed", ""},
                {"pattern_learned", "Pair parameter-binding tests with static analysis."}, {"skill_update", nullptr},
                {"flag_for_self_improvement", false}, {"agent_version_recommendation", nullptr}};
        } else throw std::runtime_error("Unexpected model " + model);
        return response({{"message", {{"content", output.dump()}}}});
    };

    std::map<std::string, std::string> activated;
    const auto complete = [&](const Json& result, std::string_view agent) -> Json {
        check(result.at("status") == "DONE", result.dump());
        activated[std::string(agent)] = result.at("agent_version").get<std::string>();
        return result.at("output");
    };

    const ark::Planner planner(provider, model_url, ark::supervisor_profile("fixture-supervisor"),
                                ark::planner_profile("fixture-planner"));
    check(planner.classify(goal) == "RESEARCH", "Supervisor did not classify the goal");
    activated["supervisor_agent"] = "supervisor_agent@1.0.0";
    const auto plan = planner.plan(goal, {"research_agent", "coding_agent", "evaluation_agent"}, Json::array());
    check(plan == dag, "Planner returned the wrong workflow");
    activated["planner_agent"] = "planner_agent@1.0.0";

    ark::CurlTransport http;
    const ark::DockerSandbox sandbox(std::ref(http), docker);
    ark::ResearchConfig settings;
    settings.model = "fixture-research";
    settings.arxiv_interval = std::chrono::seconds(0);
    ark::ResearchAgent research(provider, settings);
    const ark::CodingAgent coding(provider, model_url, "fixture-code",
                                  [&](const auto& workspace) { return sandbox.run_tests(workspace); });
    const ark::EvaluationAgent evaluate(provider, model_url, "fixture-evaluation");
    const auto flow = ark::supervise_plan(plan, [&](const ark::Task& item, const Json& dependencies, int, const Json&) {
        auto input = message(item.agent, item.intent, {{"instruction", item.instruction}});
        if (!dependencies.empty()) input["memory_context"].push_back({{"tier", "dependencies"}, {"data", dependencies}});
        if (item.id == "research") return research.run(std::move(input));
        if (item.id == "evaluate") return evaluate.run(std::move(input));
        input["payload"]["test_files"] = {{"test_lookup.py", tests}};
        return coding.run(std::move(input));
    }, [](std::string_view) { return true; });
    check(flow.escalations.empty(), flow.results.dump());
    const auto literature = complete(flow.results.at("research"), "research_agent");
    const auto code = complete(flow.results.at("code"), "coding_agent");
    const auto review = complete(flow.results.at("evaluate"), "evaluation_agent");
    check(research_persisted && code.at("passed") == true && review.at("decision") == "ACCEPT",
          "Expected stored evidence, passing Docker tests, and acceptance");

    ark::Redis redis(redis_port, 7);
    ark::WorkingMemory memory(redis);
    const auto work_id = ark::random_uuid();
    memory.set(work_id, "review", review);
    check(memory.get(work_id, "review") == review, "Redis working memory lost the review");
    memory.clear(work_id);
    activated["memory_agent"] = "native working and semantic memory";

    const ark::WebSearchAgent search(provider, model_url, ark::web_search_profile("fixture-search"),
                                     "fixture-token", Json::object());
    const auto searched = complete(search.run(message("web_search_agent", "BROWSE", {{"query", "SQL parameter binding"}})),
                                   "web_search_agent");
    check(searched.at("citations").size() == 1, "Search evidence was not cited");

    const ark::BrowserAgent browser(provider, model_url, ark::browser_profile("fixture-browser"),
                                    {.executable = chromium});
    const auto browsed = complete(browser.run(message("browser_agent", "BROWSE",
        {{"instruction", "Browse " + site.url + " and extract the heading."}})), "browser_agent");
    check(browsed.at("extractions").at(0).at("items").at(0) == "Bind SQL parameters", "Browser evidence was not extracted");

    const ark::GitHubAgent github(provider);
    const auto issues = complete(github.run(message("github_agent", "GITHUB",
        {{"action", "get_open_issues"}, {"repository", "example/project"}})), "github_agent");
    check(issues.at("issues").size() == 1, "GitHub issue fixture was lost");

    const ark::TerminalAgent terminal(sandbox);
    const auto cli = complete(terminal.run(message("terminal_agent", "EXECUTE",
        {{"command", "printf '%s\\n' 'isolated tool verified'"}})), "terminal_agent");
    check(cli.at("stdout") == "isolated tool verified\n", "Terminal did not return live Docker output");

    const char* image = std::getenv("ARK_SECURITY_IMAGE");
    const ark::SecurityAgent security(ark::DockerSandbox(std::ref(http), docker, image ? image : "ark-security:local"));
    const auto report_path = artifacts / "security_audit.html";
    const auto audit = complete(security.run(message("security_agent", "SECURITY",
        {{"files", code.at("files")}, {"report_path", report_path.string()}})), "security_agent");
    check(audit.at("findings").empty() && fs::file_size(report_path) > 0, "Patched example failed static review");

    const ark::DocumentAgent document;
    const auto docx = artifacts / "security_report.docx";
    complete(document.run(message("document_agent", "DOCUMENT", {{"action", "write_docx"}, {"path", docx.string()},
        {"title", "Audit verification"}, {"paragraphs", {summary, "Locked tests and static review passed."}}})), "document_agent");
    const auto verified = complete(document.run(message("document_agent", "DOCUMENT",
        {{"action", "read_docx"}, {"path", docx.string()}})), "document_agent");
    check(verified.at("paragraphs").at(0) == "Audit verification", "Generated report was unreadable");

    ark::EpisodicMemory episodes(artifacts / "episodes.db");
    ark::ProceduralMemory skills(artifacts / "skills.db", ark::Embeddings(provider, model_url, "fixture-embedding"));
    const ark::ReflectionAgent reflect(provider, model_url, ark::reflection_profile("fixture-reflection"));
    const auto reflection = reflect.run({{"task_id", work_id}, {"intent", "RESEARCH"}, {"status", "DONE"}, {"quality_score", 1.0},
        {"agent_versions", {{"coding_agent", "1.0.0"}}}, {"attempts", Json::array()}}, episodes, skills);
    check(reflection.at("status") == "DONE" && episodes.by_agent("reflection_agent").size() == 1, reflection.dump());
    activated["reflection_agent"] = reflection.at("agent_version").get<std::string>();

    const ark::FileSystemAgent files({{"reports", artifacts.string()}});
    const auto write = [&](std::string_view path, std::string_view content) {
        complete(files.run(message("file_system_agent", "FILES",
            {{"root", "reports"}, {"path", path}, {"operation", "write"}, {"content", content}}), episodes), "file_system_agent");
    };
    write("research_summary.md", "# Integration fixture research summary\n\n" + literature.at("summary").get<std::string>());
    write("patched_example.py", code.at("files").at("patched_example.py").get<std::string>());
    write("reflection_log.json", reflection.dump(2));
    check(activated.size() == 14, "Expected evidence from fourteen native components");
    write("integration_manifest.json", Json{{"status", "DONE"}, {"components", activated},
             {"fixture_services", {"model", "arXiv", "Tavily", "Qdrant", "GitHub"}},
             {"live_services", {"Chromium", "Docker (unittest, terminal, Semgrep and Bandit)", "Redis", "SQLite"}}}.dump(2));
    std::cout << "Verified 14 native components with artifacts in " << artifacts << '\n';
}

} // namespace

int main() {
    const auto* chromium = std::getenv("ARK_TEST_CHROMIUM");
    const auto* docker = std::getenv("ARK_TEST_DOCKER_SOCKET");
    const auto* redis = std::getenv("ARK_TEST_REDIS_PORT");
    if (!chromium || !docker || !redis) return 77;
    unsetenv("ARK_LLAMA_URL");
    const auto* persistent = std::getenv("ARK_TEST_PHASE2_ARTIFACTS");
    const auto directory = fs::absolute(persistent ? fs::path(persistent) : fs::temp_directory_path() / ("ark-phase2-" + ark::random_uuid()));
    struct Cleanup {
        fs::path path;
        bool remove = false;
        ~Cleanup() { if (remove) { std::error_code ignored; fs::remove_all(path, ignored); } }
    } cleanup{directory};
    try {
        if (!fs::create_directories(directory)) throw std::runtime_error("Integration artifact directory must be new");
        cleanup.remove = !persistent;
        run(directory, chromium, docker, std::stoi(redis));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
