#include "ark/ml_engineer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>

namespace ark {
namespace {

double oriented(double value, bool maximize) { return maximize ? -value : value; }

Json mlflow_request(const Transport& transport, std::string url, std::string_view method, Json body = Json::object()) {
    const auto response = transport({std::move(url), std::string(method), body.dump(), {"Content-Type: application/json"}});
    response.require_success("MLflow tracking");
    if (response.body.empty()) return Json::object();
    const auto parsed = Json::parse(response.body);
    if (!parsed.is_object()) throw std::runtime_error("MLflow returned a malformed response");
    return parsed;
}

} // namespace

Json optimize_hyperparameters(const std::vector<HyperparameterRange>& ranges, std::size_t trials,
                              const TrialObjective& objective, bool maximize, std::uint64_t seed) {
    if (ranges.empty() || ranges.size() > 32 || trials == 0 || trials > 1000 || !objective) {
        throw std::invalid_argument("Optimization requires 1–32 ranges, 1–1000 trials, and an objective");
    }
    std::vector<std::string> names;
    for (const auto& range : ranges) {
        if (range.name.empty() || !std::isfinite(range.minimum) || !std::isfinite(range.maximum)
            || range.minimum >= range.maximum
            || std::find(names.begin(), names.end(), range.name) != names.end()) {
            throw std::invalid_argument("Hyperparameter ranges must have unique names and finite increasing bounds");
        }
        names.push_back(range.name);
    }

    struct Completed { Json parameters; double score; };
    std::vector<Completed> completed;
    std::mt19937_64 random(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    Json observations = Json::array();
    for (std::size_t index = 0; index < trials; ++index) {
        Json parameters = Json::object();
        for (const auto& range : ranges) {
            double sample = unit(random);
            // Startup uses a space-filling random sample. Later trials sample
            // around one of the best 25% observations with broad exploration.
            if (completed.size() >= 5 && unit(random) < 0.75) {
                auto ranked = completed;
                std::sort(ranked.begin(), ranked.end(), [maximize](const auto& a, const auto& b) {
                    return oriented(a.score, maximize) < oriented(b.score, maximize);
                });
                const auto elite_count = std::max<std::size_t>(1, (ranked.size() + 3) / 4);
                std::uniform_int_distribution<std::size_t> choose(0, elite_count - 1);
                const auto center = ranked[choose(random)].parameters.at(range.name).get<double>();
                const auto sigma = (range.maximum - range.minimum) * 0.18;
                std::normal_distribution<double> local(center, sigma);
                sample = (std::clamp(local(random), range.minimum, range.maximum) - range.minimum)
                       / (range.maximum - range.minimum);
            }
            parameters[range.name] = range.minimum + sample * (range.maximum - range.minimum);
        }
        const double score = objective(parameters);
        if (!std::isfinite(score)) throw std::runtime_error("Optimization objective returned a non-finite metric");
        completed.push_back({parameters, score});
        observations.push_back({{"trial", index}, {"parameters", std::move(parameters)}, {"objective", score}});
    }
    const auto best = std::min_element(completed.begin(), completed.end(), [maximize](const auto& a, const auto& b) {
        return oriented(a.score, maximize) < oriented(b.score, maximize);
    });
    return {{"trials", std::move(observations)}, {"best_parameters", best->parameters},
            {"best_objective", best->score}, {"direction", maximize ? "maximize" : "minimize"},
            {"optimizer", "ark_tpe_style_v1"}};
}

MlflowLogger::MlflowLogger(Transport transport, std::string tracking_url)
    : transport_(std::move(transport)), url_(local_base_url(tracking_url)) {
    if (!transport_) throw std::invalid_argument("MLflow logger requires an HTTP transport");
}

Json MlflowLogger::log_study(std::string_view experiment, const Json& study) {
    if (experiment.empty() || !study.is_object() || !study.contains("trials") || !study.at("trials").is_array()) {
        throw std::invalid_argument("MLflow requires a named experiment and a study with trials");
    }
    Json found;
    try {
        found = mlflow_request(transport_, url_ + "/api/2.0/mlflow/experiments/get-by-name?experiment_name="
            + url_encode(experiment), "GET");
    } catch (const std::exception&) {
        found = mlflow_request(transport_, url_ + "/api/2.0/mlflow/experiments/create", "POST", {{"name", experiment}});
    }
    std::string experiment_id;
    if (found.contains("experiment")) experiment_id = found.at("experiment").at("experiment_id").get<std::string>();
    else if (found.contains("experiment_id")) experiment_id = found.at("experiment_id").get<std::string>();
    if (experiment_id.empty()) throw std::runtime_error("MLflow did not return an experiment ID");

    const auto created = mlflow_request(transport_, url_ + "/api/2.0/mlflow/runs/create", "POST",
        {{"experiment_id", experiment_id}, {"run_name", "ark-native-hpo"}});
    const auto run_id = created.at("run").at("info").at("run_id").get<std::string>();
    if (run_id.empty()) throw std::runtime_error("MLflow did not return a run ID");
    Json params = Json::array({{{"key", "optimizer"}, {"value", study.value("optimizer", "ark_tpe_style_v1")}},
        {{"key", "trial_count"}, {"value", std::to_string(study.at("trials").size())}},
        {{"key", "direction"}, {"value", study.value("direction", "minimize")}}});
    Json metrics = Json::array();
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& trial : study.at("trials")) {
        const auto step = trial.at("trial").get<std::int64_t>();
        metrics.push_back({{"key", "objective"}, {"value", trial.at("objective")}, {"timestamp", timestamp}, {"step", step}});
        for (const auto& [key, value] : trial.at("parameters").items()) {
            metrics.push_back({{"key", "trial." + key}, {"value", value}, {"timestamp", timestamp}, {"step", step}});
        }
    }
    constexpr std::size_t batch_limit = 900;
    for (std::size_t offset = 0; offset < metrics.size(); offset += batch_limit) {
        Json batch = Json::array();
        const auto end = std::min(metrics.size(), offset + batch_limit);
        for (auto i = offset; i < end; ++i) batch.push_back(metrics[i]);
        mlflow_request(transport_, url_ + "/api/2.0/mlflow/runs/log-batch", "POST",
            {{"run_id", run_id}, {"metrics", std::move(batch)}, {"params", offset == 0 ? params : Json::array()}});
    }
    mlflow_request(transport_, url_ + "/api/2.0/mlflow/runs/update", "POST",
        {{"run_id", run_id}, {"status", "FINISHED"}});
    return {{"experiment_id", experiment_id}, {"run_id", run_id}, {"logged_trials", study.at("trials").size()}};
}

AgentProfile ml_engineer_profile(std::string model) {
    return {"1.0.0", std::move(model), {{"system", "Run bounded hyperparameter studies and report measured metrics without inventing results."}}};
}

Json run_ml_engineer(Transport transport, std::string tracking_url, Json message) {
    if (!message.is_object()) throw std::invalid_argument("ML Engineer requires an ARKMessage object");
    message["agent_version"] = "ml_engineer_agent@1.0.0";
    try {
        if (!pending_for(message, "ml_engineer_agent") || message.at("intent") != "ML_OPTIMIZE") {
            throw std::invalid_argument("ML Engineer requires a pending ML_OPTIMIZE message");
        }
        const auto& payload = message.at("payload");
        if (!payload.contains("ranges") || !payload.at("ranges").is_array()) throw std::invalid_argument("ML optimization requires ranges");
        std::vector<HyperparameterRange> ranges;
        for (const auto& item : payload.at("ranges")) {
            ranges.push_back({item.at("name").get<std::string>(), item.at("minimum").get<double>(), item.at("maximum").get<double>()});
        }
        const auto trials = payload.value("trial_count", std::size_t{10});
        if (!payload.contains("target") || !payload.at("target").is_object()) {
            throw std::invalid_argument("Native smoke objective requires target values for each parameter");
        }
        const auto target = payload.at("target");
        for (const auto& range : ranges) if (!target.contains(range.name)) throw std::invalid_argument("Missing target for " + range.name);
        auto study = optimize_hyperparameters(ranges, trials, [&target](const Json& values) {
            double loss = 0;
            for (const auto& [name, expected] : target.items()) {
                const auto delta = values.at(name).get<double>() - expected.get<double>();
                loss += delta * delta;
            }
            return loss;
        }, false, payload.value("seed", std::uint64_t{42}));
        if (!tracking_url.empty()) {
            MlflowLogger logger(std::move(transport), std::move(tracking_url));
            study["mlflow"] = logger.log_study(payload.value("experiment", "ark-native"), study);
        }
        message["status"] = "DONE";
        message["quality_score"] = -1.0;
        message["output"] = std::move(study);
    } catch (const TransientHttpError& error) {
        message["status"] = "RETRY";
        message["quality_score"] = -1.0;
        message["output"] = {{"error_type", "MLflowUnavailable"}, {"error_message", error.what()}};
    } catch (const std::exception& error) {
        message["status"] = "FAILED";
        message["quality_score"] = -1.0;
        message["output"] = {{"error_type", "MLExperimentFailed"}, {"error_message", error.what()}};
    }
    return message;
}

} // namespace ark
