#include "ark/done_condition.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <set>
#include <stdexcept>

namespace ark {
namespace {

constexpr std::array<std::string_view, 8> allowed_metrics{
    "quality_score", "novelty_score", "feasibility_score", "all_tests_pass",
    "coverage", "security_issues", "self_repair_rate", "citation_count",
};

bool identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

class Parser {
public:
    Parser(std::string_view source, const Json& metrics) : input_(source), metrics_(metrics) {}

    bool parse() {
        const auto result = disjunction();
        spaces();
        if (!input_.empty()) {
            syntax();
        }
        return result.value_or(false);
    }

private:
    using Truth = std::optional<bool>;
    std::string_view input_;
    const Json& metrics_;

    [[noreturn]] static void syntax() { throw std::invalid_argument("Invalid done condition expression"); }

    void spaces() {
        const auto used = input_.find_first_not_of(" \t\r\n");
        input_.remove_prefix(std::min(used, input_.size()));
    }

    bool consume(std::string_view token, bool word = false) {
        spaces();
        if (!input_.starts_with(token) || (word && input_.size() > token.size() && identifier_char(input_[token.size()]))) {
            return false;
        }
        input_.remove_prefix(token.size());
        return true;
    }

    static Truth either(Truth left, Truth right) {
        if (left == true || right == true) {
            return true;
        }
        return left && right ? Truth(false) : std::nullopt;
    }

    static Truth both(Truth left, Truth right) {
        if (left == false || right == false) {
            return false;
        }
        return left && right ? Truth(true) : std::nullopt;
    }

    Truth disjunction() {
        auto result = conjunction();
        while (consume("OR", true) || consume("or", true) || consume("||")) {
            result = either(result, conjunction());
        }
        return result;
    }

    Truth conjunction() {
        auto result = predicate();
        while (consume("AND", true) || consume("and", true) || consume("&&")) {
            result = both(result, predicate());
        }
        return result;
    }

    Truth predicate() {
        if (consume("NOT", true) || consume("not", true) || consume("!")) {
            const auto value = predicate();
            return value ? Truth(!*value) : std::nullopt;
        }
        if (consume("(")) {
            const auto value = disjunction();
            if (!consume(")")) {
                syntax();
            }
            return value;
        }
        const auto left = operand();
        for (const std::string_view op : {">=", "<=", "==", "!=", ">", "<"}) {
            if (consume(op)) {
                const auto right = operand();
                if (!left || !right) {
                    return std::nullopt;
                }
                if (op == ">=") return *left >= *right;
                if (op == "<=") return *left <= *right;
                if (op == "==") return *left == *right;
                if (op == "!=") return *left != *right;
                return op == ">" ? *left > *right : *left < *right;
            }
        }
        return left ? Truth(*left != 0) : std::nullopt;
    }

    std::optional<double> operand() {
        spaces();
        if (input_.empty()) {
            syntax();
        }
        double value = 0;
        const auto number = std::from_chars(input_.data(), input_.data() + input_.size(), value);
        if (number.ec == std::errc{} && std::isfinite(value)) {
            input_.remove_prefix(number.ptr - input_.data());
            return value;
        }
        const auto end = std::find_if_not(input_.begin(), input_.end(), identifier_char);
        const auto name = input_.substr(0, end - input_.begin());
        input_.remove_prefix(name.size());
        if (name == "true" || name == "True") return 1;
        if (name == "false" || name == "False") return 0;
        if (std::find(allowed_metrics.begin(), allowed_metrics.end(), name) == allowed_metrics.end()) {
            syntax();
        }
        const auto metric = metrics_.find(name);
        if (metric != metrics_.end() && metric->is_boolean()) {
            return metric->get<bool>() ? 1 : 0;
        }
        if (metric != metrics_.end() && metric->is_number()) {
            const auto measured = metric->get<double>();
            if (std::isfinite(measured)) {
                return measured;
            }
        }
        return std::nullopt;
    }
};

} // namespace

DoneCondition::DoneCondition(std::string expression) : expression_(std::move(expression)) {
    if (expression_.size() > 512) {
        throw std::invalid_argument("Done condition exceeds 512 bytes");
    }
    (void)evaluate(Json::object());
}

bool DoneCondition::evaluate(const Json& metrics) const {
    return Parser(expression_, metrics).parse();
}

int iteration_limit(const Json& value) {
    if (!value.is_number_integer() || value.get<double>() < 1 || value.get<double>() > 100) {
        throw std::invalid_argument("max_iter must be an integer between 1 and 100");
    }
    return value.get<int>();
}

Json measured_metrics(const Json& result) {
    auto metrics = Json::object();
    if (result.contains("quality_score") && result.at("quality_score").is_number()) {
        metrics["quality_score"] = result.at("quality_score");
    }
    std::set<std::string> citations;
    bool searched = false;
    int repairs = 0;
    int succeeded = 0;
    for (const auto& task : result.value("results", Json::object())) {
        const auto output = task.value("output", Json::object());
        if (output.contains("test_result") && output.contains("passed")) {
            const auto passed = output.at("passed").get<bool>();
            metrics["all_tests_pass"] = metrics.value("all_tests_pass", true) && passed;
            if (output.value("repairs", 0) > 0) {
                ++repairs;
                succeeded += passed;
            }
            if (output.at("test_result").contains("coverage")) {
                const auto coverage = output.at("test_result").at("coverage").get<double>();
                metrics["coverage"] = std::min(metrics.value("coverage", coverage), coverage);
            }
        }
        if (output.contains("citations") && output.at("citations").is_array()) {
            searched = true;
            for (const auto& source : output.at("citations")) {
                const auto url = source.value("url", std::string{});
                if (!url.empty()) {
                    citations.insert(url);
                }
            }
        }
    }
    if (repairs > 0) metrics["self_repair_rate"] = static_cast<double>(succeeded) / repairs;
    if (searched) metrics["citation_count"] = citations.size();
    return metrics;
}

} // namespace ark
