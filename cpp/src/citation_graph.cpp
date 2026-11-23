#include "ark/citation_graph.hpp"

#include <openssl/evp.h>

#include <regex>

namespace ark {
namespace {

std::string field(const Json& request, const char* key) {
    const auto value = request.at(key).get<std::string>();
    if (value.size() > 500 || value.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument(std::string("Invalid citation graph field ") + key);
    }
    return value;
}

int limit(const Json& request) {
    const auto count = request.value("limit", Json(20));
    if (!count.is_number_integer() || count.get<double>() < 1 || count.get<double>() > 100) {
        throw std::invalid_argument("Citation graph limit must be an integer from 1 to 100");
    }
    return count.get<int>();
}

} // namespace

CitationGraph::CitationGraph(Transport transport, std::string url, std::string database,
                             std::string username, std::string password)
    : transport_(std::move(transport)), headers_{"Content-Type: application/json", "Accept: application/json"} {
    static const std::regex name(R"([A-Za-z0-9][A-Za-z0-9.-]{0,62})");
    if (!std::regex_match(database, name)) throw std::invalid_argument("Invalid Neo4j database name");
    endpoint_ = local_base_url(url) + "/db/" + database + "/query/v2";
    if (!username.empty()) {
        if (username.find(':') != std::string::npos || username.size() + password.size() > 4096) {
            throw std::invalid_argument("Invalid Neo4j credentials");
        }
        const auto input = username + ":" + password;
        std::string encoded(4 * ((input.size() + 2) / 3) + 1, '\0');
        const auto size = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
            reinterpret_cast<const unsigned char*>(input.data()), static_cast<int>(input.size()));
        encoded.resize(size);
        headers_.push_back("Authorization: Basic " + encoded);
    }
}

Json CitationGraph::query(std::string_view statement, const Json& params) const {
    const Json input{{"statement", statement}, {"parameters", params}};
    const auto response = transport_({endpoint_, "POST", input.dump(), headers_});
    response.require_success("Neo4j query");
    const auto result = Json::parse(response.body);
    const auto errors = result.value("errors", Json::array());
    if (!errors.empty()) {
        throw std::runtime_error("Neo4j query failed: " + errors.at(0).at("message").get<std::string>());
    }
    const auto& data = result.at("data");
    const auto& columns = data.at("fields");
    auto rows = Json::array();
    for (const auto& values : data.at("values")) {
        if (values.size() != columns.size()) throw std::runtime_error("Neo4j row has an unexpected size");
        auto row = Json::object();
        for (std::size_t i = 0; i < columns.size(); ++i) {
            row[columns.at(i).get<std::string>()] = values.at(i);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

Json CitationGraph::run(const Json& request) const {
    const auto action = field(request, "action");
    if (action == "initialize") {
        query("CREATE CONSTRAINT ark_paper_id IF NOT EXISTS FOR (p:Paper) REQUIRE p.id IS UNIQUE");
        query("CREATE CONSTRAINT ark_concept_name IF NOT EXISTS FOR (c:Concept) REQUIRE c.name IS UNIQUE");
        return {{"ready", true}};
    }
    if (action == "add_paper") {
        auto concepts = request.value("concepts", std::vector<std::string>{});
        if (concepts.size() > 100) throw std::invalid_argument("A paper can mention at most 100 concepts");
        for (const auto& concept_name : concepts) field({{"name", concept_name}}, "name");
        return {{"rows", query(R"(MERGE (p:Paper {id:$id}) SET p.title=$title
FOREACH (concept IN $concepts | MERGE (c:Concept {name:concept}) MERGE (p)-[:MENTIONS]->(c))
RETURN p.id AS id, p.title AS title)",
            {{"id", field(request, "id")}, {"title", field(request, "title")}, {"concepts", concepts}})}};
    }
    if (action == "add_citation" || action == "add_related") {
        const auto source = field(request, "from");
        const auto target = field(request, "to");
        if (source == target) throw std::invalid_argument("Graph links require distinct endpoints");
        const auto statement = action == "add_citation"
            ? "MATCH (a:Paper {id:$from}), (b:Paper {id:$to}) MERGE (a)-[:CITES]->(b) RETURN a.id AS source, b.id AS target"
            : "MATCH (a:Concept {name:$from}), (b:Concept {name:$to}) MERGE (a)-[:RELATED]-(b) RETURN a.name AS source, b.name AS target";
        const auto rows = query(statement, {{"from", source}, {"to", target}});
        if (rows.empty()) throw std::invalid_argument("Graph link endpoints must exist");
        return {{"rows", rows}};
    }
    if (action == "list_citations") {
        return {{"rows", query(R"(MATCH (:Paper {id:$id})-[:CITES]->(p:Paper)
RETURN p.id AS id, p.title AS title ORDER BY p.id LIMIT $limit)",
            {{"id", field(request, "id")}, {"limit", limit(request)}})}};
    }
    if (action == "find_concept_gaps") {
        return {{"rows", query(R"(MATCH (a:Concept)<-[:MENTIONS]-(p:Paper)-[:MENTIONS]->(b:Concept)
WHERE a.name < b.name AND NOT EXISTS { MATCH (a)-[:RELATED]-(b) }
RETURN a.name AS first, b.name AS second, count(DISTINCT p) AS papers
ORDER BY papers DESC, first, second LIMIT $limit)", {{"limit", limit(request)}})}};
    }
    if (action == "find_bridge_concepts") {
        return {{"rows", query(R"(MATCH (a:Concept)-[:RELATED]-(b:Concept)-[:RELATED]-(c:Concept)
WHERE a.name < c.name AND NOT EXISTS { MATCH (a)-[:RELATED]-(c) }
RETURN b.name AS concept, count(*) AS unlinked_pairs ORDER BY unlinked_pairs DESC, concept LIMIT $limit)",
            {{"limit", limit(request)}})}};
    }
    throw std::invalid_argument("Unknown citation graph action");
}

} // namespace ark
