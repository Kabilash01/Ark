#pragma once

#include "ark/http.hpp"
#include "ark/versions.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace ark {

struct HyperparameterRange {
    std::string name;
    double minimum;
    double maximum;
};

using TrialObjective = std::function<double(const Json& parameters)>;

// Native, bounded TPE-style search. The objective remains supplied by the
// caller so training code can stay in the application that owns the dataset.
Json optimize_hyperparameters(const std::vector<HyperparameterRange>& ranges,
                              std::size_t trials, const TrialObjective& objective,
                              bool maximize = false, std::uint64_t seed = 42);

class MlflowLogger {
public:
    explicit MlflowLogger(Transport transport, std::string tracking_url);
    Json log_study(std::string_view experiment, const Json& study);

private:
    Transport transport_;
    std::string url_;
};

AgentProfile ml_engineer_profile(std::string model);
Json run_ml_engineer(Transport transport, std::string tracking_url, Json message);

} // namespace ark
