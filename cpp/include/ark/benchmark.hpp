#pragma once

#include "ark/coding.hpp"

namespace ark {

using BenchmarkDispatch = std::function<Json(const Json&)>;
using BenchmarkProgress = std::function<void(const Json&)>;
using BenchmarkTool = std::function<Json(const Json&)>;

Json run_benchmark(const Json& suite, const BenchmarkDispatch& dispatch, const TestRunner& run_tests,
                   const BenchmarkProgress& progress = {}, const BenchmarkTool& run_tool = {});
Json benchmark_history(const std::filesystem::path& directory, std::string_view agent);

} // namespace ark
