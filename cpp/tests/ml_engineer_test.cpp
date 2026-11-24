#include "ark/ml_engineer.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;

void check(bool ok, std::string_view message) { if (!ok) throw std::runtime_error(std::string(message)); }

void optimizes_and_tracks_ten_trials() {
    const std::vector<ark::HyperparameterRange> ranges{{"learning_rate", 0.0, 1.0}, {"weight_decay", 0.0, 1.0}};
    const auto study = ark::optimize_hyperparameters(ranges, 10, [](const Json& values) {
        const auto a = values.at("learning_rate").get<double>() - 0.73;
        const auto b = values.at("weight_decay").get<double>() - 0.21;
        return a * a + b * b;
    }, false, 2026);
    check(study.at("trials").size() == 10 && study.at("best_objective").is_number(), "optimizer did not retain 10 scores");
    check(study.at("optimizer") == "ark_tpe_style_v1", "optimizer metadata is missing");

    int created = 0, logged = 0, updated = 0;
    const auto transport = [&](const ark::Request& request) -> ark::Response {
        const auto body = Json::parse(request.body);
        if (request.url.ends_with("experiments/get-by-name?experiment_name=vision")) return {404, "{}"};
        if (request.url.ends_with("experiments/create")) { ++created; return {200, R"({"experiment_id":"17"})"}; }
        if (request.url.ends_with("runs/create")) { check(body.at("experiment_id") == "17", "wrong experiment ID"); return {200, R"({"run":{"info":{"run_id":"run-1"}}})"}; }
        if (request.url.ends_with("runs/log-batch")) {
            ++logged;
            check(body.at("metrics").size() == 30 && body.at("params").size() == 3,
                "trial metric history or stable study parameters were missing");
            return {200, "{}"};
        }
        if (request.url.ends_with("runs/update")) { ++updated; check(body.at("status") == "FINISHED", "run was not finished"); return {200, "{}"}; }
        throw std::runtime_error("unexpected MLflow URL: " + request.url);
    };
    const auto tracking = ark::MlflowLogger(transport, "http://127.0.0.1:5000").log_study("vision", study);
    check(created == 1 && logged == 1 && updated == 1 && tracking.at("logged_trials") == 10,
          "MLflow did not record the full study lifecycle");
}

void validates_agent_contract_and_finite_scores() {
    auto message = ark::make_message("test", "ml_engineer_agent", "ML_OPTIMIZE", {
        {"ranges", Json::array({{{"name", "learning_rate"}, {"minimum", 0.0}, {"maximum", 1.0}}})},
        {"target", {{"learning_rate", 0.5}}}, {"trial_count", 10}});
    const auto result = ark::run_ml_engineer({}, "", std::move(message));
    check(result.at("status") == "DONE" && result.at("output").at("trials").size() == 10,
          "native ML Engineer did not run its deterministic smoke objective");
    bool rejected = false;
    try { (void)ark::optimize_hyperparameters({{"rate", 1.0, 1.0}}, 10, [](const Json&) { return 0.0; }); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "invalid hyperparameter range was accepted");
}

void trains_and_validates_native_linear_model() {
    Json features = Json::array(), targets = Json::array();
    for (int i = 0; i < 100; ++i) {
        const double x = static_cast<double>(i) / 100.0;
        features.push_back(Json::array({x}));
        targets.push_back(1.2 + 2.5 * x);
    }
    const Json payload{{"model", "linear_regression"}, {"dataset", {{"features", features}, {"targets", targets}}},
        {"ranges", Json::array({{{"name", "learning_rate"}, {"minimum", 0.02}, {"maximum", 0.3}},
            {{"name", "l2"}, {"minimum", 0.0}, {"maximum", 0.05}}})},
        {"trial_count", 10}, {"epochs", 120}, {"seed", 13}};
    auto message = ark::make_message("test", "ml_engineer_agent", "ML_OPTIMIZE", payload);
    const auto result = ark::run_ml_engineer({}, "", std::move(message));
    check(result.at("status") == "DONE", result.dump());
    check(!result.at("payload").contains("dataset"), "raw training data leaked in the response message");
    const auto& output = result.at("output");
    check(output.at("model") == "linear_regression" && output.at("trials").size() == 10,
        "linear experiment did not return its model and trial history");
    check(output.at("validation_rows") == 20 && output.at("validation_rmse").get<double>() < 0.25,
        "held-out validation RMSE was unexpectedly high");
    check(output.at("plot").at("series").at(0).at("values").size() == 10,
        "pipeline did not return plot-ready validation history");
    check(std::abs(output.at("intercept").get<double>() - 1.2) < 0.25
        && std::abs(output.at("coefficients").at(0).get<double>() - 2.5) < 0.5,
        "fitted linear parameters were far from the known synthetic model");
}
}

int main() {
    try { optimizes_and_tracks_ten_trials(); validates_agent_contract_and_finite_scores(); trains_and_validates_native_linear_model(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
