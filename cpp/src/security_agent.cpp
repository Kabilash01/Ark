#include "ark/security_agent.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>

namespace ark {
namespace {

constexpr auto semgrep_rules = R"yaml(rules:
  - id: dynamic-sql
    message: Build parameterized queries and pass values separately to execute.
    languages: [python]
    severity: ERROR
    pattern-either:
      - pattern: $CUR.execute($QUERY + $VALUE)
      - pattern: $CUR.execute($QUERY % $VALUE)
      - pattern: $CUR.execute($QUERY.format(...))
)yaml";

constexpr auto java_rules = R"yaml(rules:
  - id: java-dynamic-sql
    message: Review assembled SQL and bind request values through a prepared statement.
    languages: [java]
    severity: ERROR
    mode: taint
    pattern-sources:
      - pattern: $A + $B
      - pattern: String.format(...)
    pattern-sinks:
      - patterns:
          - pattern-either:
              - pattern: $ST.executeQuery($QUERY)
              - pattern: $ST.execute($QUERY)
              - pattern: $ST.executeUpdate($QUERY)
          - focus-metavariable: $QUERY
  - id: java-object-deserialization
    message: Check object deserialization boundaries and adopt a restricted data format for untrusted input.
    languages: [java]
    severity: WARNING
    pattern: $INPUT.readObject()
  - id: java-weak-message-digest
    message: Review MD5 or SHA-1 usage and use the algorithm appropriate to the security purpose.
    languages: [java]
    severity: WARNING
    pattern-either:
      - pattern: MessageDigest.getInstance("MD5")
      - pattern: MessageDigest.getInstance("SHA-1")
  - id: java-upload-path
    message: Constrain uploaded file destinations to a fixed directory and use server-generated names.
    languages: [java]
    severity: ERROR
    mode: taint
    pattern-sources:
      - pattern: $FILE.getOriginalFilename()
    pattern-sinks:
      - patterns:
          - pattern-either:
              - pattern: $ROOT.resolve($NAME)
              - pattern: new File($ROOT, $NAME)
          - focus-metavariable: $NAME
)yaml";

SandboxWorkspace workspace_for(const Json& request, bool java) {
    const auto& files = request.at("files");
    if (!files.is_object() || files.empty() || files.size() > 100) {
        throw std::invalid_argument("Supply 1 to 100 source files for the selected scanner language");
    }
    SandboxWorkspace workspace;
    for (const auto& [name, source] : files.items()) {
        if (std::filesystem::path(name).is_absolute() || !name.ends_with(java ? ".java" : ".py")) {
            throw std::invalid_argument("Scanner file paths must be relative and match the selected language");
        }
        workspace.write_file("source/" + name, source.get_ref<const std::string&>());
    }
    workspace.write_file("rules.yml", java ? java_rules : semgrep_rules);
    return workspace;
}

std::string scanner_path(std::string path) {
    constexpr std::string_view root = "source/";
    if (const auto at = path.find(root); at != std::string::npos) return path.substr(at + root.size());
    return path;
}

Json scan(const DockerSandbox& sandbox, const SandboxWorkspace& workspace, bool bandit) {
    const auto tool = bandit ? "bandit" : "semgrep";
    const auto result = sandbox.run_project_command(
        bandit ? "bandit --quiet --exit-zero -r source -f json"
               : "semgrep scan --metrics=off --disable-version-check --no-git-ignore --jobs=1 --json --config rules.yml source",
        workspace);
    if (result.exit_code != 0 || result.timed_out) {
        throw std::runtime_error(std::string(tool) + " did not complete in the Docker sandbox (exit "
                                 + std::to_string(result.exit_code) + ")");
    }
    const auto report = Json::parse(result.output);
    if (!report.at("results").is_array() || !report.at("errors").is_array() || !report.at("errors").empty()) {
        throw std::runtime_error(std::string(tool) + " reported incomplete scan coverage");
    }
    auto findings = Json::array();
    for (const auto& row : report.at("results")) {
        const auto id = row.at(bandit ? "test_id" : "check_id").get<std::string>();
        const bool credential = id == "B105" || id == "B106" || id == "B107";
        const bool sql = id == "B608" || id.ends_with("dynamic-sql");
        const auto& details = bandit ? row : row.at("extra");
        const auto detail = credential ? "Move hardcoded credentials into a managed secret."
            : sql ? "Bind values as parameters when executing SQL."
            : details.at(bandit ? "issue_text" : "message").get<std::string>();
        findings.push_back({{"tool", tool}, {"rule", id}, {"severity", details.at(bandit ? "issue_severity" : "severity")},
            {"file", scanner_path(row.at(bandit ? "filename" : "path").get<std::string>())},
            {"line", bandit ? row.at("line_number") : row.at("start").at("line")}, {"summary", detail},
            {"owasp", credential ? Json("A07:2021") : sql ? Json("A03:2021")
                : id == "java-weak-message-digest" ? Json("A02:2021")
                : id == "java-object-deserialization" ? Json("A08:2021")
                : id == "java-upload-path" ? Json("A01:2021") : Json(nullptr)}});
    }
    return findings;
}

std::string package_key(std::string name) {
    static const std::regex separators("[-_.]+");
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return std::regex_replace(name, separators, "-");
}

Json check_dependencies(const Json& request) {
    const auto dependencies = request.value("dependencies", Json::object());
    const auto advisories = request.value("advisories", Json::array());
    if (!dependencies.is_object() || !advisories.is_array()) {
        throw std::invalid_argument("Expected pinned dependencies and a local advisory array");
    }
    std::map<std::string, std::string> installed;
    for (const auto& [name, version] : dependencies.items()) {
        installed[package_key(name)] = version.get<std::string>();
    }
    auto findings = Json::array();
    for (const auto& row : advisories) {
        const auto& package = row.at("package").get_ref<const std::string&>();
        const auto version = installed.find(package_key(package));
        const auto versions = row.at("affected_versions").get<std::vector<std::string>>();
        if (version != installed.end() && std::find(versions.begin(), versions.end(), version->second) != versions.end()) {
            findings.push_back({{"package", package}, {"version", version->second}, {"advisory", row.at("id")},
                                {"owasp", "A06:2021"}});
        }
    }
    return {{"supplied_advisories", advisories.size()}, {"pinned_dependencies", installed.size()},
            {"findings", std::move(findings)}};
}

std::string html(std::string_view text) {
    std::string result;
    for (char c : text) {
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else result += c;
    }
    return result;
}

void audit_report(const Json& audit, const std::filesystem::path& path) {
    const auto temporary = path.string() + ".ark-" + random_uuid();
    struct Cleanup {
        const std::string& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    std::ofstream file(temporary);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>Security audit</title>"
            "<h1>Security audit</h1><h2>Static analyzer findings</h2><ul>";
    for (const auto& row : audit.at("findings")) {
        file << "<li>" << html(row.at("file").get<std::string>()) << ":" << row.at("line").get<int>()
             << " (" << html(row.at("tool").get<std::string>()) << " "
             << html(row.at("rule").get<std::string>()) << ") "
             << html(row.at("summary").get<std::string>()) << "</li>";
    }
    file << "</ul><h2>Dependency findings</h2><ul>";
    for (const auto& row : audit.at("dependencies").at("findings")) {
        file << "<li>" << html(row.at("package").get<std::string>()) << " "
             << html(row.at("version").get<std::string>()) << " "
             << html(row.at("advisory").get<std::string>()) << "</li>";
    }
    file << "</ul><p>Coverage includes static source checks and exact matches in the supplied advisory list.</p></html>";
    file.close();
    std::filesystem::rename(temporary, path);
}

Json execute(const DockerSandbox& sandbox, const Json& request) {
    const auto action = request.value("action", "audit");
    if (action == "check_dependencies") return check_dependencies(request);
    if (action != "audit" && action != "run_bandit" && action != "run_semgrep") {
        throw std::invalid_argument("Unknown security action");
    }
    const auto language = request.value("language", "python");
    const bool java = language == "java";
    if ((!java && language != "python") || (java && action == "run_bandit")) {
        throw std::invalid_argument("Select Python or Java for Semgrep, or Python for Bandit");
    }
    const auto workspace = workspace_for(request, java);
    auto findings = Json::array();
    if (action != "run_bandit") findings = scan(sandbox, workspace, false);
    if (action != "run_semgrep" && !java) {
        for (auto& finding : scan(sandbox, workspace, true)) findings.push_back(std::move(finding));
    }
    Json output{{"findings", std::move(findings)}, {"dependencies", check_dependencies(request)},
        {"owasp_coverage", {{"standard", "OWASP Top 10 (2021)"},
            {"mapped_categories", java ? Json::array({"A01:2021", "A02:2021", "A03:2021", "A06:2021", "A08:2021"})
                                       : Json::array({"A03:2021", "A06:2021", "A07:2021"})}}}};
    if (request.contains("report_path")) {
        const auto& path = request.at("report_path").get_ref<const std::string&>();
        if (path.empty() || path.find('\0') != std::string::npos) throw std::invalid_argument("Invalid report path");
        audit_report(output, path);
        output["report_path"] = path;
    }
    return output;
}

} // namespace

SecurityAgent::SecurityAgent(DockerSandbox sandbox) : sandbox_(std::move(sandbox)) {}

Json SecurityAgent::run(Json message) const {
    if (!message.is_object()) throw std::invalid_argument("Expected an ARKMessage");
    message["agent_version"] = "security_agent@1.0.0";
    try {
        if (!pending_for(message, "security_agent") || message.at("intent") != "SECURITY") {
            throw std::invalid_argument("SecurityAgent requires a pending SECURITY message");
        }
        message["output"] = execute(sandbox_, message.at("payload"));
        message["status"] = "DONE";
    } catch (const std::exception& error) {
        message["status"] = "FAILED";
        message["quality_score"] = 0.0;
        message["output"] = {{"error_type", "SecurityAuditError"}, {"error_message", error.what()}};
    }
    return message;
}

} // namespace ark
