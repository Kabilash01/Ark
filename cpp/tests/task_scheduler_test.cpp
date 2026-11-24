#include "ark/task_scheduler.hpp"

#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }
std::string iso_utc(std::chrono::system_clock::time_point value) {
    const auto stamp = std::chrono::system_clock::to_time_t(value);
    std::tm utc{}; gmtime_r(&stamp, &utc); char output[32]{};
    std::strftime(output, sizeof(output), "%Y-%m-%dT%H:%M:%SZ", &utc); return output;
}

void persists_and_fires_interval_once_and_file_schedules() {
    const auto root = std::filesystem::temp_directory_path() / ("ark-scheduler-" + ark::random_uuid());
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);} } cleanup{root};
    const auto db = root / "schedules.db";
    const auto watched = root / "watch.txt";
    { std::ofstream file(watched); file << "initial"; }
    std::mutex mutex; std::vector<std::string> fired; std::vector<bool> privacy_modes;
    ark::TaskScheduler scheduler(db, [&](std::string_view goal, bool privacy) {
        std::lock_guard lock(mutex); fired.emplace_back(goal); privacy_modes.push_back(privacy); return Json{{"status", "DONE"}};
    });
    const auto start = std::chrono::system_clock::now();
    const auto interval = scheduler.schedule_interval(std::chrono::seconds(5), "interval goal", true);
    const auto once_time = start + std::chrono::minutes(2);
    const auto once = scheduler.schedule_once(iso_utc(once_time), "once goal");
    const auto watch = scheduler.schedule_on_file_change(watched, "watch goal");
    const auto cron = scheduler.schedule_cron("0 0 1 1 *", "cron goal");
    check(scheduler.pause(once) && scheduler.resume(once), "pause/resume failed");
    check(scheduler.list().size() == 4, "persistent schedule listing is incomplete");
    check(scheduler.run_due(start + std::chrono::seconds(6)) == 1, "interval did not fire when due");
    { std::ofstream file(watched, std::ios::app); file << " changed"; }
    check(scheduler.run_due(start + std::chrono::seconds(8)) == 1, "file change did not trigger its task");
    check(scheduler.cancel(interval) && scheduler.cancel(watch) && scheduler.cancel(cron), "cancel did not remove schedules");
    check(scheduler.run_due(once_time) == 1, "one-time schedule did not fire");
    check(scheduler.list().size() == 1, "one-time completed schedule should remain inspectable");
    {
        ark::TaskScheduler reopened(db, [](std::string_view, bool) { return Json::object(); });
        check(reopened.list().size() == 1, "SQLite schedules did not survive reopening");
    }
    std::lock_guard lock(mutex);
    check(fired.size() == 3 && fired[0] == "interval goal" && privacy_modes[0]
        && fired[1] == "watch goal" && !privacy_modes[1] && fired[2] == "once goal" && !privacy_modes[2],
        "scheduled callback results or privacy values were incorrect");
}

void rejects_invalid_cron_and_watch_paths() {
    const auto root = std::filesystem::temp_directory_path() / ("ark-scheduler-" + ark::random_uuid());
    ark::TaskScheduler scheduler(root / "schedules.db", [](std::string_view, bool){return Json::object();});
    bool bad_cron=false,bad_watch=false;
    try { (void)scheduler.schedule_cron("nope", "goal"); } catch(const std::invalid_argument&) {bad_cron=true;}
    try { (void)scheduler.schedule_on_file_change("relative", "goal"); } catch(const std::invalid_argument&) {bad_watch=true;}
    check(bad_cron && bad_watch, "invalid cron expression or relative file path was accepted");
}
}

int main(){try{persists_and_fires_interval_once_and_file_schedules();rejects_invalid_cron_and_watch_paths();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
