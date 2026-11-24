#include "ark/data_analysis.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

void computes_stats_correlations_and_three_svg_plots() {
    auto message = ark::make_message("test", "data_analysis_agent", "ANALYZE_DATA",
        {{"csv", "x&amp;,y,label\n1,2,a\n2,4,b\n3,6,c\n"}});
    message["payload"]["csv"] = "x&gain,y,label\n1,2,a\n2,4,b\n3,6,c\n";
    const auto result = ark::analyze_csv(std::move(message));
    check(result.at("status") == "DONE", result.dump());
    check(result.at("output").at("rows") == 3 && result.at("output").at("numeric_columns").size() == 2,
        "row or numeric-column summary is incorrect");
    const auto& x_stats = result.at("output").at("numeric_columns").at(0);
    check(x_stats.at("column") == "x&gain" && std::abs(x_stats.at("mean").get<double>() - 2.0) < 1e-10
        && std::abs(x_stats.at("stddev_sample").get<double>() - 1.0) < 1e-10,
        "mean or sample standard deviation is incorrect");
    check(std::abs(result.at("output").at("correlation").at(0).at("values").at("y").get<double>() - 1.0) < 1e-10,
        "pairwise Pearson correlation is incorrect");
    const auto& plots = result.at("output").at("plots");
    check(plots.size() == 3 && plots.at(0).at("content").get<std::string>().starts_with("<svg")
        && plots.at(0).at("content").get<std::string>().find("x&amp;gain") != std::string::npos,
        "three SVG plots or XML escaping are missing");
    check(!result.at("payload").contains("csv"), "source CSV leaked in the response payload");
}

void rejects_malformed_csv() {
    auto message = ark::make_message("test", "data_analysis_agent", "ANALYZE_DATA", {{"csv", "a,b\n1\n2,3\n"}});
    const auto result = ark::analyze_csv(std::move(message));
    check(result.at("status") == "FAILED", "inconsistent CSV rows were accepted");
}
}

int main() {
    try { computes_stats_correlations_and_three_svg_plots(); rejects_malformed_csv(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
