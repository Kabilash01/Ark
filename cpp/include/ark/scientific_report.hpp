#pragma once

#include "ark/citation_graph.hpp"

namespace ark {

class ScientificReportGenerator {
public:
    explicit ScientificReportGenerator(CitationGraph& graph) : graph_(graph) {}
    Json run(Json message) const;

private:
    CitationGraph& graph_;
};

} // namespace ark
