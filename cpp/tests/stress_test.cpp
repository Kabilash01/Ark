#include "ark/conflict_resolver.hpp"
#include "ark/storage.hpp"
#include "ark/runtime_files.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace {

void check(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

void update_peak(std::atomic<int>& peak, int current) {
    auto old = peak.load(std::memory_order_relaxed);
    while (old < current && !peak.compare_exchange_weak(old, current, std::memory_order_relaxed)) {}
}

struct RunResult {
    std::vector<double> scores;
    ark::Json report;
};

RunResult run_wave(ark::EpisodicMemory& memory, bool concurrent) {
    constexpr int task_count = 20;
    std::array<double, task_count> scores{};
    constexpr std::array<ark::Tool, 3> tools{ark::Tool::browser, ark::Tool::terminal, ark::Tool::arxiv};
    constexpr std::array<int, 3> capacities{1, 2, 3};
    std::array<std::atomic<int>, 3> active{};
    std::array<std::atomic<int>, 3> peaks{};
    std::barrier start(task_count);
    std::vector<std::future<void>> tasks;
    auto task = [&](int i) {
        if (concurrent) start.arrive_and_wait();
        const auto slot = static_cast<std::size_t>(i % tools.size());
        {
            auto permit = ark::tool_semaphore(tools[slot]).acquire();
            const auto now = active[slot].fetch_add(1, std::memory_order_relaxed) + 1;
            update_peak(peaks[slot], now);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            active[slot].fetch_sub(1, std::memory_order_relaxed);
        }
        const auto score = static_cast<double>((i * 7 + 3) % 11) / 10.0;
        scores[static_cast<std::size_t>(i)] = score;
        memory.log({{"task_id", std::string(concurrent ? "parallel-" : "serial-") + std::to_string(i)},
            {"agent_name", "stress_worker"}, {"agent_version", "stress_worker@1.0.0"},
            {"intent", "EXECUTE"}, {"summary", "deterministic stress fixture"}, {"quality_score", score}});
    };
    if (concurrent) {
        for (int i = 0; i < task_count; ++i) tasks.push_back(std::async(std::launch::async, task, i));
        for (auto& future : tasks) future.get();
    } else {
        for (int i = 0; i < task_count; ++i) task(i);
    }
    auto output = ark::Json::array();
    for (const auto score : scores) output.push_back(score);
    auto report = ark::Json::object();
    report["observed_peak"] = ark::Json::object();
    for (std::size_t i = 0; i < tools.size(); ++i) {
        const auto name = i == 0 ? "browser" : i == 1 ? "terminal" : "arxiv";
        report["observed_peak"][name] = peaks[i].load();
        check(peaks[i].load() <= capacities[i], std::string(name) + " semaphore exceeded capacity");
    }
    report["scores"] = output;
    return {std::vector<double>(scores.begin(), scores.end()), report};
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto path = std::filesystem::temp_directory_path() / (ark::random_uuid() + ".db");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
        } cleanup{path};
        ark::EpisodicMemory memory(path);
        const auto serial = run_wave(memory, false);
        const auto parallel = run_wave(memory, true);
        check(serial.scores == parallel.scores, "parallel task scores differ from serial scores");
        const auto rows = memory.recent(100);
        check(rows.size() == 40, "concurrent episode writes were lost");
        std::set<std::string> stored;
        for (const auto& row : rows) stored.insert(row.at("task_id").get<std::string>());
        for (int i = 0; i < 20; ++i) {
            const auto serial_id = "serial-" + std::to_string(i);
            const auto parallel_id = "parallel-" + std::to_string(i);
            check(stored.contains(serial_id) && stored.contains(parallel_id), "a serial or concurrent memory record is missing");
        }
        auto report = parallel.report;
        report["stress_tasks"] = 20;
        report["serial_reference_tasks"] = 20;
        report["memory_rows_written"] = rows.size();
        report["scores_match_serial"] = true;
        if (argc == 2) ark::write_json(argv[1], report);
        std::cout << report.dump(2) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
