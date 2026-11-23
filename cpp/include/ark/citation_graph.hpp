#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

namespace ark {

class CitationGraph {
public:
    CitationGraph(Transport transport, std::string url, std::string database = "neo4j",
                  std::string username = {}, std::string password = {});
    Json run(const Json& request) const;

private:
    Json query(std::string_view statement, const Json& params = Json::object()) const;
    Transport transport_;
    std::string endpoint_;
    std::vector<std::string> headers_;
};

} // namespace ark
