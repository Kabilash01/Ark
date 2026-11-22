#pragma once

#include "ark/message.hpp"

namespace ark {

class DoneCondition {
public:
    explicit DoneCondition(std::string expression);
    bool evaluate(const Json& metrics) const;
    const std::string& expression() const { return expression_; }

private:
    std::string expression_;
};

Json measured_metrics(const Json& result);
int iteration_limit(const Json& value);

} // namespace ark
