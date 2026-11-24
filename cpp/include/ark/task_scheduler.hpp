#pragma once

#include "ark/message.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

namespace ark {

class TaskScheduler {
public:
    using Runner = std::function<Json(std::string_view goal, bool privacy)>;

    TaskScheduler(std::filesystem::path database, Runner runner);
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    std::string schedule_cron(std::string_view expression, std::string_view goal, bool privacy = false);
    std::string schedule_interval(std::chrono::seconds interval, std::string_view goal, bool privacy = false);
    std::string schedule_once(std::string_view datetime_utc, std::string_view goal, bool privacy = false);
    std::string schedule_on_file_change(const std::filesystem::path& path, std::string_view goal, bool privacy = false);
    bool cancel(std::string_view id);
    bool pause(std::string_view id);
    bool resume(std::string_view id);
    Json list() const;
    std::size_t run_due(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ark
