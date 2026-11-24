#include "ark/scientific_report.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {

std::string latex(std::string_view value) {
    if (value.size() > 100'000 || value.find('\0') != std::string_view::npos) throw std::invalid_argument("Report text is empty or too large");
    std::string result;
    result.reserve(value.size() * 2);
    for (const char c : value) {
        switch (c) {
            case '\\': result += "\\textbackslash{}"; break;
            case '{': result += "\\{"; break;
            case '}': result += "\\}"; break;
            case '$': result += "\\$"; break;
            case '&': result += "\\&"; break;
            case '%': result += "\\%"; break;
            case '#': result += "\\#"; break;
            case '_': result += "\\_"; break;
            case '^': result += "\\textasciicircum{}"; break;
            case '~': result += "\\textasciitilde{}"; break;
            default: result.push_back(c);
        }
    }
    return result;
}

std::string required_text(const Json& source, const char* key, std::size_t limit = 10'000) {
    if (!source.is_object() || !source.contains(key) || !source.at(key).is_string()) throw std::invalid_argument(std::string("Report requires text field ") + key);
    const auto text = source.at(key).get<std::string>();
    if (text.empty() || text.size() > limit) throw std::invalid_argument(std::string("Invalid report text field ") + key);
    return text;
}

std::string describe_results(const Json& results) {
    if (!results.is_object() || results.empty() || results.dump().size() > 50'000) throw std::invalid_argument("Report results must be a non-empty object under 50 KiB");
    std::ostringstream output;
    for (const auto& [key, value] : results.items()) {
        output << "\\paragraph{" << latex(key) << "} " << latex(value.is_string() ? value.get<std::string>() : value.dump()) << "\n";
    }
    return output.str();
}

} // namespace

Json ScientificReportGenerator::run(Json message) const {
    if (!message.is_object()) throw std::invalid_argument("Scientific Report Generator requires an ARKMessage object");
    message["agent_version"] = "scientific_report_generator@1.0.0";
    try {
        if (!pending_for(message, "scientific_report_generator") || message.at("intent") != "GENERATE_REPORT")
            throw std::invalid_argument("Scientific Report Generator requires a pending GENERATE_REPORT message");
        const auto& payload = message.at("payload");
        const auto hypothesis = required_text(payload, "hypothesis");
        const auto& spec = payload.at("spec");
        const auto dataset = required_text(spec, "dataset", 25'000);
        const auto method = required_text(spec, "method", 25'000);
        const auto baseline = required_text(spec, "baseline", 25'000);
        const auto results = describe_results(payload.at("results"));

        Json citations = Json::array();
        if (payload.contains("references")) {
            const auto& references = payload.at("references");
            if (!references.is_array() || references.size() > 20) throw std::invalid_argument("Report accepts at most 20 direct references");
            std::set<std::string> seen;
            static const std::regex safe_url(R"(^https://[A-Za-z0-9.-]+(/[A-Za-z0-9._~:/?#\[\]@!$&'()*+,;=%-]*)?$)");
            for (const auto& reference : references) {
                if (!reference.is_object()) throw std::invalid_argument("Every report reference must be an object");
                const auto id = required_text(reference, "paper_id", 200);
                const auto title = required_text(reference, "title", 1000);
                if (!seen.insert(id).second) continue;
                Json item{{"id", id}, {"title", title}, {"source", "supplied_evidence"}};
                if (reference.contains("url") && !reference.at("url").is_null() && !reference.at("url").get<std::string>().empty()) {
                    const auto url = reference.at("url").get<std::string>();
                    if (url.size() > 2000 || !std::regex_match(url, safe_url) || url.find("..") != std::string::npos)
                        throw std::invalid_argument("Reference URL must be a safe HTTPS URL");
                    item["url"] = url;
                }
                citations.push_back(std::move(item));
            }
        } else if (payload.contains("paper_ids")) {
            if (!payload.at("paper_ids").is_array() || payload.at("paper_ids").size() > 20) throw std::invalid_argument("paper_ids must contain at most 20 graph IDs");
            for (const auto& id : payload.at("paper_ids")) {
                if (!id.is_string()) throw std::invalid_argument("Paper graph IDs must be strings");
                const auto rows = graph_.run({{"action", "list_citations"}, {"id", id.get<std::string>()}, {"limit", 20}}).value("rows", Json::array());
                for (const auto& row : rows) {
                    if (citations.size() >= 100) break;
                    citations.push_back(row);
                }
            }
        }

        std::ostringstream tex;
        tex << "\\documentclass{article}\n\\usepackage[T1]{fontenc}\n\\usepackage{graphicx}\n\\usepackage{hyperref}\n"
            << "\\title{" << latex(payload.value("title", "Scientific Study Draft")) << "}\n"
            << "\\author{ARK OS}\n\\date{\\today}\n\\begin{document}\n\\maketitle\n"
            << "\\begin{abstract}This draft describes a proposed study of " << latex(hypothesis)
            << ". Dataset: " << latex(dataset) << ". The reported results below are supplied study artifacts and have not been independently verified.\\end{abstract}\n"
            << "\\section{Introduction}\nThis study examines the hypothesis: ``" << latex(hypothesis) << "'' The motivation and claims in this draft should be checked against primary sources before publication.\n"
            << "\\section{Related Work}\n";
        if (citations.empty()) tex << "No linked papers were returned from the citation graph for the supplied paper IDs.\n";
        else {
            tex << "The citation graph returned the following linked works: ";
            for (std::size_t i = 0; i < citations.size(); ++i) tex << "\\cite{arkref" << i << "}" << (i + 1 == citations.size() ? ".\n" : ", ");
        }
        tex << "\\section{Method}\n" << latex(method) << "\\\\\nDataset plan: " << latex(dataset)
            << "\\\\\nBaseline: " << latex(baseline) << "\n\\section{Results}\n" << results << "\n";

        Json figures = payload.value("figures", Json::array());
        if (!figures.is_array() || figures.size() > 12) throw std::invalid_argument("Report accepts at most 12 figure descriptors");
        tex << "\\section{Figures}\n";
        for (const auto& figure : figures) {
            const auto caption = required_text(figure, "caption", 2000);
            const auto path = required_text(figure, "path", 1024);
            static const std::regex safe(R"([A-Za-z0-9_./-]+)");
            if (!std::regex_match(path, safe) || path.starts_with('/') || path.find("..") != std::string::npos)
                throw std::invalid_argument("Figure path must be a safe relative filename");
            tex << "\\begin{figure}[!htbp]\n\\centering\n\\includegraphics[width=0.9\\linewidth]{\\detokenize{" << path
                << "}}\n\\caption{" << latex(caption) << "}\n\\end{figure}\n";
        }
        if (figures.empty()) tex << "No figure files were supplied.\n";
        if (!citations.empty()) {
            tex << "\\begin{thebibliography}{99}\n";
            for (std::size_t i = 0; i < citations.size(); ++i) {
                const auto& row = citations.at(i);
                tex << "\\bibitem{arkref" << i << "}" << latex(row.value("title", "Untitled"))
                    << ". Source identifier: \\texttt{" << latex(row.value("id", "unknown")) << "}.";
                if (row.contains("url")) tex << " Available at \\url{" << row.at("url").get<std::string>() << "}.";
                else if (!(row.contains("source") && row.at("source") == "supplied_evidence")) tex << " Citation graph record.";
                tex << "\n";
            }
            tex << "\\end{thebibliography}\n";
        }
        tex << "\\end{document}\n";
        auto document = tex.str();
        if (document.size() > 250'000) throw std::invalid_argument("Generated LaTeX document exceeds 250 KiB");
        message["status"] = "DONE"; message["quality_score"] = -1.0;
        message["output"] = {{"format", "latex"}, {"filename", "report.tex"}, {"content", std::move(document)},
            {"citation_count", citations.size()}, {"figures", figures.size()}, {"draft_status", "unverified"}};
        message["payload"].erase("results");
    } catch (const TransientHttpError& error) {
        message["status"] = "RETRY"; message["quality_score"] = -1.0;
        message["output"] = {{"error_type", "CitationGraphUnavailable"}, {"error_message", std::string(error.what()).substr(0, 300)}};
        if (message.contains("payload")) message["payload"].erase("results");
    } catch (const std::exception& error) {
        message["status"] = "FAILED"; message["quality_score"] = -1.0;
        message["output"] = {{"error_type", "ScientificReportFailed"}, {"error_message", std::string(error.what()).substr(0, 300)}};
        if (message.contains("payload")) message["payload"].erase("results");
    }
    return message;
}

} // namespace ark
