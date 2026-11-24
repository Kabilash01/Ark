#pragma once

#include "ark/message.hpp"

#include <filesystem>

namespace ark {

// Build an auditable capability map from registry records, reflection episodes,
// and per-agent ARKBench histories. Missing evidence never implies support.
Json build_capability_map(const Json& registry, const Json& reflections,
                          const Json& benchmark_histories);
Json answer_capability_question(const Json& capability_map, std::string_view question);
void write_capability_map(const std::filesystem::path& path, const Json& capability_map);

} // namespace ark
