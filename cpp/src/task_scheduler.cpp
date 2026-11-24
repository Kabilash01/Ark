#include "ark/task_scheduler.hpp"

#include <sqlite3.h>

#include <array>
#include <charconv>
#include <condition_variable>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <set>
#include <stdexcept>

namespace ark {
namespace {

using Clock = std::chrono::system_clock;
using Time = Clock::time_point;
std::int64_t seconds(Time value) { return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count(); }
Time from_seconds(std::int64_t value) { return Time(std::chrono::seconds(value)); }

void sql_ok(int status, sqlite3* db) { if (status != SQLITE_OK && status != SQLITE_DONE && status != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db)); }
struct Statement {
    sqlite3_stmt* value{};
    sqlite3* db{};
    Statement(sqlite3* connection, const char* query) : db(connection) { sql_ok(sqlite3_prepare_v2(db, query, -1, &value, nullptr), db); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement() { if (value) sqlite3_finalize(value); }
};
void bind_text_param(Statement& s, int index, std::string_view value) { sql_ok(sqlite3_bind_text(s.value, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT), s.db); }
std::string column_text(sqlite3_stmt* s, int index) { const auto* value = sqlite3_column_text(s, index); return value ? reinterpret_cast<const char*>(value) : ""; }

std::set<int> cron_field(std::string_view text, int minimum, int maximum) {
    std::set<int> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        auto part = text.substr(start, comma == std::string_view::npos ? text.size() - start : comma - start);
        int step = 1;
        if (const auto slash = part.find('/'); slash != std::string_view::npos) {
            const auto step_text = part.substr(slash + 1); part = part.substr(0, slash);
            auto [end, error] = std::from_chars(step_text.data(), step_text.data() + step_text.size(), step);
            if (error != std::errc{} || end != step_text.data() + step_text.size() || step < 1 || step > maximum - minimum + 1) throw std::invalid_argument("Invalid cron step");
        }
        int low = minimum, high = maximum;
        if (part != "*") {
            const auto dash = part.find('-');
            const auto first = part.substr(0, dash);
            auto [end1, error1] = std::from_chars(first.data(), first.data() + first.size(), low);
            if (error1 != std::errc{} || end1 != first.data() + first.size()) throw std::invalid_argument("Invalid cron field");
            high = low;
            if (dash != std::string_view::npos) {
                const auto last = part.substr(dash + 1);
                auto [end2, error2] = std::from_chars(last.data(), last.data() + last.size(), high);
                if (error2 != std::errc{} || end2 != last.data() + last.size()) throw std::invalid_argument("Invalid cron range");
            }
        }
        if (low < minimum || high > maximum || low > high) throw std::invalid_argument("Cron field is outside its valid range");
        for (int value = low; value <= high; value += step) result.insert(value);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return result;
}

std::int64_t next_cron(std::string_view expression, std::int64_t after) {
    std::array<std::string_view, 5> fields{};
    std::size_t count = 0, pos = 0;
    while (pos < expression.size()) {
        while (pos < expression.size() && expression[pos] == ' ') ++pos;
        if (pos == expression.size()) break;
        const auto end = expression.find(' ', pos);
        if (count == fields.size()) throw std::invalid_argument("Cron expression must contain exactly five fields");
        fields[count++] = expression.substr(pos, end == std::string_view::npos ? expression.size() - pos : end - pos);
        pos = end == std::string_view::npos ? expression.size() : end + 1;
    }
    if (count != 5) throw std::invalid_argument("Cron expression must contain exactly five fields");
    const auto minutes = cron_field(fields[0], 0, 59), hours = cron_field(fields[1], 0, 23);
    const auto days = cron_field(fields[2], 1, 31), months = cron_field(fields[3], 1, 12), weekdays = cron_field(fields[4], 0, 7);
    auto candidate = ((after / 60) + 1) * 60;
    const auto limit = candidate + 5LL * 366 * 24 * 60 * 60;
    for (; candidate <= limit; candidate += 60) {
        const auto stamp = static_cast<std::time_t>(candidate);
        std::tm utc{};
        gmtime_r(&stamp, &utc);
        const int dow = utc.tm_wday;
        const bool day_match = days.contains(utc.tm_mday);
        const bool weekday_match = weekdays.contains(dow) || (dow == 0 && weekdays.contains(7));
        const bool dom_wild = fields[2] == "*", dow_wild = fields[4] == "*";
        const bool calendar_match = dom_wild && dow_wild ? true : dom_wild ? weekday_match : dow_wild ? day_match : (day_match || weekday_match);
        if (minutes.contains(utc.tm_min) && hours.contains(utc.tm_hour) && months.contains(utc.tm_mon + 1) && calendar_match) return candidate;
    }
    throw std::invalid_argument("Cron expression has no occurrence in the next five years");
}

std::int64_t parse_utc(std::string_view value) {
    if (value.size() != 20 || value.back() != 'Z') throw std::invalid_argument("One-time schedule must use ISO UTC format YYYY-MM-DDTHH:MM:SSZ");
    std::tm tm{};
    std::istringstream input{std::string(value)};
    input >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) throw std::invalid_argument("Invalid ISO UTC schedule time");
    const auto timestamp = timegm(&tm);
    if (timestamp < 0) throw std::invalid_argument("One-time schedule is before the Unix epoch");
    return static_cast<std::int64_t>(timestamp);
}

std::int64_t modification(const std::filesystem::path& path) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::filesystem::last_write_time(path).time_since_epoch()).count();
}

} // namespace

struct TaskScheduler::Impl {
    sqlite3* db{};
    Runner runner;
    mutable std::mutex mutex;
    std::jthread worker;
    std::condition_variable_any wake;
    explicit Impl(const std::filesystem::path& path, Runner callback) : runner(std::move(callback)) {
        if (runner == nullptr) throw std::invalid_argument("Task Scheduler requires a runner callback");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) throw std::runtime_error("Could not open scheduler database");
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS schedules (id TEXT PRIMARY KEY, kind TEXT NOT NULL, expression TEXT NOT NULL, goal TEXT NOT NULL, privacy INTEGER NOT NULL, next_run INTEGER NOT NULL, interval_seconds INTEGER NOT NULL DEFAULT 0, file_path TEXT NOT NULL DEFAULT '', file_mtime INTEGER NOT NULL DEFAULT 0, paused INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0, last_run INTEGER, last_error TEXT NOT NULL DEFAULT '');", nullptr, nullptr, nullptr);
    }
    ~Impl() { if (db) sqlite3_close(db); }
};

TaskScheduler::TaskScheduler(std::filesystem::path database, Runner runner) : impl_(std::make_unique<Impl>(database, std::move(runner))) {}
TaskScheduler::~TaskScheduler() { stop(); }

std::string TaskScheduler::schedule_cron(std::string_view expression, std::string_view goal, bool privacy) {
    if (goal.empty() || goal.size() > 4000) throw std::invalid_argument("Scheduled goal must be 1–4000 bytes");
    const auto next = next_cron(expression, seconds(Clock::now()));
    const auto id = "ark_sched_" + random_uuid();
    std::lock_guard lock(impl_->mutex); Statement s(impl_->db, "INSERT INTO schedules(id,kind,expression,goal,privacy,next_run) VALUES(?,?,?,?,?,?)");
    bind_text_param(s,1,id); bind_text_param(s,2,"cron"); bind_text_param(s,3,expression); bind_text_param(s,4,goal); sqlite3_bind_int(s.value,5,privacy); sqlite3_bind_int64(s.value,6,next); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return id;
}

std::string TaskScheduler::schedule_interval(std::chrono::seconds interval, std::string_view goal, bool privacy) {
    if (interval < std::chrono::seconds(1) || interval > std::chrono::hours(24 * 365) || goal.empty() || goal.size() > 4000) throw std::invalid_argument("Interval must be 1 second–365 days and goal must be non-empty");
    const auto id = "ark_sched_" + random_uuid(); const auto next = seconds(Clock::now() + interval);
    std::lock_guard lock(impl_->mutex); Statement s(impl_->db, "INSERT INTO schedules(id,kind,expression,goal,privacy,next_run,interval_seconds) VALUES(?,?,?,?,?,?,?)");
    bind_text_param(s,1,id); bind_text_param(s,2,"interval"); bind_text_param(s,3,std::to_string(interval.count())); bind_text_param(s,4,goal); sqlite3_bind_int(s.value,5,privacy); sqlite3_bind_int64(s.value,6,next); sqlite3_bind_int64(s.value,7,interval.count()); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return id;
}

std::string TaskScheduler::schedule_once(std::string_view datetime_utc, std::string_view goal, bool privacy) {
    const auto next = parse_utc(datetime_utc); if (next <= seconds(Clock::now())) throw std::invalid_argument("One-time schedule must be in the future");
    if (goal.empty() || goal.size() > 4000) throw std::invalid_argument("Scheduled goal must be 1–4000 bytes");
    const auto id = "ark_sched_" + random_uuid(); std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"INSERT INTO schedules(id,kind,expression,goal,privacy,next_run) VALUES(?,?,?,?,?,?)");
    bind_text_param(s,1,id); bind_text_param(s,2,"once"); bind_text_param(s,3,datetime_utc); bind_text_param(s,4,goal); sqlite3_bind_int(s.value,5,privacy); sqlite3_bind_int64(s.value,6,next); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return id;
}

std::string TaskScheduler::schedule_on_file_change(const std::filesystem::path& path, std::string_view goal, bool privacy) {
    if (!path.is_absolute() || !std::filesystem::exists(path) || goal.empty() || goal.size() > 4000) throw std::invalid_argument("File watch requires an existing absolute path and a non-empty goal");
    const auto canonical = std::filesystem::weakly_canonical(path).string(); const auto id = "ark_sched_" + random_uuid();
    std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"INSERT INTO schedules(id,kind,expression,goal,privacy,next_run,file_path,file_mtime) VALUES(?,?,?,?,?,?,?,?)");
    bind_text_param(s,1,id); bind_text_param(s,2,"file"); bind_text_param(s,3,canonical); bind_text_param(s,4,goal); sqlite3_bind_int(s.value,5,privacy); sqlite3_bind_int64(s.value,6,seconds(Clock::now())+1); bind_text_param(s,7,canonical); sqlite3_bind_int64(s.value,8,modification(canonical)); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return id;
}

bool TaskScheduler::cancel(std::string_view id) { std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"DELETE FROM schedules WHERE id=?"); bind_text_param(s,1,id); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return sqlite3_changes(impl_->db)>0; }
bool TaskScheduler::pause(std::string_view id) { std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"UPDATE schedules SET paused=1 WHERE id=? AND completed=0"); bind_text_param(s,1,id); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return sqlite3_changes(impl_->db)>0; }
bool TaskScheduler::resume(std::string_view id) { std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"UPDATE schedules SET paused=0 WHERE id=? AND completed=0"); bind_text_param(s,1,id); sql_ok(sqlite3_step(s.value),impl_->db); impl_->wake.notify_all(); return sqlite3_changes(impl_->db)>0; }

Json TaskScheduler::list() const {
    std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"SELECT id,kind,expression,goal,privacy,next_run,paused,completed,last_run,last_error,file_path FROM schedules ORDER BY id");
    Json rows=Json::array(); for(int status;(status=sqlite3_step(s.value))!=SQLITE_DONE;) { sql_ok(status,impl_->db); rows.push_back({{"id",column_text(s.value,0)},{"kind",column_text(s.value,1)},{"expression",column_text(s.value,2)},{"goal",column_text(s.value,3)},{"privacy",sqlite3_column_int(s.value,4)!=0},{"next_run_utc_epoch",sqlite3_column_int64(s.value,5)},{"paused",sqlite3_column_int(s.value,6)!=0},{"completed",sqlite3_column_int(s.value,7)!=0},{"last_run_utc_epoch",sqlite3_column_type(s.value,8)==SQLITE_NULL?Json(nullptr):Json(sqlite3_column_int64(s.value,8))},{"last_error",column_text(s.value,9)},{"file_path",column_text(s.value,10)}}); } return rows;
}

std::size_t TaskScheduler::run_due(Time now) {
    struct Due {std::string id,kind,expression,goal,path; bool privacy; std::int64_t interval,mtime,next;}; std::vector<Due> due; const auto now_s=seconds(now);
    {
        std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"SELECT id,kind,expression,goal,privacy,interval_seconds,file_path,file_mtime,next_run FROM schedules WHERE paused=0 AND completed=0 AND next_run<=?"); sqlite3_bind_int64(s.value,1,now_s);
        for(int status;(status=sqlite3_step(s.value))!=SQLITE_DONE;) {sql_ok(status,impl_->db); due.push_back({column_text(s.value,0),column_text(s.value,1),column_text(s.value,2),column_text(s.value,3),column_text(s.value,6),sqlite3_column_int(s.value,4)!=0,sqlite3_column_int64(s.value,5),sqlite3_column_int64(s.value,7),sqlite3_column_int64(s.value,8)});}
    }
    std::size_t fired=0;
    for(const auto& item:due) {
        bool trigger=item.kind!="file"; std::int64_t new_mtime=item.mtime;
        if(item.kind=="file") { try {new_mtime=modification(item.path); trigger=new_mtime!=item.mtime;} catch(...) { trigger=false; } }
        if(!trigger) { std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"UPDATE schedules SET next_run=? WHERE id=?"); sqlite3_bind_int64(s.value,1,now_s+1); bind_text_param(s,2,item.id); sql_ok(sqlite3_step(s.value),impl_->db); continue; }
        std::int64_t next=0; bool completed=false;
        if(item.kind=="once") completed=true;
        else if(item.kind=="interval") next=now_s+item.interval;
        else if(item.kind=="cron") next=next_cron(item.expression,now_s);
        else next=now_s+1;
        { std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"UPDATE schedules SET next_run=?,completed=?,file_mtime=?,last_run=?,last_error='' WHERE id=?"); sqlite3_bind_int64(s.value,1,next); sqlite3_bind_int(s.value,2,completed); sqlite3_bind_int64(s.value,3,new_mtime); sqlite3_bind_int64(s.value,4,now_s); bind_text_param(s,5,item.id); sql_ok(sqlite3_step(s.value),impl_->db); }
        try { (void)impl_->runner(item.goal,item.privacy); ++fired; }
        catch(const std::exception& error) { std::lock_guard lock(impl_->mutex); Statement s(impl_->db,"UPDATE schedules SET last_error=? WHERE id=?"); bind_text_param(s,1,std::string(error.what()).substr(0,500)); bind_text_param(s,2,item.id); sql_ok(sqlite3_step(s.value),impl_->db); }
    }
    return fired;
}

void TaskScheduler::start() {
    if (impl_->worker.joinable()) return;
    impl_->worker=std::jthread([this](std::stop_token stop){ while(!stop.stop_requested()) { try{run_due();}catch(...){} std::unique_lock lock(impl_->mutex); impl_->wake.wait_for(lock,stop,std::chrono::seconds(1),[]{return false;}); } });
}
void TaskScheduler::stop() { if(impl_->worker.joinable()){impl_->worker.request_stop();impl_->wake.notify_all();impl_->worker.join();} }

} // namespace ark
