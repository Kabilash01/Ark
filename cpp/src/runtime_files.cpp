#include "ark/runtime_files.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace ark {
namespace {

void replace_text(const std::filesystem::path& path, std::string_view text) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    auto temporary = path;
    temporary += "." + random_uuid() + ".tmp";
    try {
        std::ofstream file(temporary, std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << text;
        file.close();
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

double rounded(double value) { return std::round(value * 10.0) / 10.0; }

} // namespace

Json pick_models(const Json& library, double vram_gib, double ram_gib) {
    if (!std::isfinite(vram_gib) || vram_gib < 0.0 || !std::isfinite(ram_gib) || ram_gib < 0.0) {
        throw std::invalid_argument("Detected memory must be a nonnegative finite number");
    }
    const char* tier = vram_gib == 0 ? "T5" : vram_gib < 6 ? "T1" : vram_gib < 14 ? "T2" : vram_gib < 22 ? "T3" : "T4";
    return {{"tier", tier}, {"vram_gb", vram_gib}, {"ram_gb", ram_gib},
            {"models", library.at("tiers").at(tier).at("models")}};
}

double detect_ram_gib() {
#if defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t length = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) == 0) {
        return rounded(static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    }
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        return rounded(static_cast<double>(pages) * page_size / (1024.0 * 1024 * 1024));
    }
#endif
    return 0;
}

double detect_vram_gib() {
#if defined(__unix__) || defined(__APPLE__)
    // This fixed command queries the driver. No input is interpolated into it.
    auto* probe = popen("nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null", "r");
    if (probe) {
        std::array<char, 64> buffer{};
        const bool read = std::fgets(buffer.data(), buffer.size(), probe) != nullptr;
        // Drain all other GPU rows so the subprocess can exit on multi-GPU machines.
        std::array<char, 128> unused{};
        while (std::fgets(unused.data(), unused.size(), probe)) {}
        if (const auto status = pclose(probe); read && status == 0) {
            const auto mib = std::strtod(buffer.data(), nullptr);
            if (std::isfinite(mib) && mib > 0) {
                return rounded(mib / 1024.0);
            }
        }
    }
#endif
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return detect_ram_gib();
#else
    return 0;
#endif
}

void write_json(const std::filesystem::path& path, const Json& data) { replace_text(path, data.dump(2) + "\n"); }

void write_state(const std::filesystem::path& path, std::string_view goal, std::string_view task_id,
                 std::string_view status, bool privacy, int completed, const Json& last_result) {
    const auto score = last_result.value("score", Json("n/a"));
    std::ostringstream text;
    text << "# ARK OS STATE\n"
         << "Last updated: " << utc_timestamp() << "\n-------------------------------------\n"
         << "Current goal:    " << goal << '\n'
         << "Task ID:         " << task_id << '\n'
         << "Status:          " << status << '\n'
         << "Iteration:       " << completed << '\n'
         << "Privacy mode:    " << (privacy ? "True" : "False") << '\n'
         << "-------------------------------------\n"
         << "Last task:       " << last_result.value("agent", "unknown") << " -> "
         << last_result.value("task", "unknown") << '\n'
         << "Last score:      " << (score.is_string() ? score.get<std::string>() : score.dump()) << " ("
         << last_result.value("version", "n/a") << ")\n";
    replace_text(path, text.str());
}

} // namespace ark
