#include "ark/ml_engineer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

namespace ark {
namespace {

double oriented(double value, bool maximize) { return maximize ? -value : value; }

struct Dataset {
    std::vector<std::vector<double>> x;
    std::vector<double> y;
    std::vector<std::size_t> train;
    std::vector<std::size_t> validation;
};

Dataset read_dataset(const Json& payload) {
    if (!payload.contains("dataset") || !payload.at("dataset").is_object()) throw std::invalid_argument("linear_regression requires an inline dataset object");
    const auto& data = payload.at("dataset");
    if (!data.contains("features") || !data.at("features").is_array() || !data.contains("targets") || !data.at("targets").is_array())
        throw std::invalid_argument("dataset requires numeric features and targets arrays");
    const auto& features = data.at("features");
    const auto& targets = data.at("targets");
    if (features.size() < 10 || features.size() > 5000 || targets.size() != features.size()) throw std::invalid_argument("dataset requires 10–5000 rows with one target per row");
    Dataset result;
    std::size_t dimensions = 0;
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!features[i].is_array() || features[i].empty() || features[i].size() > 32 || !targets[i].is_number()) throw std::invalid_argument("invalid numeric feature row or target");
        if (dimensions == 0) dimensions = features[i].size();
        if (features[i].size() != dimensions) throw std::invalid_argument("feature rows must have matching dimensions");
        std::vector<double> row;
        for (const auto& value : features[i]) {
            if (!value.is_number() || !std::isfinite(value.get<double>())) throw std::invalid_argument("features must be finite numbers");
            row.push_back(value.get<double>());
        }
        const auto target = targets[i].get<double>();
        if (!std::isfinite(target)) throw std::invalid_argument("targets must be finite numbers");
        result.x.push_back(std::move(row)); result.y.push_back(target);
    }
    std::vector<std::size_t> order(result.x.size());
    std::iota(order.begin(), order.end(), 0);
    std::mt19937_64 random(payload.value("seed", std::uint64_t{42}));
    std::shuffle(order.begin(), order.end(), random);
    const auto split = order.size() * 8 / 10;
    result.train.assign(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(split));
    result.validation.assign(order.begin() + static_cast<std::ptrdiff_t>(split), order.end());
    return result;
}

struct Fit { std::vector<double> weights; double rmse; };

Fit fit_linear(const Dataset& data, const std::vector<std::size_t>& train,
               const std::vector<std::size_t>& evaluate, double rate, double l2, std::size_t epochs) {
    std::vector<double> weights(data.x.front().size() + 1, 0.0);
    for (std::size_t epoch = 0; epoch < epochs; ++epoch) {
        std::vector<double> gradient(weights.size(), 0.0);
        for (auto row : train) {
            double prediction = weights[0];
            for (std::size_t j = 0; j + 1 < weights.size(); ++j) prediction += weights[j + 1] * data.x[row][j];
            const auto error = prediction - data.y[row];
            gradient[0] += error;
            for (std::size_t j = 0; j + 1 < weights.size(); ++j) gradient[j + 1] += error * data.x[row][j];
        }
        for (std::size_t j = 0; j < weights.size(); ++j) {
            gradient[j] = gradient[j] * 2.0 / static_cast<double>(train.size()) + (j == 0 ? 0.0 : 2.0 * l2 * weights[j]);
            weights[j] -= rate * gradient[j];
            if (!std::isfinite(weights[j]) || std::abs(weights[j]) > 1e12) throw std::runtime_error("training diverged; reduce learning_rate range");
        }
    }
    double squared = 0;
    for (auto row : evaluate) {
        double prediction = weights[0];
        for (std::size_t j = 0; j + 1 < weights.size(); ++j) prediction += weights[j + 1] * data.x[row][j];
        const auto error = prediction - data.y[row]; squared += error * error;
    }
    return {std::move(weights), std::sqrt(squared / static_cast<double>(evaluate.size()))};
}

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

Json run_linear_regression_experiment(const Json& payload) {
    if (payload.value("model", "") != "linear_regression") throw std::invalid_argument("Supported native experiment model: linear_regression");
    const auto data = read_dataset(payload);
    if (!payload.contains("ranges") || !payload.at("ranges").is_array()) throw std::invalid_argument("Experiment requires hyperparameter ranges");
    std::vector<HyperparameterRange> ranges;
    for (const auto& range : payload.at("ranges")) {
        ranges.push_back({range.at("name").get<std::string>(), range.at("minimum").get<double>(), range.at("maximum").get<double>()});
    }
    const auto has_rate = std::find_if(ranges.begin(), ranges.end(), [](const auto& range) { return range.name == "learning_rate"; });
    if (has_rate == ranges.end()) throw std::invalid_argument("Experiment ranges must include learning_rate");
    const auto has_l2 = std::find_if(ranges.begin(), ranges.end(), [](const auto& range) { return range.name == "l2"; });
    if (has_l2 == ranges.end()) ranges.push_back({"l2", 0.0, 0.1});
    for (const auto& range : ranges) {
        if (range.name != "learning_rate" && range.name != "l2") throw std::invalid_argument("Unsupported linear_regression hyperparameter: " + range.name);
        if (range.name == "learning_rate" && range.minimum <= 0) throw std::invalid_argument("learning_rate range must be positive");
        if (range.name == "l2" && range.minimum < 0) throw std::invalid_argument("l2 range cannot be negative");
    }
    const auto epochs = payload.value("epochs", std::size_t{100});
    const auto trial_count = payload.value("trial_count", std::size_t{10});
    if (epochs == 0 || epochs > 300) throw std::invalid_argument("epochs must be between 1 and 300");
    if (trial_count == 0 || trial_count > 100) throw std::invalid_argument("linear_regression trial_count must be between 1 and 100");
    const auto seed = payload.value("seed", std::uint64_t{42});
    const auto study = optimize_hyperparameters(ranges, trial_count, [&](const Json& parameters) {
        try {
            return fit_linear(data, data.train, data.validation, parameters.at("learning_rate").get<double>(),
                              parameters.at("l2").get<double>(), epochs).rmse;
        } catch (const std::runtime_error&) {
            return 1e18; // Keep divergent proposals in the trial history as poor candidates.
        }
    }, false, seed);
    const auto& best = study.at("best_parameters");
    const auto trained = fit_linear(data, data.train, data.train, best.at("learning_rate").get<double>(), best.at("l2").get<double>(), epochs);
    const auto validation = fit_linear(data, data.train, data.validation, best.at("learning_rate").get<double>(), best.at("l2").get<double>(), epochs);
    auto coefficients = Json::array();
    for (std::size_t i = 1; i < trained.weights.size(); ++i) coefficients.push_back(trained.weights[i]);
    auto curve = Json::array();
    for (const auto& trial : study.at("trials")) curve.push_back({{"x", trial.at("trial")}, {"y", trial.at("objective")}});
    return {{"model", "linear_regression"}, {"dataset_rows", data.x.size()},
        {"train_rows", data.train.size()}, {"validation_rows", data.validation.size()},
        {"best_parameters", best}, {"best_objective", study.at("best_objective")},
        {"train_rmse", trained.rmse}, {"validation_rmse", validation.rmse},
        {"intercept", trained.weights[0]}, {"coefficients", std::move(coefficients)},
        {"plot", {{"title", "Validation RMSE by trial"}, {"x_label", "Trial"}, {"y_label", "Validation RMSE"},
                   {"series", Json::array({{{"name", "validation_rmse"}, {"values", std::move(curve)}}})}}},
        {"trials", study.at("trials")}, {"optimizer", study.at("optimizer")}};
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
        if (payload.value("model", "") == "linear_regression") {
            auto study = run_linear_regression_experiment(payload);
            if (!tracking_url.empty()) {
                MlflowLogger logger(std::move(transport), std::move(tracking_url));
                study["mlflow"] = logger.log_study(payload.value("experiment", "ark-native"), study);
            }
            message["status"] = "DONE";
            message["quality_score"] = -1.0;
            message["output"] = std::move(study);
            message["payload"].erase("dataset");
            message["payload"].erase("target");
            return message;
        }
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
        message["payload"].erase("target");
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
