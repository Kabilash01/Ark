#include "ark/experiment_designer.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

Json design() {
    return {{"hypothesis", "Adding a small fraction of dopant X increases conductivity."},
        {"dataset", "Composition, processing temperature, conductivity; controlled synthetic batches."},
        {"method", "Randomized replicated preparation followed by four-point probe measurement."},
        {"baseline", "Undoped material prepared with the same processing schedule."},
        {"metrics", Json::array({{{"name", "conductivity"}, {"definition", "Mean conductivity in S/m across replicate samples."}}})},
        {"code_spec", {{"language", "C++20"}, {"files", Json::array({"experiment.cpp", "experiment_test.cpp"})},
            {"interface", "Read rows from CSV and compute grouped means with confidence intervals."},
            {"dependencies", "C++20 standard library."},
            {"acceptance_tests", Json::array({"Verify the undoped baseline is recovered.", "Reject missing conductivity values."})}}}};
}

void designs_cpp_experiment_from_hypothesis() {
    int calls = 0;
    const auto transport = [&](const ark::Request& request) -> ark::Response {
        ++calls;
        const auto body = Json::parse(request.body);
        check(body.at("model") == "fixture-model" && request.url.ends_with("/api/chat"), "wrong local inference request");
        check(body.at("messages").at(0).at("content").get<std::string>().find("C++20") != std::string::npos,
            "C++ implementation constraint was not provided to the designer");
        check(body.at("messages").at(1).at("content").get<std::string>().find("conductivity") != std::string::npos,
            "hypothesis was not forwarded");
        return {200, Json{{"message", {{"content", design().dump()}}}, {"done_reason", "stop"}}.dump()};
    };
    ark::ExperimentDesigner designer(transport, "http://127.0.0.1:11434", ark::experiment_designer_profile("fixture-model"));
    const auto message = ark::make_message("test", "experiment_designer", "DESIGN_EXPERIMENT", {{"hypothesis", "Adding dopant X increases conductivity."}});
    const auto result = designer.run(message);
    check(result.at("status") == "DONE" && result.at("output").at("code_spec").at("language") == "C++20"
        && result.at("output").at("metrics").size() == 1 && calls == 1,
        "experiment design response violated the structured contract");
}

void rejects_missing_hypotheses_before_inference() {
    int calls = 0;
    ark::ExperimentDesigner designer([&](const ark::Request&) -> ark::Response { ++calls; return {503, "{}"}; },
        "http://127.0.0.1:11434", ark::experiment_designer_profile("fixture-model"));
    auto message = ark::make_message("test", "experiment_designer", "DESIGN_EXPERIMENT", Json::object());
    const auto result = designer.run(std::move(message));
    check(result.at("status") == "FAILED" && calls == 0, "missing hypothesis reached inference");
}
}

int main() {
    try { designs_cpp_experiment_from_hypothesis(); rejects_missing_hypotheses_before_inference(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
