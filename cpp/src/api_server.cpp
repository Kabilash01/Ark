#include "ark/api_server.hpp"
#include "ark/conflict_resolver.hpp"
#include "ark/research.hpp"
#include "ark/runtime_files.hpp"

#include <httplib.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace ark {
namespace {

using HttpRequest = httplib::Request;
using HttpResponse = httplib::Response;
using Socket = httplib::ws::WebSocket;

class HttpFailure : public std::runtime_error {
public:
    HttpFailure(int status, std::string message) : std::runtime_error(std::move(message)), status(status) {}
    const int status;
};

void reply(HttpResponse& out, const Json& body, int status = 200) {
    out.status = status;
    out.set_content(body.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json");
}

template <typename Handler>
auto route(Handler handler) {
    return [handler = std::move(handler)](const HttpRequest& request, HttpResponse& response) {
        try {
            handler(request, response);
        } catch (const HttpFailure& error) {
            reply(response, {{"detail", error.what()}}, error.status);
        } catch (const Json::exception& error) {
            reply(response, {{"detail", error.what()}}, 422);
        } catch (const std::exception& error) {
            reply(response, {{"detail", error.what()}}, 500);
        }
    };
}

Json body(const HttpRequest& request) {
    if (!request.get_header_value("Content-Type").starts_with("application/json")) {
        throw HttpFailure(415, "Expected application/json");
    }
    const auto input = Json::parse(request.body);
    if (!input.is_object()) {
        throw HttpFailure(422, "Expected a JSON object");
    }
    return input;
}

std::string param(const HttpRequest& request, const char* key) {
    return request.has_param(key) ? request.get_param_value(key) : "";
}

int count_param(const HttpRequest& request) {
    const auto text = param(request, "limit");
    if (text.empty()) {
        return 50;
    }
    try {
        std::size_t end = 0;
        const auto value = std::stoi(text, &end);
        if (end == text.size() && value >= 1 && value <= 200) {
            return value;
        }
    } catch (const std::exception&) {}
    throw HttpFailure(422, "limit must be an integer between 1 and 200");
}

class Events {
public:
    void listen(Socket& socket) {
        {
            const std::lock_guard lock(mutex_);
            sockets_.push_back(&socket);
        }
        struct Remove {
            Events& events;
            Socket* socket;
            ~Remove() {
                const std::lock_guard lock(events.mutex_);
                std::erase(events.sockets_, socket);
            }
        } remove{*this, &socket};
        std::string message;
        while (socket.read(message)) {}
    }

    void send(std::string_view event, std::string_view task_id, const Json& report = nullptr) {
        Json payload{{"event", event}, {"task_id", task_id}, {"timestamp", utc_timestamp()}};
        if (!report.is_null()) {
            payload["report"] = report;
        }
        const auto message = payload.dump();
        const std::lock_guard lock(mutex_);
        for (auto* socket : sockets_) {
            if (socket->is_open()) {
                socket->send(message);
            }
        }
    }

private:
    std::mutex mutex_;
    std::vector<Socket*> sockets_;
};

class Tasks {
public:
    Tasks(Events& events, std::function<Json(const Json&)> run, std::function<void(const Json&)> log_conflict)
        : events_(events), run_(std::move(run)), log_conflict_(std::move(log_conflict)),
          worker_([this](std::stop_token token) { work(token); }) {}

    Json submit(const Json& request) {
        const auto& goal = request.at("goal").get_ref<const std::string&>();
        if (goal.empty() || goal.size() > 8192 || goal.find_first_not_of(" \r\t\n") == std::string::npos) {
            throw HttpFailure(422, "goal must contain 1 to 8192 bytes");
        }
        const bool airgap = request.value("airgap_mode", false);
        const bool privacy = request.value("privacy", false) || airgap;
        const auto priority = request.value("priority", Json(5));
        if (!priority.is_number_integer() || priority.get<double>() < std::numeric_limits<int>::min()
            || priority.get<double>() > std::numeric_limits<int>::max()) {
            throw HttpFailure(422, "priority must be an integer in range");
        }
        const auto domain = request.value("domain", std::string("general"));
        if (domain.size() > 64 || domain.find_first_not_of(" \r\t\n") == std::string::npos) {
            throw HttpFailure(422, "domain must contain 1 to 64 bytes");
        }
        Json payload{{"goal", goal}, {"privacy", privacy}, {"airgap_mode", airgap}};
        if (request.contains("domain")) {
            payload["domain"] = domain;
        }
        if (request.contains("test_files")) {
            if (!request.at("test_files").is_object()) {
                throw HttpFailure(422, "test_files must map filenames to contents");
            }
            payload["test_files"] = request.at("test_files");
        }
        auto task = make_message("user", "supervisor_agent", "SUPERVISE", std::move(payload));
        {
            const std::lock_guard lock(mutex_);
            if (queue_.size() >= 32) {
                throw HttpFailure(503, "Task queue is full");
            }
            if (order_.size() >= 200) {
                const auto expired = std::find_if(order_.begin(), order_.end(), [&](const auto& old) {
                    const auto& status = tasks_.at(old).at("status");
                    return status != "RUNNING" && status != "PENDING";
                });
                if (expired != order_.end()) {
                    tasks_.erase(*expired);
                    order_.erase(expired);
                }
            }
            auto id = task.at("task_id").get<std::string>();
            while (tasks_.contains(id)) {
                task = make_message("user", "supervisor_agent", "SUPERVISE", task.at("payload"));
                id = task.at("task_id").get<std::string>();
            }
            task["priority"] = priority;
            tasks_.emplace(id, task);
            order_.push_back(id);
            queue_.push_back({id, priority.get<int>(), task.at("timestamp").get<std::string>(), domain});
            // Publish creation before the worker can announce a start.
            events_.send("task_created", id);
        }
        ready_.notify_one();
        return task;
    }

    Json get(const std::string& id) {
        const std::lock_guard lock(mutex_);
        return find(id);
    }

    Json recent(int limit) {
        const std::lock_guard lock(mutex_);
        auto output = Json::array();
        for (auto it = order_.rbegin(); it != order_.rend() && limit-- > 0; ++it) {
            output.push_back(tasks_.at(*it));
        }
        return output;
    }

    Json cancel(const std::string& id) {
        Json canceled;
        {
            const std::lock_guard lock(mutex_);
            auto& task = find(id);
            if (task.at("status") != "PENDING") {
                throw HttpFailure(409, "Only queued tasks can be cancelled");
            }
            task["status"] = "FAILED";
            task["output"] = {{"cancelled", true}, {"reason", "cancelled by user"}};
            std::erase_if(queue_, [&](const auto& queued) { return queued.id == id; });
            canceled = task;
        }
        events_.send("task_cancelled", id);
        return canceled;
    }

private:
    Events& events_;
    std::function<Json(const Json&)> run_;
    std::function<void(const Json&)> log_conflict_;
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::map<std::string, Json> tasks_;
    std::vector<PendingTask> queue_;
    std::deque<std::string> order_;
    ConflictResolver conflicts_;
    std::jthread worker_;

    Json& find(const std::string& id) {
        const auto task = tasks_.find(id);
        if (task == tasks_.end()) {
            throw HttpFailure(404, "task not found");
        }
        return task->second;
    }

    void work(std::stop_token token) {
        while (!token.stop_requested()) {
            Json task;
            std::string id;
            Json conflict;
            {
                std::unique_lock lock(mutex_);
                if (!ready_.wait(lock, token, [this] { return !queue_.empty(); }) || token.stop_requested()) {
                    return;
                }
                const auto selected = conflicts_.resolve_priority(queue_);
                if (conflicts_.detect_priority_conflict(queue_)) {
                    conflict = conflicts_.log_conflict(queue_, selected);
                }
                id = std::move(queue_[selected].id);
                queue_.erase(queue_.begin() + selected);
                auto& stored = find(id);
                stored["status"] = "RUNNING";
                task = stored;
            }
            if (!conflict.is_null()) {
                if (log_conflict_) {
                    try {
                        log_conflict_(conflict);
                    } catch (const std::exception& error) {
                        std::cerr << "Could not record priority conflict: " << error.what() << '\n';
                    }
                }
                events_.send("conflict_detected", id, conflict);
            }
            events_.send("task_started", id);
            Json output;
            try {
                output = run_(task);
                if (!output.is_object() || (output.at("status") != "DONE" && output.at("status") != "FAILED")) {
                    throw std::runtime_error("Native runner returned no terminal status");
                }
            } catch (const std::exception& error) {
                output = {{"status", "FAILED"}, {"error_message", error.what()}};
            }
            const auto escalations = output.value("escalations", Json::array());
            {
                const std::lock_guard lock(mutex_);
                auto& stored = find(id);
                stored["status"] = output.at("status");
                if (output.contains("quality_score") && output.at("quality_score").is_number()) {
                    stored["quality_score"] = output.at("quality_score");
                }
                stored["output"] = std::move(output);
            }
            for (const auto& report : escalations) {
                events_.send("task_escalated", id, report);
            }
            events_.send("task_finished", id);
        }
    }
};

} // namespace

bool serve_api(int port, ApiServices services, std::stop_token stop, const std::function<void(int)>& ready) {
    if (port < 0 || port > 65535 || !services.run || !services.agents || !services.versions || !services.memory) {
        throw std::invalid_argument("API requires a local port and runtime services");
    }
    Events events;
    Tasks tasks(events, services.run, services.log_conflict);
    httplib::Server server;
    server.set_payload_max_length(64 * 1024);
    server.set_write_timeout(5, 0);
    if (port == 0) {
        port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) {
            return false;
        }
    } else if (!server.bind_to_port("127.0.0.1", port)) {
        return false;
    }
    const auto address = "127.0.0.1:" + std::to_string(port);
    const auto localhost = "localhost:" + std::to_string(port);
    server.set_pre_routing_handler([&](const HttpRequest& request, HttpResponse& response) {
        const auto host = request.get_header_value("Host");
        const auto origin = request.get_header_value("Origin");
        if ((host != address && host != localhost)
            || (!origin.empty() && origin != "http://" + address && origin != "http://" + localhost)) {
            reply(response, {{"detail", "This API accepts local requests only"}}, 403);
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server.Post("/task", route([&](const HttpRequest& request, HttpResponse& response) {
        reply(response, tasks.submit(body(request)));
    }));
    server.Get(R"(/task/([a-zA-Z0-9_-]+))", route([&](const HttpRequest& request, HttpResponse& response) {
        reply(response, tasks.get(request.matches[1]));
    }));
    server.Get("/tasks", route([&](const HttpRequest& request, HttpResponse& response) {
        reply(response, tasks.recent(count_param(request)));
    }));
    server.Delete(R"(/task/([a-zA-Z0-9_-]+))", route([&](const HttpRequest& request, HttpResponse& response) {
        reply(response, tasks.cancel(request.matches[1]));
    }));
    server.Get("/agents", route([&](const HttpRequest&, HttpResponse& response) { reply(response, services.agents()); }));
    server.Get(R"(/agents/([a-zA-Z0-9_-]+)/versions)", route([&](const HttpRequest& request, HttpResponse& response) {
        const auto history = services.versions(request.matches[1].str());
        if (history.is_null()) {
            throw HttpFailure(404, "agent not found");
        }
        reply(response, history);
    }));
    server.Get(R"(/memory/([a-z]+))", route([&](const HttpRequest& request, HttpResponse& response) {
        const auto tier = request.matches[1].str();
        if (tier != "working" && tier != "episodic" && tier != "semantic" && tier != "procedural") {
            throw HttpFailure(400, "unknown memory tier");
        }
        reply(response, {{"tier", tier},
                         {"items", services.memory(tier, param(request, "query"), param(request, "task_id"),
                                                   count_param(request))}});
    }));
    server.Post("/memory/search", route([&](const HttpRequest& request, HttpResponse& response) {
        const auto input = body(request);
        const auto query = input.at("query").get<std::string>();
        const auto limit = input.value("top_k", 5);
        if (query.empty() || query.size() > 8192 || limit < 1 || limit > 50) {
            throw HttpFailure(422, "query and top_k are out of range");
        }
        reply(response, {{"query", query}, {"top_k", limit}, {"results", services.memory("semantic", query, "", limit)}});
    }));
    std::mutex config_lock;
    server.Get("/config/privacy", route([&](const HttpRequest&, HttpResponse& response) {
        const std::lock_guard lock(config_lock);
        reply(response, read_json(services.privacy_file));
    }));
    server.Post("/config/privacy", route([&](const HttpRequest& request, HttpResponse& response) {
        const auto input = body(request);
        const std::lock_guard lock(config_lock);
        auto config = read_json(services.privacy_file);
        config["privacy_mode"] = input.at("privacy_mode").get<bool>();
        if (input.contains("airgap_mode") && !input.at("airgap_mode").is_null()) {
            config["airgap_mode"] = input.at("airgap_mode").get<bool>();
        }
        write_json(services.privacy_file, config);
        reply(response, config);
    }));
    server.WebSocket("/ws", [&](const HttpRequest&, Socket& socket) { events.listen(socket); });
    const std::stop_callback cancel(stop, [&server] { server.stop(); });
    if (stop.stop_requested()) {
        return false;
    }
    if (ready) {
        ready(port);
    }
    return server.listen_after_bind();
}

} // namespace ark
