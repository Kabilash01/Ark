#include "ark/terminal.hpp"

#include <regex>

namespace ark {
namespace {

constexpr auto name = "terminal_agent";

Json failure(const SandboxResult& result, std::chrono::seconds timeout) {
    if (result.timed_out) {
        return {{"error_type", "Timeout"},
                {"error_message", "Command exceeded its " + std::to_string(timeout.count()) + " second timeout"}};
    }
    static const std::regex missing_module(
        R"((?:^|\n)ModuleNotFoundError: No module named (['"])([A-Za-z_][A-Za-z0-9_.]{0,127})\1\r?(?=\n|$))");
    std::string module;
    for (auto match = std::sregex_iterator(result.error.begin(), result.error.end(), missing_module);
         match != std::sregex_iterator(); ++match) {
        module = (*match)[2].str();
    }
    if (!module.empty()) {
        const auto package = module.substr(0, module.find('.'));
        return {{"error_type", "ModuleNotFoundError"}, {"error_message", "Python could not import " + module},
                {"missing_module", module}, {"suggestion", "python -m pip install " + package},
                {"suggestion_note", "Verify the distribution name for this import before installing it."}};
    }
    return {{"error_type", "CommandFailed"},
            {"error_message", "Command exited with status " + std::to_string(result.exit_code)}};
}

} // namespace

TerminalAgent::TerminalAgent(DockerSandbox sandbox) : sandbox_(std::move(sandbox)) {}

Json TerminalAgent::run(Json message) const {
    if (!message.is_object()) throw std::invalid_argument("An ARKMessage must be a JSON object");
    message["agent_version"] = "terminal_agent@1.0.0";
    try {
        if (!pending_for(message, name) || message.at("intent") != "EXECUTE") {
            throw std::invalid_argument("TerminalAgent requires a pending EXECUTE message");
        }
        const auto& payload = message.at("payload");
        if (!payload.contains("command") || !payload.at("command").is_string()) {
            throw std::invalid_argument("Terminal command must be a string");
        }
        const auto& command = payload.at("command").get_ref<const std::string&>();
        const auto seconds = payload.value("timeout_seconds", Json(30));
        if (!seconds.is_number_integer() || seconds.get<double>() < 1 || seconds.get<double>() > 180) {
            throw std::invalid_argument("Terminal timeout_seconds must be an integer from 1 to 180");
        }
        const auto timeout = std::chrono::seconds(seconds.get<int>());
        const auto result = sandbox_.run_command(command, timeout);
        message["output"] = result.json();
        if (!result.timed_out && result.exit_code == 0) {
            message["status"] = "DONE";
            message["output"]["summary"] = "Command completed in the Docker sandbox";
            return message;
        }
        message["output"].update(failure(result, timeout));
    } catch (const std::invalid_argument& error) {
        message["output"] = {{"error_type", "InvalidTerminalRequest"}, {"error_message", error.what()}};
    } catch (const std::exception& error) {
        message["output"] = {{"error_type", "SandboxError"}, {"error_message", error.what()}};
    }
    message["status"] = "FAILED";
    message["quality_score"] = 0.0;
    return message;
}

} // namespace ark
