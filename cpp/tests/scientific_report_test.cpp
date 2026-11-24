#include "ark/scientific_report.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

Json report_request(Json figures = Json::array({{{"path", "figures/result_1.svg"}, {"caption", "Result & uncertainty_1"}}})) {
    return ark::make_message("test", "scientific_report_generator", "GENERATE_REPORT", {
        {"title", "Dopant X: conductivity study"},
        {"hypothesis", "Dopant X increases conductivity by changing grain boundaries."},
        {"spec", {{"dataset", "Composition and conductivity measurements."}, {"method", "Prepare controlled batches and measure conductivity."}, {"baseline", "Undoped samples."}}},
        {"results", {{"sample_count", 24}, {"validation_rmse", 0.12}, {"note", "Preliminary & unverified."}}},
        {"figures", std::move(figures)}, {"paper_ids", Json::array({"paper-a"})}});
}

void creates_latex_with_graph_citations_and_escaped_inputs() {
    int graph_calls = 0;
    const auto transport = [&](const ark::Request& request) -> ark::Response {
        ++graph_calls;
        const auto query = Json::parse(request.body);
        check(query.at("parameters").at("id") == "paper-a", "wrong citation graph paper ID");
        return {200, R"({"data":{"fields":["id","title"],"values":[["paper-b","Prior & related study"]]},"errors":[]})"};
    };
    ark::CitationGraph graph(transport, "http://127.0.0.1:7474");
    const auto result = ark::ScientificReportGenerator(graph).run(report_request());
    check(result.at("status") == "DONE" && result.at("output").at("format") == "latex" && graph_calls == 1,
        "report generation did not complete or query Neo4j");
    const auto content = result.at("output").at("content").get<std::string>();
    for (const auto* section : {"\\begin{abstract}", "\\section{Introduction}", "\\section{Related Work}",
                                 "\\section{Method}", "\\section{Results}", "\\section{Figures}", "\\bibitem{arkref0}"}) {
        check(content.find(section) != std::string::npos, "LaTeX draft is missing a required section");
    }
    check(content.find("Dopant X: conductivity study") != std::string::npos
        && content.find("Result \\& uncertainty\\_1") != std::string::npos
        && content.find("Prior \\& related study") != std::string::npos,
        "report content was not preserved or LaTeX escaped");
    check(result.at("output").at("citation_count") == 1 && !result.at("payload").contains("results"),
        "citation count or response data minimization failed");
}

void rejects_unsafe_figure_paths() {
    ark::CitationGraph graph([](const ark::Request&) -> ark::Response { throw std::runtime_error("should not query graph"); }, "http://127.0.0.1:7474");
    const auto result = ark::ScientificReportGenerator(graph).run(report_request(Json::array({{{"path", "../escape.pdf"}, {"caption", "unsafe"}}})));
    check(result.at("status") == "FAILED", "unsafe figure path was accepted");
}

void includes_hypothesis_evidence_in_safe_bibliography() {
    ark::CitationGraph graph([](const ark::Request&) -> ark::Response { throw std::runtime_error("direct evidence must not query Neo4j"); }, "http://127.0.0.1:7474");
    auto request = report_request(Json::array({{{"path", "figures/result.svg"}, {"caption", "band gap screening"}}}));
    request["payload"]["paper_ids"] = Json::array();
    request["payload"]["references"] = Json::array({{{"paper_id", "2401.00001"}, {"title", "A & B solar materials"},
        {"url", "https://arxiv.org/abs/2401.00001"}}});
    const auto result = ark::ScientificReportGenerator(graph).run(std::move(request));
    check(result.at("status") == "DONE" && result.at("output").at("citation_count") == 1,
        "supplied paper evidence did not produce a citation");
    const auto content = result.at("output").at("content").get<std::string>();
    check(content.find("A \\& B solar materials") != std::string::npos
        && content.find("\\url{https://arxiv.org/abs/2401.00001}") != std::string::npos
        && content.find("\\begin{figure}[!htbp]") != std::string::npos,
        "bibliography, URL, or figure placement was not rendered safely");
}

void rejects_unsafe_reference_urls() {
    ark::CitationGraph graph([](const ark::Request&) -> ark::Response { throw std::runtime_error("should not query graph"); }, "http://127.0.0.1:7474");
    auto request = report_request(Json::array());
    request["payload"].erase("paper_ids");
    request["payload"]["references"] = Json::array({{{"paper_id", "paper_1"}, {"title", "unsafe source"},
        {"url", "javascript:alert(1)"}}});
    const auto result = ark::ScientificReportGenerator(graph).run(std::move(request));
    check(result.at("status") == "FAILED", "unsafe bibliography URL was accepted");
}
}

int main() {
    try { creates_latex_with_graph_citations_and_escaped_inputs(); rejects_unsafe_figure_paths();
        includes_hypothesis_evidence_in_safe_bibliography(); rejects_unsafe_reference_urls(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
